#pragma once

#include <cstdint>

namespace pip3D
{

    struct Texture
    {
        const uint16_t *data = nullptr;
        const uint16_t *mipData = nullptr;

        uint8_t shiftU = 0;
        uint8_t shiftV = 0;
        uint8_t mipCount = 0;

        PIP3D_FORCE_INLINE uint16_t maskU() const noexcept
        {
            return static_cast<uint16_t>((1u << shiftU) - 1u);
        }
        PIP3D_FORCE_INLINE float widthFlt() const noexcept
        {
            return static_cast<float>(1u << shiftU);
        }

        PIP3D_FORCE_INLINE uint8_t vShift() const noexcept
        {
            return (shiftV == 0) ? shiftU : shiftV;
        }
        PIP3D_FORCE_INLINE uint16_t maskV() const noexcept
        {
            const uint8_t sv = (shiftV == 0) ? shiftU : shiftV;
            return static_cast<uint16_t>((1u << sv) - 1u);
        }
        PIP3D_FORCE_INLINE float heightFlt() const noexcept
        {
            const uint8_t sv = (shiftV == 0) ? shiftU : shiftV;
            return static_cast<float>(1u << sv);
        }
    };
}