#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

namespace pip3D
{
    namespace Bake
    {
        namespace Noise
        {

            [[nodiscard]] inline uint32_t pcg(uint32_t v) noexcept
            {
                v = v * 747796405u + 2891336453u;
                const uint32_t w = ((v >> ((v >> 28u) + 4u)) ^ v) * 277803737u;
                return (w >> 22u) ^ w;
            }

            [[nodiscard]] inline uint32_t pcg3d(uint32_t a, uint32_t b, uint32_t c) noexcept
            {
                uint32_t x = a * 0x9E3779B1u ^ b * 0x85EBCA77u ^ c * 0xC2B2AE3Du;
                x = pcg(x);
                return x;
            }

            [[nodiscard]] inline float u01(uint32_t v) noexcept
            {
                return static_cast<float>(v & 0xFFFFFFu) * (1.0f / 16777216.0f);
            }

            [[nodiscard]] inline float halton(uint32_t idx, uint32_t base) noexcept
            {
                float f = 1.0f;
                float r = 0.0f;
                while (idx > 0u)
                {
                    f /= static_cast<float>(base);
                    r += f * static_cast<float>(idx % base);
                    idx /= base;
                }
                return r;
            }

            [[nodiscard]] inline float halton01(uint32_t &s, uint32_t base) noexcept
            {
                const uint32_t idx = s++;
                return halton(idx, base);
            }

            class BlueNoise
            {
            public:
                BlueNoise()
                {
                    if (!tryLoadCache())
                        generateAndCache();
                }

                bool loadFromFile(const char *path) noexcept
                {
                    std::ifstream f(path, std::ios::binary);
                    if (!f)
                        return false;
                    f.read(reinterpret_cast<char *>(m_rank.data()), sizeof(m_rank));
                    return f.gcount() == static_cast<std::streamsize>(sizeof(m_rank));
                }

                bool saveToFile(const char *path) const noexcept
                {
                    try
                    {
                        std::filesystem::create_directories(std::filesystem::path(path).parent_path());
                    }
                    catch (...)
                    {
                    }
                    std::ofstream f(path, std::ios::binary);
                    if (!f)
                        return false;
                    f.write(reinterpret_cast<const char *>(m_rank.data()), sizeof(m_rank));
                    return static_cast<bool>(f);
                }

                [[nodiscard]] float at(int32_t x, int32_t y) const noexcept
                {
                    const uint32_t v = m_rank[(static_cast<uint32_t>(y & kMask) << kShift) |
                                              static_cast<uint32_t>(x & kMask)];
                    return static_cast<float>(v) * (1.0f / static_cast<float>(kCount));
                }

                [[nodiscard]] float atCh(int32_t x, int32_t y, uint32_t ch) const noexcept
                {
                    static constexpr uint32_t kOff[4][2] = {
                        {0, 0}, {89, 47}, {37, 101}, {113, 73}};
                    return at(x + static_cast<int32_t>(kOff[ch & 3][0]),
                              y + static_cast<int32_t>(kOff[ch & 3][1]));
                }

                static constexpr uint32_t kRankSize = 128u * 128u;
                [[nodiscard]] const uint16_t *rankTable() const noexcept { return m_rank.data(); }

            private:
                static constexpr uint32_t kSize = 128;
                static constexpr uint32_t kShift = 7;
                static constexpr uint32_t kMask = kSize - 1u;
                static constexpr uint32_t kCount = kSize * kSize;

                std::array<uint16_t, kCount> m_rank{};

                static constexpr uint32_t kKRad = 7;
                std::array<float, (2 * kKRad + 1) * (2 * kKRad + 1)> m_kernel{};

                void buildKernel() noexcept
                {
                    const float sigma = 1.9f;
                    const float inv2s2 = 1.0f / (2.0f * sigma * sigma);
                    float sum = 0.0f;
                    for (uint32_t y = 0; y <= 2 * kKRad; ++y)
                        for (uint32_t x = 0; x <= 2 * kKRad; ++x)
                        {
                            const int dx = static_cast<int>(x) - kKRad;
                            const int dy = static_cast<int>(y) - kKRad;
                            const float w = std::exp(-(dx * dx + dy * dy) * inv2s2);
                            m_kernel[y * (2 * kKRad + 1) + x] = w;
                            sum += w;
                        }
                    const float inv = 1.0f / sum;
                    for (float &w : m_kernel)
                        w *= inv;
                }

                void convEnergy(const std::array<uint8_t, kCount> &occ,
                                std::array<float, kCount> &energy) const noexcept
                {
                    for (uint32_t y = 0; y < kSize; ++y)
                        for (uint32_t x = 0; x < kSize; ++x)
                        {
                            float acc = 0.0f;
                            for (int ky = -static_cast<int>(kKRad); ky <= static_cast<int>(kKRad); ++ky)
                                for (int kx = -static_cast<int>(kKRad); kx <= static_cast<int>(kKRad); ++kx)
                                {
                                    const uint32_t ox = static_cast<uint32_t>((static_cast<int>(x) + kx) & static_cast<int>(kMask));
                                    const uint32_t oy = static_cast<uint32_t>((static_cast<int>(y) + ky) & static_cast<int>(kMask));
                                    if (occ[oy * kSize + ox])
                                        acc += m_kernel[(ky + kKRad) * (2 * kKRad + 1) + (kx + kKRad)];
                                }
                            energy[y * kSize + x] = acc;
                        }
                }

