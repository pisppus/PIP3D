#pragma once

#include <PipCore/Features.hpp>

#if PIPCORE_TARGET_DESKTOP

#include <PipCore/Audio/Backend.hpp>
#include <atomic>
#include <cstdint>

namespace pipcore
{
    class Audio;
}

namespace pipcore::desktop
{
    class Audio final : public audio::Backend
    {
    public:
        Audio() = default;
        ~Audio() override = default;

        [[nodiscard]] bool init(const audio::BackendConfig &cfg) noexcept override
        {
            (void)cfg;
            return true;
        }
        void deinit() noexcept override {}

        [[nodiscard]] bool ready() const noexcept override { return _ready.load(std::memory_order_relaxed); }
        [[nodiscard]] uint32_t sampleRate() const noexcept override { return _sampleRate; }

        void writeInterleavedS16(const int16_t *, size_t) noexcept override {}

        void pumpCallback(int16_t *out, size_t frameCount) noexcept
        {

            if (out && frameCount > 0)
            {
                for (size_t i = 0; i < frameCount * 2; ++i)
                    out[i] = 0;
            }
        }

        void bindMixer(pipcore::Audio *mixer) noexcept { _mixer = mixer; }

    private:
        std::atomic<bool> _ready{false};
        uint32_t _sampleRate = 44100;
        pipcore::Audio *_mixer = nullptr;
    };
}

#endif
