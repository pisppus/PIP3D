#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

namespace pip3D
{
    namespace Bake
    {
        void ensureConsoleAnsi() noexcept;

        void formatHumanTime(char *out, size_t cap, float secs, bool withDeci);

        class UnifiedProgressBar
        {
        public:
            using Clock = std::chrono::steady_clock;
            UnifiedProgressBar() = default;
            UnifiedProgressBar(const UnifiedProgressBar &) = delete;
            UnifiedProgressBar &operator=(const UnifiedProgressBar &) = delete;
            ~UnifiedProgressBar();
            void init(uint64_t totalUnits);
            void advance(uint64_t delta, const char *status);
            void setStatus(const char *status);
            void logf(const char *fmt, ...) noexcept;
            void finish(const char *finalStatus = "Bake Complete");

            void panic() noexcept;
            [[nodiscard]] bool isActive() const noexcept;

        private:
            static constexpr int kBarWidth = 22;
            static constexpr int kStatusMax = 16;
            void workerLoop();
            void stopWorker();
            void finishImpl(const char *finalStatus);
            void animateLocked();
            void paintLocked();
            void paintExactLocked();
            void renderInto(char *buf, size_t bufn, bool exact);
            std::atomic<uint64_t> m_target{0};
            std::atomic<bool> m_stopping{false};
            uint64_t m_total = 1;
            float m_shown = 0.0f;
            Clock::time_point m_startTime;
            Clock::time_point m_lastArrive;
            float m_rateEma = 0.0f;
            uint64_t m_lastTargetSeen = 0;
            std::string m_status = "Starting...";
            bool m_active = false;
            bool m_workerActive = false;
            std::thread m_worker;
            std::atomic<bool> m_panic{false};
            mutable std::mutex m_mutex;
        };
        inline UnifiedProgressBar &progressBar()
        {
            static UnifiedProgressBar bar;
            return bar;
        }

        void progressBarPanic() noexcept;
    }
}