                void applyDelta(std::array<float, kCount> &energy, int32_t cx, int32_t cy,
                                float sign) const noexcept
                {
                    for (int ky = -static_cast<int>(kKRad); ky <= static_cast<int>(kKRad); ++ky)
                        for (int kx = -static_cast<int>(kKRad); kx <= static_cast<int>(kKRad); ++kx)
                        {
                            const uint32_t ox = static_cast<uint32_t>((cx + kx) & static_cast<int>(kMask));
                            const uint32_t oy = static_cast<uint32_t>((cy + ky) & static_cast<int>(kMask));
                            energy[oy * kSize + ox] +=
                                sign * m_kernel[(ky + kKRad) * (2 * kKRad + 1) + (kx + kKRad)];
                        }
                }

                static std::string cacheSavePath() noexcept
                {
                    static const char *const kCacheDirs[] = {
                        "Tools/Bake/Build",
                        "Build",
                    };
                    for (const char *dir : kCacheDirs)
                    {
                        std::error_code ec;
                        if (std::filesystem::is_directory(dir, ec))
                            return std::string(dir) + "/blue_noise.bin";
                    }
                    return "blue_noise.bin";
                }

                bool tryLoadCache() noexcept
                {
                    if (loadFromFile(cacheSavePath().c_str()))
                        return true;
                    if (const char *env = std::getenv("PIP3D_BAKE_OUT"))
                    {
                        std::string ep = std::string(env) + "/blue_noise.bin";
                        if (loadFromFile(ep.c_str()))
                            return true;
                    }
                    return false;
                }

                void generateAndCache() noexcept
                {
                    generate();
                    saveToFile(cacheSavePath().c_str());
                }

                void generate() noexcept
                {
                    buildKernel();

                    std::array<uint8_t, kCount> occ{};
                    std::array<float, kCount> energy{};

                    constexpr uint32_t kInitial = kCount / 10;
                    convEnergy(occ, energy);
                    for (uint32_t n = 0; n < kInitial; ++n)
                    {
                        uint32_t best = 0;
                        float bestE = 1e30f;
                        for (uint32_t i = 0; i < kCount; ++i)
                        {
                            if (!occ[i] && energy[i] < bestE)
                            {
                                bestE = energy[i];
                                best = i;
                            }
                        }
                        occ[best] = 1;
                        applyDelta(energy, static_cast<int32_t>(best % kSize),
                                   static_cast<int32_t>(best / kSize), 1.0f);
                    }

                    for (uint32_t iter = 0; iter < 4096; ++iter)
                    {
                        uint32_t weakest = 0;
                        float weakE = 1e30f;
                        for (uint32_t i = 0; i < kCount; ++i)
                            if (occ[i] && energy[i] < weakE)
                            {
                                weakE = energy[i];
                                weakest = i;
                            }
                        occ[weakest] = 0;
                        applyDelta(energy, static_cast<int32_t>(weakest % kSize),
                                   static_cast<int32_t>(weakest / kSize), -1.0f);

                        uint32_t bestSpot = 0;
                        float bestE = 1e30f;
                        for (uint32_t i = 0; i < kCount; ++i)
                            if (!occ[i] && energy[i] < bestE)
                            {
                                bestE = energy[i];
                                bestSpot = i;
                            }
                        occ[bestSpot] = 1;
                        applyDelta(energy, static_cast<int32_t>(bestSpot % kSize),
                                   static_cast<int32_t>(bestSpot / kSize), 1.0f);

                        if (bestSpot == weakest)
                            break;
                    }

                    std::array<uint8_t, kCount> filled{};
                    filled = occ;
                    convEnergy(filled, energy);

                    uint32_t rank = 0;
                    for (uint32_t i = 0; i < kCount; ++i)
                        if (filled[i])
                            m_rank[i] = static_cast<uint16_t>(rank++);

                    for (uint32_t n = rank; n < kCount; ++n)
                    {
                        uint32_t best = 0;
                        float bestE = 1e30f;
                        for (uint32_t i = 0; i < kCount; ++i)
                            if (!filled[i] && energy[i] < bestE)
                            {
                                bestE = energy[i];
                                best = i;
                            }
                        filled[best] = 1;
                        applyDelta(energy, static_cast<int32_t>(best % kSize),
                                   static_cast<int32_t>(best / kSize), 1.0f);
                        m_rank[best] = static_cast<uint16_t>(n);
                    }
                }
            };

            inline const BlueNoise &blueNoise() noexcept
            {
                static const BlueNoise bn;
                return bn;
            }

            [[nodiscard]] inline uint32_t quantDither(float v, float levels,
                                                      float bn01) noexcept
            {
                const float d = v * (levels - 1.0f) + (bn01 - 0.5f);
                const int32_t q = static_cast<int32_t>(d + 0.5f);
                return static_cast<uint32_t>(q < 0 ? 0 : (q > static_cast<int32_t>(levels - 1.0f) ? static_cast<int32_t>(levels - 1.0f) : q));
            }

            [[nodiscard]] inline uint16_t pack565Dither(float r, float g, float b,
                                                        int32_t x, int32_t y) noexcept
            {
                r = r < 0.0f ? 0.0f : (r > 1.0f ? 1.0f : r);
                g = g < 0.0f ? 0.0f : (g > 1.0f ? 1.0f : g);
                b = b < 0.0f ? 0.0f : (b > 1.0f ? 1.0f : b);
                const BlueNoise &bn = blueNoise();
                const uint32_t r5 = quantDither(r, 32.0f, bn.atCh(x, y, 0));
                const uint32_t g6 = quantDither(g, 64.0f, bn.atCh(x, y, 1));
                const uint32_t b5 = quantDither(b, 32.0f, bn.atCh(x, y, 2));
                return static_cast<uint16_t>((r5 << 11) | (g6 << 5) | b5);
            }

            [[nodiscard]] inline float lum709(float r, float g, float b) noexcept
            {
                return r * 0.2126f + g * 0.7152f + b * 0.0722f;
            }
        }
    }
}
