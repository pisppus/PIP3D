#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <vector>

#include "Core/Noise.hpp"

namespace pip3D
{
    namespace Bake
    {
        namespace Palette
        {
            struct BlockPt
            {
                uint8_t r, g, b;
            };
            constexpr uint32_t kMaxPal = 32;

            [[nodiscard]] inline int palDist(uint16_t a, uint16_t b) noexcept
            {
                const int dr = static_cast<int>((a >> 11) & 0x1F) - static_cast<int>((b >> 11) & 0x1F);
                const int dg = static_cast<int>((a >> 5) & 0x3F) - static_cast<int>((b >> 5) & 0x3F);
                const int db = static_cast<int>(a & 0x1F) - static_cast<int>(b & 0x1F);
                return dr * dr * 3 + dg * dg * 4 + db * db * 2;
            }

            inline void blockError(const uint16_t *vals, uint32_t n, const uint16_t *pal,
                                   const uint8_t *idx, float &maxD, float &avgD)
            {
                maxD = 0.0f;
                double sum = 0.0;
                for (uint32_t i = 0; i < n; ++i)
                {
                    const uint16_t c = pal[idx[i]];
                    const uint32_t dr = std::abs(static_cast<int>((c >> 11) & 0x1F) -
                                                 static_cast<int>((vals[i] >> 11) & 0x1F));
                    const uint32_t dg = std::abs(static_cast<int>((c >> 5) & 0x3F) -
                                                 static_cast<int>((vals[i] >> 5) & 0x3F));
                    const uint32_t db = std::abs(static_cast<int>(c & 0x1F) -
                                                 static_cast<int>(vals[i] & 0x1F));
                    const uint32_t dm = std::max(dr, std::max(dg, db));
                    if (static_cast<float>(dm) > maxD)
                        maxD = static_cast<float>(dm);
                    sum += static_cast<double>(dm);
                }
                avgD = static_cast<float>(sum / n);
            }

            [[nodiscard]] inline uint32_t ditheredIndex(int b1, int b2, double d1sq, double d2sq,
                                                        float bn01) noexcept
            {
                if (b2 < 0)
                    return static_cast<uint32_t>(b1);
                const float sd1 = std::sqrt(static_cast<float>(d1sq));
                const float sd2 = std::sqrt(static_cast<float>(d2sq));

                float p = 0.5f + (sd1 - sd2) * 0.45f;
                p = p < 0.0f ? 0.0f : (p > 1.0f ? 1.0f : p);
                return (bn01 < p) ? static_cast<uint32_t>(b2) : static_cast<uint32_t>(b1);
            }

            uint32_t quantizeBlockK(const uint16_t *vals, uint32_t n, uint32_t K, uint16_t *palOut,
                                    uint8_t *idxOut, int32_t ax, int32_t ay);

            inline void packIdxN(const uint8_t *idx, uint32_t n, uint32_t bits, uint8_t *out)
            {
                const uint32_t totalBits = n * bits;
                const uint32_t totalBytes = (totalBits + 7) / 8;
                for (uint32_t b = 0; b < totalBytes; ++b)
                    out[b] = 0;
                for (uint32_t i = 0; i < n; ++i)
                {
                    const uint32_t bitPos = i * bits;
                    uint32_t v = idx[i];
                    const uint32_t bytePos = bitPos >> 3;
                    const uint32_t shift = bitPos & 7u;
                    out[bytePos] |= static_cast<uint8_t>(v << shift);
                    if (shift + bits > 8)
                        out[bytePos + 1] |= static_cast<uint8_t>(v >> (8 - shift));
                }
            }

            struct RampFit
            {
                uint16_t e0 = 0, e1 = 0;
                uint8_t idx[64];
                float maxD = 0.0f, avgD = 0.0f;
            };
            void fitRampBlock(const uint16_t *vals, uint32_t n, int32_t ax, int32_t ay, RampFit &out);

        }
    }
}
