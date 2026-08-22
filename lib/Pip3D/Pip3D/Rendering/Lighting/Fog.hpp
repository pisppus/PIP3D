#pragma once

#include <cstdint>

#include "Core/Platform.hpp"
#include "Math/Algebra.hpp"

namespace pip3D
{
    namespace Rasterizer
    {

        struct alignas(16) FogState
        {
            float worldNear = 0.0f;
            float worldScale = 0.0f;
            float worldScale32 = 0.0f;
            float wScale = 0.0f;

            uint32_t color_rb = 0;
            uint32_t color_g = 0;
            uint16_t color = 0;
            uint16_t pad16 = 0;
            bool enabled = false;
            uint8_t pad8[3] = {0, 0, 0};
        };
        static_assert(sizeof(FogState) == 32);

        inline FogState g_fogState{};

        inline bool g_mipmapsEnabled = true;

        inline float g_mipmapBias = 0.0f;
        inline float g_ambientScale = 1.0f;
        inline float g_exposureScale = 1.0f;

        struct FogLut
        {
            uint8_t alpha[257];
            bool valid;
        };

        inline FogLut g_fogLut{};

        __attribute__((cold)) inline void rebuildFogLut()
        {
            const FogState &f = g_fogState;
            if (!f.enabled)
            {
                g_fogLut.valid = false;
                return;
            }

            const float wScale = f.wScale;
            const float worldNear = f.worldNear;
            const float worldScale32 = f.worldScale32;

            uint32_t i = 0;
            for (; i + 8 <= 257; i += 8)
            {
                uint8_t out[8];
                for (uint32_t k = 0; k < 8; ++k)
                {
                    const uint32_t d = (i + k) * 128u;
                    if (d == 0u)
                    {
                        out[k] = 32u;
                        continue;
                    }
                    const float z_eye = wScale * FastMath::fastReciprocal(static_cast<float>(d));
                    float a = (z_eye - worldNear) * worldScale32;
                    if (a < 0.0f)
                        a = 0.0f;
                    else if (a > 32.0f)
                        a = 32.0f;
                    out[k] = static_cast<uint8_t>(a + 0.5f);
                }
                g_fogLut.alpha[i + 0] = out[0];
                g_fogLut.alpha[i + 1] = out[1];
                g_fogLut.alpha[i + 2] = out[2];
                g_fogLut.alpha[i + 3] = out[3];
                g_fogLut.alpha[i + 4] = out[4];
                g_fogLut.alpha[i + 5] = out[5];
                g_fogLut.alpha[i + 6] = out[6];
                g_fogLut.alpha[i + 7] = out[7];
            }
            for (; i < 257; ++i)
            {
                const uint32_t d = i * 128u;
                if (d == 0u)
                {
                    g_fogLut.alpha[i] = 32u;
                    continue;
                }
                const float z_eye = wScale * FastMath::fastReciprocal(static_cast<float>(d));
                float a = (z_eye - worldNear) * worldScale32;
                if (a < 0.0f)
                    a = 0.0f;
                else if (a > 32.0f)
                    a = 32.0f;
                g_fogLut.alpha[i] = static_cast<uint8_t>(a + 0.5f);
            }
            g_fogLut.valid = true;
        }

        struct FogColorF
        {
            float r, g, b;
        };
        PIP3D_FORCE_INLINE FogColorF fogColorFloat() noexcept
        {
            const uint16_t c = g_fogState.color;
            return FogColorF{
                static_cast<float>((c >> 11) & 0x1F) * (1.0f / 31.0f),
                static_cast<float>((c >> 5) & 0x3F) * (1.0f / 63.0f),
                static_cast<float>(c & 0x1F) * (1.0f / 31.0f)};
        }
    }
}
