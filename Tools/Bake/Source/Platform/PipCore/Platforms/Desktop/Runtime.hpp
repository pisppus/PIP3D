#pragma once

#include <PipCore/Features.hpp>
#include <PipCore/Platform.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace pipcore::desktop
{
    class Runtime final
    {
    public:
        static Runtime &instance() noexcept;

        [[nodiscard]] bool configureDisplay(uint16_t width, uint16_t height) noexcept;
        [[nodiscard]] bool beginDisplay(uint8_t rotation) noexcept;
        [[nodiscard]] bool setDisplayRotation(uint8_t rotation) noexcept;

        [[nodiscard]] uint16_t width() const noexcept { return _width; }
        [[nodiscard]] uint16_t height() const noexcept { return _height; }

        void fillScreen565(uint16_t color565) noexcept;
        void writeRect565(int16_t x,
                          int16_t y,
                          int16_t w,
                          int16_t h,
                          const uint16_t *pixels,
                          int32_t stridePixels) noexcept;

        void pumpEvents() noexcept {}
        [[nodiscard]] bool shouldQuit() const noexcept;
        void requestQuit() noexcept { _shouldQuit = true; }

        [[nodiscard]] uint32_t nowMs() const noexcept;
        [[nodiscard]] uint64_t nowMicros() const noexcept;
        void delayMs(uint32_t ms) noexcept;

        [[nodiscard]] bool isRecording() const noexcept { return false; }
        void startRecording(const char *) noexcept {}
        void stopRecording() noexcept {}

        void saveFrameToHistory() noexcept {}
        void stepBack() noexcept {}

        void setTimeScale(float s) noexcept { _timeScale = s; }
        [[nodiscard]] float timeScale() const noexcept { return _timeScale; }
        void pause() noexcept { _paused = true; }
        void resume() noexcept { _paused = false; }
        [[nodiscard]] bool isPaused() const noexcept { return _paused; }

        void uiTogglePause() noexcept {}
        void uiStepFrame() noexcept {}
        void uiCycleTimeScale() noexcept {}
        void uiCycleSpiLimit() noexcept {}
        void uiToggleRgb565Preview() noexcept {}
        void uiToggleRecording() noexcept {}
        void uiSaveScreenshot() noexcept;
        void uiRestartProcess() noexcept {}
        void uiLogLine(const char *) noexcept {}

        void throttleSpiTransfer(size_t) noexcept {}

        void injectTouch(bool, int16_t, int16_t) noexcept {}
        void pinModeInput(uint8_t) noexcept {}
        [[nodiscard]] bool digitalRead(uint8_t) const noexcept { return false; }
        [[nodiscard]] int16_t analogRead(uint8_t) const noexcept { return 0; }

        void serialWrite(const char *s) noexcept;
        void serialWrite(const char *s, size_t n) noexcept;
        void serialWrite(const uint8_t *s, size_t n) noexcept;
        [[nodiscard]] bool serialAvailable() const noexcept { return false; }
        int serialRead() noexcept { return -1; }

        [[nodiscard]] const std::vector<uint16_t> &frame565() const noexcept { return _fb565; }

        [[nodiscard]] const std::string &windowTitle() const noexcept { return _windowTitle; }
        void setWindowTitle(const char *title) noexcept { _windowTitle = title ? title : ""; }

    private:
        Runtime() = default;
        ~Runtime() = default;
        Runtime(const Runtime &) = delete;
        Runtime &operator=(const Runtime &) = delete;

        std::string _windowTitle;
        uint16_t _width = 0;
        uint16_t _height = 0;
        bool _shouldQuit = false;
        bool _paused = false;
        float _timeScale = 1.0f;
        uint64_t _displayStartedUs = 0;
        std::vector<uint16_t> _fb565;
    };
}
