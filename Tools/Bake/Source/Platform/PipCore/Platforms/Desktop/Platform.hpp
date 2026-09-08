#pragma once

#include <PipCore/Features.hpp>

#if PIPCORE_TARGET_DESKTOP

#include <PipCore/Display.hpp>
#include <PipCore/Platform.hpp>
#include <PipCore/Platforms/Desktop/Runtime.hpp>

namespace pipcore::desktop
{

    class Platform final : public pipcore::Platform
    {
    public:
        Platform();
        ~Platform() override;

        [[nodiscard]] uint32_t nowMs() noexcept override;
        [[nodiscard]] uint64_t nowUs() noexcept override;

        void pinModeInput(uint8_t pin, InputMode mode) noexcept override;
        [[nodiscard]] bool digitalRead(uint8_t pin) noexcept override;
        [[nodiscard]] int16_t analogRead(uint8_t pin) noexcept override;

        void *alloc(size_t bytes, AllocCaps caps = AllocCaps::Default) noexcept override;
        void free(void *ptr) noexcept override;
        void *allocAligned(size_t bytes, size_t align,
                           AllocCaps caps = AllocCaps::Default) noexcept override;
        void freeAligned(void *ptr) noexcept override;

        [[nodiscard]] bool configDisplay(const DisplayConfig &cfg) noexcept override;
        [[nodiscard]] bool beginDisplay(uint8_t rotation) noexcept override;
        [[nodiscard]] bool setDisplayRotation(uint8_t rotation) noexcept override;
        [[nodiscard]] Display *display() noexcept override;

        [[nodiscard]] uint32_t freeHeapTotal() noexcept override { return 256 * 1024 * 1024; }
        [[nodiscard]] uint32_t freeHeapInternal() noexcept override { return 256 * 1024 * 1024; }
        [[nodiscard]] uint32_t largestFreeBlock() noexcept override { return 64 * 1024 * 1024; }
        [[nodiscard]] uint32_t minFreeHeap() noexcept override { return 128 * 1024 * 1024; }

        [[nodiscard]] net::Backend *network() noexcept override { return nullptr; }
        [[nodiscard]] const net::Backend *network() const noexcept override { return nullptr; }
        [[nodiscard]] ota::Backend *update() noexcept override { return nullptr; }
        [[nodiscard]] const ota::Backend *update() const noexcept override { return nullptr; }

        [[nodiscard]] Touch *touch() noexcept override { return nullptr; }
        [[nodiscard]] const Touch *touch() const noexcept override { return nullptr; }

        [[nodiscard]] ::pipcore::Audio *audio() noexcept override { return nullptr; }
        [[nodiscard]] const ::pipcore::Audio *audio() const noexcept override { return nullptr; }

    private:
        Display *_display = nullptr;
    };
}

#endif
