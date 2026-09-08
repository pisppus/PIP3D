#include <PipCore/Platforms/Desktop/Runtime.hpp>
#include <PipCore/Platforms/Desktop/Platform.hpp>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace pipcore::desktop
{

    Runtime &Runtime::instance() noexcept
    {
        static Runtime inst;
        return inst;
    }

    bool Runtime::configureDisplay(uint16_t width, uint16_t height) noexcept
    {
        _width = width;
        _height = height;
        return true;
    }

    bool Runtime::beginDisplay(uint8_t rotation) noexcept
    {
        (void)rotation;
        _fb565.assign(static_cast<size_t>(_width) * _height, 0);
        _displayStartedUs = nowMicros();
        return true;
    }

    bool Runtime::setDisplayRotation(uint8_t rotation) noexcept
    {
        (void)rotation;
        return true;
    }

    bool Runtime::shouldQuit() const noexcept
    {
        constexpr uint64_t kAutoQuitUs = 10000000ull;
        return _shouldQuit ||
               (_displayStartedUs != 0 &&
                nowMicros() - _displayStartedUs > kAutoQuitUs);
    }

    void Runtime::fillScreen565(uint16_t color565) noexcept
    {
        if (_fb565.empty())
            return;
        std::fill(_fb565.begin(), _fb565.end(), color565);
    }

    void Runtime::writeRect565(int16_t x, int16_t y, int16_t w, int16_t h,
                               const uint16_t *pixels, int32_t stridePixels) noexcept
    {
        if (w <= 0 || h <= 0 || !pixels || _fb565.empty() || stridePixels <= 0)
            return;
        const int16_t x0 = (x < 0) ? 0 : x;
        const int16_t y0 = (y < 0) ? 0 : y;
        const int16_t x1 = std::min<int16_t>(static_cast<int16_t>(_width), static_cast<int16_t>(x + w));
        const int16_t y1 = std::min<int16_t>(static_cast<int16_t>(_height), static_cast<int16_t>(y + h));
        if (x0 >= x1 || y0 >= y1)
            return;
        for (int16_t yy = y0; yy < y1; ++yy)
        {
            const int row = yy - y;
            const uint16_t *src = pixels + static_cast<size_t>(row) * stridePixels + (x0 - x);
            uint16_t *dst = _fb565.data() + static_cast<size_t>(yy) * _width + x0;
            std::memcpy(dst, src, static_cast<size_t>(x1 - x0) * sizeof(uint16_t));
        }
    }

    uint32_t Runtime::nowMs() const noexcept
    {
        return static_cast<uint32_t>(nowMicros() / 1000ull);
    }

    uint64_t Runtime::nowMicros() const noexcept
    {
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch())
                .count());
    }

    void Runtime::delayMs(uint32_t ms) noexcept
    {
        (void)ms;
    }

    void Runtime::uiSaveScreenshot() noexcept
    {
        std::printf("[Bake] uiSaveScreenshot: no window in headless bake build\n");
    }

    void Runtime::serialWrite(const char *s) noexcept
    {
        if (s)
            std::fputs(s, stdout);
    }
    void Runtime::serialWrite(const char *s, size_t n) noexcept
    {
        if (s && n)
            std::fwrite(s, 1, n, stdout);
    }
    void Runtime::serialWrite(const uint8_t *s, size_t n) noexcept
    {
        if (s && n)
            std::fwrite(s, 1, n, stdout);
    }

    Platform::Platform() = default;
    Platform::~Platform() = default;

    uint32_t Platform::nowMs() noexcept
    {
        return Runtime::instance().nowMs();
    }
    uint64_t Platform::nowUs() noexcept
    {
        return Runtime::instance().nowMicros();
    }

    void Platform::pinModeInput(uint8_t, InputMode) noexcept {}
    bool Platform::digitalRead(uint8_t) noexcept { return false; }
    int16_t Platform::analogRead(uint8_t) noexcept { return 0; }

    void *Platform::alloc(size_t bytes, AllocCaps) noexcept
    {
        return std::malloc(bytes);
    }
    void Platform::free(void *ptr) noexcept { std::free(ptr); }
    void *Platform::allocAligned(size_t bytes, size_t align, AllocCaps) noexcept
    {
        if (align == 0)
            align = 16;
        if ((align & (align - 1)) != 0)
            return nullptr;
#if defined(_WIN32)
        return _aligned_malloc(bytes, align);
#else
        void *p = nullptr;
        if (::posix_memalign(&p, align, bytes) != 0)
            return nullptr;
        return p;
#endif
    }
    void Platform::freeAligned(void *ptr) noexcept
    {
#if defined(_WIN32)
        _aligned_free(ptr);
#else
        std::free(ptr);
#endif
    }

    bool Platform::configDisplay(const DisplayConfig &) noexcept { return true; }
    bool Platform::beginDisplay(uint8_t) noexcept { return true; }
    bool Platform::setDisplayRotation(uint8_t) noexcept { return true; }
    Display *Platform::display() noexcept { return nullptr; }

}