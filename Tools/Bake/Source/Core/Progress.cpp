#include "Core/Progress.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdarg>
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace pip3D
{
    namespace Bake
    {
        void ensureConsoleAnsi() noexcept
        {
#if defined(_WIN32)
            HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
            DWORD mode = 0;
            if (h && h != INVALID_HANDLE_VALUE && GetConsoleMode(h, &mode))
                SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
#endif
        }
        UnifiedProgressBar::~UnifiedProgressBar() { finishImpl(nullptr); }
        void UnifiedProgressBar::init(uint64_t totalUnits)
        {
            finishImpl(nullptr);
            ensureConsoleAnsi();
            std::lock_guard<std::mutex> lock(m_mutex);
            m_total = std::max<uint64_t>(1, totalUnits);
            m_target.store(0, std::memory_order_relaxed);
            m_shown = 0.0f;
            m_startTime = Clock::now();
            m_lastArrive = m_startTime;
            m_rateEma = 0.0f;
            m_lastTargetSeen = 0;
            m_status = "Starting...";
            m_active = true;
            m_stopping = false;
            std::fputs("\033[?25l", stdout);
            paintLocked();
            std::fflush(stdout);
            m_worker = std::thread(&UnifiedProgressBar::workerLoop, this);
            m_workerActive = true;
        }
        void UnifiedProgressBar::advance(uint64_t delta, const char *status)
        {
            m_target.fetch_add(delta, std::memory_order_relaxed);
            if (status && *status)
                setStatus(status);
        }
        void UnifiedProgressBar::setStatus(const char *status)
        {
            if (!status || !*status)
                return;
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_status != status)
                m_status = status;
        }
        void UnifiedProgressBar::logf(const char *fmt, ...) noexcept
        {
            va_list args;
            va_start(args, fmt);
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (m_active)
                    std::fputs("\r\033[K", stdout);
                std::vprintf(fmt, args);
                std::fputc('\n', stdout);
                std::fflush(stdout);
                if (m_active)
                    paintLocked();
            }
            va_end(args);
        }
        void UnifiedProgressBar::finish(const char *finalStatus) { finishImpl(finalStatus); }
        void UnifiedProgressBar::panic() noexcept
        {
            m_panic.store(true, std::memory_order_relaxed);
            std::fputs("\r\033[K\033[?25h", stdout);
            std::fflush(stdout);
        }
        void progressBarPanic() noexcept
        {
            progressBar().panic();
        }
        bool UnifiedProgressBar::isActive() const noexcept
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_active;
        }
        void UnifiedProgressBar::workerLoop()
        {
            while (!m_stopping.load(std::memory_order_relaxed) &&
                   !m_panic.load(std::memory_order_relaxed))
            {
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    if (m_active)
                        animateLocked();
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(16));
            }
        }
        void UnifiedProgressBar::stopWorker()
        {
            m_stopping.store(true, std::memory_order_relaxed);
            if (m_worker.joinable())
                m_worker.join();
            m_workerActive = false;
            m_stopping.store(false, std::memory_order_relaxed);
        }
        void UnifiedProgressBar::finishImpl(const char *finalStatus)
        {
            bool wasActive = false;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                wasActive = m_active;
                if (wasActive)
                {
                    m_active = false;
                    if (m_panic.load(std::memory_order_relaxed))
                        return;
                    m_status = (finalStatus && *finalStatus) ? finalStatus : m_status;
                    paintExactLocked();
                    std::fputs("\033[?25h\n", stdout);
                    std::fflush(stdout);
                }
            }
            if (m_workerActive)
                stopWorker();
        }
        void UnifiedProgressBar::animateLocked()
        {
            const auto now = Clock::now();
            const uint64_t tgtU = std::min<uint64_t>(m_target.load(std::memory_order_relaxed), m_total);
            const float dt = std::chrono::duration<float>(now - m_lastArrive).count();
            if (tgtU > m_lastTargetSeen && dt > 1e-4f)
            {
                const float inst = static_cast<float>(static_cast<double>(tgtU - m_lastTargetSeen) / dt);
                if (m_rateEma > 0.0f)
                {
                    const float alpha = (inst < m_rateEma) ? 0.35f : 0.15f;
                    m_rateEma = m_rateEma * (1.0f - alpha) + inst * alpha;
                }
                else
                    m_rateEma = inst;
                m_lastArrive = now;
                m_lastTargetSeen = tgtU;
            }
            const float tgt = static_cast<float>(tgtU);
            const float diff = tgt - m_shown;
            if (diff > 0.5f)
            {
                float step = diff * 0.18f + static_cast<float>(m_total) * 0.0006f;
                step = std::min(step, diff);
                step = std::max(step, static_cast<float>(m_total) * 0.0002f);
                m_shown += std::min(step, diff);
                if (m_shown > tgt)
                    m_shown = tgt;
            }
            else if (diff > 0.0f)
                m_shown = tgt;
            paintLocked();
            std::fflush(stdout);
        }
        void UnifiedProgressBar::paintLocked()
        {
            if (m_panic.load(std::memory_order_relaxed))
                return;
            char buf[256];
            renderInto(buf, sizeof(buf), false);
            std::fputs(buf, stdout);
        }
        void UnifiedProgressBar::paintExactLocked()
        {
            if (m_panic.load(std::memory_order_relaxed))
                return;
            m_shown = static_cast<float>(m_total);
            char buf[256];
            renderInto(buf, sizeof(buf), true);
            std::fputs(buf, stdout);
        }
        void formatHumanTime(char *out, size_t cap, float secs, bool withDeci)
        {
            if (secs < 0)
                secs = 0;
            if (secs < 60.0f)
            {
                if (withDeci)
                    std::snprintf(out, cap, "%.1fs", secs);
                else
                {
                    int s = static_cast<int>(secs + 0.5f);
                    if (s < 1)
                        s = 1;
                    std::snprintf(out, cap, "%ds", s);
                }
            }
            else if (secs < 3600.0f)
            {
                int m = static_cast<int>(secs / 60.0f);
                int s = static_cast<int>(secs - m * 60.0f + 0.5f);
                if (s >= 60)
                {
                    ++m;
                    s -= 60;
                }
                if (m >= 60)
                {
                    int h = m / 60;
                    m %= 60;
                    std::snprintf(out, cap, "%dh%02dm", h, m);
                }
                else
                    std::snprintf(out, cap, "%dm%02ds", m, s);
            }
            else
            {
                int h = static_cast<int>(secs / 3600.0f);
                int m = static_cast<int>((secs - h * 3600.0f) / 60.0f + 0.5f);
                if (m >= 60)
                {
                    ++h;
                    m = 0;
                }
                if (h < 100)
                    std::snprintf(out, cap, "%dh%02dm", h, m);
                else
                    std::snprintf(out, cap, "%dh+", h);
            }
        }

        void UnifiedProgressBar::renderInto(char *buf, size_t bufn, bool exact)
        {
            const float frac = exact ? 1.0f : (m_total > 0 ? m_shown / static_cast<float>(m_total) : 0.0f);
            int filled = static_cast<int>(static_cast<float>(kBarWidth) * frac + 1e-3f);
            filled = std::clamp(filled, 0, kBarWidth);
            char statusBuf[32];
            {
                const int srcn = static_cast<int>(std::min<size_t>(m_status.size(), kStatusMax));
                std::snprintf(statusBuf, sizeof(statusBuf), "%-*.*s", kStatusMax, srcn, m_status.c_str());
            }
            const float elapsed = std::chrono::duration<float>(Clock::now() - m_startTime).count();
            char elapsedBuf[16];
            formatHumanTime(elapsedBuf, sizeof(elapsedBuf), elapsed, true);
            char etaBuf[16];
            etaBuf[0] = '\0';
            if (!exact && m_rateEma > 0.0f)
            {
                const uint64_t shownU = static_cast<uint64_t>(m_shown);
                const uint64_t left = (m_total > shownU) ? (m_total - shownU) : 0;
                if (left > 0)
                {
                    const float etaSec = static_cast<float>(left) / m_rateEma;
                    char tmp[16];
                    formatHumanTime(tmp, sizeof(tmp), etaSec, false);
                    std::snprintf(etaBuf, sizeof(etaBuf), "~%s", tmp);
                }
            }
            char bar[80];
            {
                size_t w = 0;
                for (int i = 0; i < kBarWidth; ++i)
                {
                    const char *g = i < filled ? "\xE2\x96\x88" : "\xE2\x96\x91";
                    bar[w++] = g[0];
                    bar[w++] = g[1];
                    bar[w++] = g[2];
                }
                bar[w] = '\0';
            }
            std::snprintf(buf, bufn, "\r\033[36m[Pip3D]\033[0m [\033[32m%s\033[0m] \033[1m%5.1f%%\033[0m %-*s \033[90m%-7s %-8s\033[0m\033[K", bar, frac * 100.0f, kStatusMax, statusBuf, elapsedBuf, etaBuf);
        }
    }
}
