#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <vector>

#include "Encode/Atlas.hpp"
#include "Encode/Preview.hpp"
#include "Core/Noise.hpp"
#include "Core/Progress.hpp"
#include "Encode/Palette.hpp"

namespace pip3d_fs = std::filesystem;

namespace pip3D
{
    namespace Bake
    {
        using namespace Palette;

        namespace
        {
            std::string f6(float v)
            {
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%.6g", static_cast<double>(v));
                std::string out(buf);
                if (out.find('.') == std::string::npos &&
                    out.find('e') == std::string::npos &&
                    out.find('n') == std::string::npos &&
                    out.find('i') == std::string::npos)
                    out += ".0";
                return out;
            }

            std::string hex4(uint16_t v)
            {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "0x%04X", v);
                return std::string(buf);
            }

            std::string hex2(uint8_t v)
            {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "0x%02X", v);
                return std::string(buf);
            }

        }
        uint16_t decodeAtlasTexel(const BakeAtlas &atlas, uint32_t x, uint32_t y)
        {
            const uint32_t block = (y >> 3) * atlas.wBlocks + (x >> 3);
            const uint32_t i = ((y & 7) << 3) | (x & 7);
            const uint16_t d = atlas.dir[block];

            if ((d & 0xC000u) == 0xC000u)
            {
                const uint32_t base = d & 0x3FFFu;
                const uint32_t bitPos = i * 3u;
                const uint32_t bytePos = base * 24u + (bitPos >> 3);
                const uint32_t shift = bitPos & 7u;
                uint32_t v;
                if (shift <= 5u)
                    v = (atlas.rampIdx[bytePos] >> shift) & 7u;
                else
                    v = ((atlas.rampIdx[bytePos] |
                          (static_cast<uint32_t>(atlas.rampIdx[bytePos + 1]) << 8)) >>
                         shift) &
                        7u;
                const uint16_t a = atlas.ramp[base << 1];
                const uint16_t b = atlas.ramp[(base << 1) | 1];
                auto lerp7 = [](uint32_t p, uint32_t q, uint32_t t) -> int
                {
                    const int diff = static_cast<int>(q) - static_cast<int>(p);
                    const int mag = (((diff < 0 ? -diff : diff) * static_cast<int>(t) * 18724) + 65536) >> 17;
                    return (diff < 0) ? static_cast<int>(p) - mag : static_cast<int>(p) + mag;
                };
                const uint32_t rr = static_cast<uint32_t>(lerp7((a >> 11) & 0x1F, (b >> 11) & 0x1F, v));
                const uint32_t rg = static_cast<uint32_t>(lerp7((a >> 5) & 0x3F, (b >> 5) & 0x3F, v));
                const uint32_t rb = static_cast<uint32_t>(lerp7(a & 0x1F, b & 0x1F, v));
                return static_cast<uint16_t>((rr << 11) | (rg << 5) | rb);
            }
            if (d & 0x8000u)
                return atlas.uniformCol[d & 0x3FFFu];
            const uint32_t base = d & 0x3FFFu;
            if (d & 0x4000u)
            {
                const uint32_t bitPos = i * 3u;
                const uint32_t bytePos = base * 24u + (bitPos >> 3);
                const uint32_t shift = bitPos & 7u;
                uint32_t v;
                if (shift <= 5u)
                    v = (atlas.indices8[bytePos] >> shift) & 7u;
                else
                    v = ((atlas.indices8[bytePos] |
                          (static_cast<uint32_t>(atlas.indices8[bytePos + 1]) << 8)) >>
                         shift) &
                        7u;
                return atlas.palettes8[(base << 3) + v];
            }
            if (d & 0x2000u)
            {

                const uint32_t base32 = d & 0x1FFFu;
                const uint32_t bitPos = i * 5u;
                const uint32_t bytePos = base32 * 40u + (bitPos >> 3);
                const uint32_t shift = bitPos & 7u;
                uint32_t v;
                if (shift <= 3u)
                    v = (atlas.indices32[bytePos] >> shift) & 31u;
                else
                    v = ((atlas.indices32[bytePos] |
                          (static_cast<uint32_t>(atlas.indices32[bytePos + 1]) << 8)) >>
                         shift) &
                        31u;
                return atlas.palettes32[(base32 << 5) + v];
            }
            const uint8_t byte = atlas.indices[(base << 5) + (i >> 1)];
            const uint32_t idx4 = (i & 1) ? (byte >> 4) : (byte & 0x0Fu);
            return atlas.palettes[(base << 4) + idx4];
        }

        bool encodePaletteAtlas(BakeAtlas &atlas)
        {
            constexpr uint32_t kUniformLimit = 0x4000u;
            constexpr uint32_t kRampLimit = 0x4000u;
            constexpr uint32_t kPal8Limit = 0x4000u;
            constexpr uint32_t kPal16Limit = 0x4000u;
            constexpr uint32_t kPal32Limit = 0x2000u;

            if (atlas.w == 0 || atlas.h == 0 || atlas.data.empty())
                return false;
            if (atlas.w % 8 || atlas.h % 8)
                return false;

            atlas.wBlocks = atlas.w / 8;
            atlas.hBlocks = atlas.h / 8;
            const size_t blocks = static_cast<size_t>(atlas.wBlocks) * atlas.hBlocks;

            if (blocks > 65535)
            {
                progressBar().logf("\033[91m[-] Atlas too large (%zu blocks >65535) - reduce PIP3D_BAKE_TEXELS\033[0m",
                                   blocks);
                atlas.dir.clear();
                return false;
            }

            const bool haveF32 = (atlas.f32.size() == static_cast<size_t>(atlas.w) * atlas.h * 3);

            auto tileIsUniform = [&](uint32_t bx, uint32_t by) -> bool
            {
                if (!haveF32)
                    return false;
                const size_t base = (static_cast<size_t>(by) * 8) * atlas.w + bx * 8;
                float mn[3] = {1e30f, 1e30f, 1e30f};
                float mx[3] = {-1e30f, -1e30f, -1e30f};
                for (uint32_t y = 0; y < 8; ++y)
                {
                    const float *row = &atlas.f32[(base + static_cast<size_t>(y) * atlas.w) * 3];
                    for (uint32_t x = 0; x < 8; ++x)
                        for (int c = 0; c < 3; ++c)
                        {
                            const float v = row[x * 3 + c];
                            mn[c] = std::fmin(mn[c], v);
                            mx[c] = std::fmax(mx[c], v);
                        }
                }
                constexpr float kThrR = 0.75f / 31.0f;
                constexpr float kThrG = 0.75f / 63.0f;
                return (mx[0] - mn[0]) < kThrR &&
                       (mx[1] - mn[1]) < kThrG &&
                       (mx[2] - mn[2]) < kThrR;
            };

            std::vector<uint8_t> isUniform(blocks, 0);
            uint32_t uniformCount = 0;
            for (uint32_t by = 0; by < atlas.hBlocks; ++by)
                for (uint32_t bx = 0; bx < atlas.wBlocks; ++bx)
                {
                    const size_t block = static_cast<size_t>(by) * atlas.wBlocks + bx;
                    if (tileIsUniform(bx, by))
                    {
                        isUniform[block] = 1;
                        ++uniformCount;
                    }
                }
            if (uniformCount > kUniformLimit)
            {
                std::printf("\033[33m[!] uniform tiles %u > %u, demoting %u to palette\033[0m\n",
                            uniformCount, kUniformLimit, uniformCount - kUniformLimit);
                uint32_t keep = kUniformLimit;
                uint32_t seen = 0;
                for (size_t b = 0; b < blocks; ++b)
                {
                    if (!isUniform[b])
                        continue;
                    ++seen;
                    if (seen > keep)
                        isUniform[b] = 0;
                }
                uniformCount = kUniformLimit;
            }
            atlas.uniformTiles = uniformCount;

            const uint32_t normalCount = static_cast<uint32_t>(blocks - uniformCount);
            if (uniformCount > kUniformLimit || normalCount > (kRampLimit + kPal8Limit + kPal16Limit + kPal32Limit))
            {
                progressBar().logf("\033[91m[-] Atlas overflow: uni=%u normal=%u > class capacity - reduce PIP3D_BAKE_TEXELS\033[0m",
                                   uniformCount, normalCount);
                atlas.dir.clear();
                return false;
            }

            atlas.ramp.reserve(normalCount * 2);
            atlas.rampIdx.reserve(normalCount * 24);
            atlas.palettes8.reserve(normalCount * 8);
            atlas.indices8.reserve(normalCount * 24 + 1);
            atlas.palettes.reserve(normalCount * 16);
            atlas.indices.reserve(normalCount * 32);
            atlas.palettes32.reserve(normalCount * 32);
            atlas.indices32.reserve(normalCount * 40 + 1);
            atlas.uniformCol.assign(uniformCount ? uniformCount : 1, 0);
            atlas.dir.assign(blocks, 0);

            uint16_t vals[64];
            uint8_t packedBuf[48];

            uint32_t rampOrd = 0, ord8 = 0, ord16 = 0, ord32 = 0, uniOrd = 0;
            double errSumAvg = 0.0;
            uint32_t errTexels = 0;
            float errMaxLSB = 0.0f;
            atlas.rampTiles = atlas.pal8Tiles = atlas.pal16Tiles = atlas.pal32Tiles = 0;

            auto accountErr = [&](float maxD, float avgD)
            {
                errSumAvg += avgD * 64.0;
                errMaxLSB = std::fmax(errMaxLSB, maxD);
                errTexels += 64;
            };
            auto pushRamp = [&](size_t blockIdx, uint16_t e0, uint16_t e1, const uint8_t *idx3, float maxD, float avgD)
            {
                atlas.ramp.push_back(e0);
                atlas.ramp.push_back(e1);
                packIdxN(idx3, 64, 3, packedBuf);
                atlas.rampIdx.insert(atlas.rampIdx.end(), packedBuf, packedBuf + 24);
                atlas.dir[blockIdx] = static_cast<uint16_t>(0xC000u | rampOrd++);
                ++atlas.rampTiles;
                accountErr(maxD, avgD);
            };
            auto pushPal8 = [&](size_t blockIdx, const uint16_t *p, const uint8_t *ix, float maxD, float avgD)
            {
                atlas.palettes8.insert(atlas.palettes8.end(), p, p + 8);
                packIdxN(ix, 64, 3, packedBuf);
                atlas.indices8.insert(atlas.indices8.end(), packedBuf, packedBuf + 24);
                atlas.dir[blockIdx] = static_cast<uint16_t>(0x4000u | ord8++);
                ++atlas.pal8Tiles;
                accountErr(maxD, avgD);
            };
            auto pushPal16 = [&](size_t blockIdx, const uint16_t *p, const uint8_t *ix, float maxD, float avgD)
            {
                const size_t po = atlas.palettes.size();
                atlas.palettes.resize(po + 16);
                std::copy(p, p + 16, atlas.palettes.begin() + static_cast<long>(po));
                packIdxN(ix, 64, 4, packedBuf);
                atlas.indices.insert(atlas.indices.end(), packedBuf, packedBuf + 32);
                atlas.dir[blockIdx] = static_cast<uint16_t>(ord16++);
                ++atlas.pal16Tiles;
                accountErr(maxD, avgD);
            };
            auto pushPal32 = [&](size_t blockIdx, const uint16_t *p, const uint8_t *ix, float maxD, float avgD)
            {
                const size_t po32 = atlas.palettes32.size();
                atlas.palettes32.resize(po32 + 32);
                std::copy(p, p + 32, atlas.palettes32.begin() + static_cast<long>(po32));
                packIdxN(ix, 64, 5, packedBuf);
                atlas.indices32.insert(atlas.indices32.end(), packedBuf, packedBuf + 40);
                atlas.dir[blockIdx] = static_cast<uint16_t>(0x2000u | ord32++);
                ++atlas.pal32Tiles;
                accountErr(maxD, avgD);
            };

            for (uint32_t by = 0; by < atlas.hBlocks; ++by)
            {
                for (uint32_t bx = 0; bx < atlas.wBlocks; ++bx)
                {
                    const size_t block = static_cast<size_t>(by) * atlas.wBlocks + bx;
                    const int32_t ax = static_cast<int32_t>(bx * 8);
                    const int32_t ay = static_cast<int32_t>(by * 8);

                    if (isUniform[block])
                    {

                        float sr = 0.0f, sg = 0.0f, sb = 0.0f;
                        const size_t base = (static_cast<size_t>(by) * 8) * atlas.w + bx * 8;
                        for (uint32_t y = 0; y < 8; ++y)
                        {
                            const float *row = &atlas.f32[(base + static_cast<size_t>(y) * atlas.w) * 3];
                            for (uint32_t x = 0; x < 8; ++x)
                            {
                                sr += row[x * 3 + 0];
                                sg += row[x * 3 + 1];
                                sb += row[x * 3 + 2];
                            }
                        }
                        constexpr float kInv = 1.0f / 64.0f;
                        const uint32_t r5 = Noise::quantDither(sr * kInv, 32.0f, 0.5f);
                        const uint32_t g6 = Noise::quantDither(sg * kInv, 64.0f, 0.5f);
                        const uint32_t b5 = Noise::quantDither(sb * kInv, 32.0f, 0.5f);
                        atlas.uniformCol[uniOrd] = static_cast<uint16_t>(
                            (r5 << 11) | (g6 << 5) | b5);
                        atlas.dir[block] = static_cast<uint16_t>(0x8000u | uniOrd);
                        ++uniOrd;
                        continue;
                    }

                    if (haveF32)
                    {
                        for (uint32_t y = 0; y < 8; ++y)
                        {
                            const float *row = &atlas.f32[(static_cast<size_t>(by * 8 + y) * atlas.w + bx * 8) * 3];
                            for (uint32_t x = 0; x < 8; ++x, row += 3)
                            {
                                const float r = row[0] < 0.0f ? 0.0f : (row[0] > 1.0f ? 1.0f : row[0]);
                                const float g = row[1] < 0.0f ? 0.0f : (row[1] > 1.0f ? 1.0f : row[1]);
                                const float b = row[2] < 0.0f ? 0.0f : (row[2] > 1.0f ? 1.0f : row[2]);
                                const uint32_t r5 = static_cast<uint32_t>(r * 31.0f + 0.5f);
                                const uint32_t g6 = static_cast<uint32_t>(g * 63.0f + 0.5f);
                                const uint32_t b5 = static_cast<uint32_t>(b * 31.0f + 0.5f);
                                vals[y * 8 + x] = static_cast<uint16_t>((r5 << 11) | (g6 << 5) | b5);
                            }
                        }
                    }
                    else
                    {
                        for (uint32_t y = 0; y < 8; ++y)
                            for (uint32_t x = 0; x < 8; ++x)
                                vals[y * 8 + x] = atlas.data[(static_cast<size_t>(by * 8 + y)) * atlas.w + (bx * 8 + x)];
                    }

                    uint16_t pal[kMaxPal];
                    uint8_t idx[64];
                    float maxD = 0.0f, avgD = 0.0f;

                    RampFit rf;
                    fitRampBlock(vals, 64, ax, ay, rf);
                    if (rampOrd < kRampLimit && rf.maxD <= 1.8f && rf.avgD <= 0.45f)
                    {
                        pushRamp(block, rf.e0, rf.e1, rf.idx, rf.maxD, rf.avgD);
                        continue;
                    }

                    if (ord8 < kPal8Limit)
                    {
                        quantizeBlockK(vals, 64, 8, pal, idx, ax, ay);
                        blockError(vals, 64, pal, idx, maxD, avgD);
                        if (maxD <= 2.2f && avgD <= 0.60f)
                        {
                            pushPal8(block, pal, idx, maxD, avgD);
                            continue;
                        }
                    }

                    if (ord16 < kPal16Limit)
                    {
                        quantizeBlockK(vals, 64, 16, pal, idx, ax, ay);
                        blockError(vals, 64, pal, idx, maxD, avgD);
                        if (maxD <= 2.8f && avgD <= 0.75f)
                        {
                            pushPal16(block, pal, idx, maxD, avgD);
                            continue;
                        }
                    }

                    if (ord32 < kPal32Limit)
                    {
                        quantizeBlockK(vals, 64, 32, pal, idx, ax, ay);
                        blockError(vals, 64, pal, idx, maxD, avgD);
                        pushPal32(block, pal, idx, maxD, avgD);
                        continue;
                    }

                    if (ord16 < kPal16Limit)
                    {
                        quantizeBlockK(vals, 64, 16, pal, idx, ax, ay);
                        blockError(vals, 64, pal, idx, maxD, avgD);
                        pushPal16(block, pal, idx, maxD, avgD);
                    }
                    else if (ord8 < kPal8Limit)
                    {
                        quantizeBlockK(vals, 64, 8, pal, idx, ax, ay);
                        blockError(vals, 64, pal, idx, maxD, avgD);
                        pushPal8(block, pal, idx, maxD, avgD);
                    }
                    else
                    {
                        pushRamp(block, rf.e0, rf.e1, rf.idx, rf.maxD, rf.avgD);
                    }
                }
            }

            atlas.avgLSB = errTexels ? static_cast<float>(errSumAvg / errTexels) : 0.0f;
            atlas.maxLSB = errMaxLSB;

            if (atlas.palettes.empty())
            {
                atlas.palettes.assign(16, 0);
                atlas.indices.assign(32, 0);
            }

            if (!atlas.indices8.empty())
                atlas.indices8.push_back(0);
            if (!atlas.rampIdx.empty())
                atlas.rampIdx.push_back(0);
            if (!atlas.indices32.empty())
                atlas.indices32.push_back(0);

            if (std::getenv("PIP3D_BAKE_LOG"))
            {
                uint32_t maxDUni = 0, maxDNorm = 0;
                uint64_t sumD = 0, cnt = 0;
                for (uint32_t y = 0; y < atlas.h; ++y)
                {
                    for (uint32_t x = 0; x < atlas.w; ++x)
                    {
                        const uint32_t block = (y >> 3) * atlas.wBlocks + (x >> 3);
                        const uint16_t d = atlas.dir[block];
                        const uint16_t c = decodeAtlasTexel(atlas, x, y);
                        const uint32_t src = atlas.data[static_cast<size_t>(y) * atlas.w + x];
                        const uint32_t dr = std::abs(static_cast<int>((c >> 11) & 0x1F) -
                                                     static_cast<int>((src >> 11) & 0x1F));
                        const uint32_t dg = std::abs(static_cast<int>((c >> 5) & 0x3F) -
                                                     static_cast<int>((src >> 5) & 0x3F));
                        const uint32_t db = std::abs(static_cast<int>(c & 0x1F) -
                                                     static_cast<int>(src & 0x1F));
                        const uint32_t dmax = std::max(dr, std::max(dg, db));
                        if (d & 0x8000u && (d & 0xC000u) != 0xC000u)
                            maxDUni = std::max(maxDUni, dmax);
                        else
                            maxDNorm = std::max(maxDNorm, dmax);
                        sumD += dmax;
                        ++cnt;
                    }
                }

                progressBar().logf("\033[36m[Pip3D]\033[0m Encode verify: max diff uni=%u / normal=%u LSB, avg=%.3f%s",
                                   maxDUni, maxDNorm,
                                   cnt ? static_cast<double>(sumD) / cnt : 0.0,
                                   (maxDNorm > 8) ? " \033[91m[!] SUSPICIOUS\033[0m" : "");
            }
            return true;
        }

        void blurAtlas(BakeAtlas &atlas, uint32_t passes)
        {
            if (atlas.w == 0 || atlas.h == 0)
                return;
            const uint32_t w = atlas.w, h = atlas.h;
            if (atlas.f32.size() != static_cast<size_t>(w) * h * 3)
                return;
            passes = std::clamp(passes, 1u, 4u);

            std::vector<float> tmp(atlas.f32.size());
            for (uint32_t pass = 0; pass < passes; ++pass)
            {
                tmp = atlas.f32;
                for (uint32_t y = 0; y < h; ++y)
                {
                    for (uint32_t x = 0; x < w; ++x)
                    {
                        float r = 0.0f, g = 0.0f, b = 0.0f;
                        for (int ky = -1; ky <= 1; ++ky)
                        {
                            int sy = static_cast<int>(y) + ky;
                            if (sy < 0)
                                sy = 0;
                            else if (sy >= static_cast<int>(h))
                                sy = static_cast<int>(h) - 1;
                            for (int kx = -1; kx <= 1; ++kx)
                            {
                                int sx = static_cast<int>(x) + kx;
                                if (sx < 0)
                                    sx = 0;
                                else if (sx >= static_cast<int>(w))
                                    sx = static_cast<int>(w) - 1;
                                float wgt = (kx == 0 && ky == 0) ? 4.0f : (kx == 0 || ky == 0) ? 2.0f
                                                                                               : 1.0f;
                                const float *src = &tmp[(static_cast<size_t>(sy) * w + sx) * 3];
                                r += src[0] * wgt;
                                g += src[1] * wgt;
                                b += src[2] * wgt;
                            }
                        }
                        float *dst = &atlas.f32[(static_cast<size_t>(y) * w + x) * 3];
                        dst[0] = r / 16.0f;
                        dst[1] = g / 16.0f;
                        dst[2] = b / 16.0f;
                    }
                }
            }
            for (uint32_t y = 0; y < h; ++y)
            {
                for (uint32_t x = 0; x < w; ++x)
                {
                    const float *f = &atlas.f32[(static_cast<size_t>(y) * w + x) * 3];

                    atlas.data[static_cast<size_t>(y) * w + x] =
                        Noise::pack565Dither(f[0], f[1], f[2],
                                             static_cast<int32_t>(x), static_cast<int32_t>(y));
                }
            }
        }

        void writeBakedHeader(const char *sceneName, BakedLightMode mode,
                              const std::vector<InstanceBakeData> &bakes,
                              const std::map<Mesh *, MeshCacheEntry> &meshCache,
                              const BakeAtlas &atlas,
                              const ProbeBake &probes)
        {

            pip3d_fs::create_directories("src/Lighting");
            const std::string path = std::string("src/Lighting/Baked") + sceneName + ".hpp";
            std::ofstream os(path, std::ios::binary);
            if (!os || !atlas.encoded())
                return;

            const bool sparse = !atlas.dir.empty();
            const size_t atlasBytes =
                atlas.palettes.size() * 2 + atlas.indices.size() +
                atlas.palettes8.size() * 2 + atlas.indices8.size() +
                atlas.ramp.size() * 2 + atlas.rampIdx.size() +
                atlas.palettes32.size() * 2 + atlas.indices32.size() +
                (sparse ? (atlas.uniformCol.size() * 2 + atlas.dir.size() * 2) : 0);
            size_t uv2Bytes = 0;
            for (const auto &kv : meshCache)
                uv2Bytes += kv.second.uv2Mesh.size() * sizeof(LMUVQuant);
            const size_t probeBytes = probes.valid ? probes.probes.size() * 2 : 0;
            const size_t totalBytes = atlasBytes + uv2Bytes + probeBytes;

            uint64_t usedTexelArea = 0;
            for (const auto &b : bakes)
            {
                const uint64_t w = b.meshCache->rectW;
                const uint64_t h = b.meshCache->rectH;
                usedTexelArea += w * h;
            }
            const float fillPercent = (atlas.w > 0 && atlas.h > 0)
                                          ? (static_cast<float>(usedTexelArea) / static_cast<float>(atlas.w * atlas.h) * 100.0f)
                                          : 0.0f;
            const std::string probeDims = probes.valid
                                              ? (std::to_string(probes.dx) + "x" + std::to_string(probes.dy) + "x" + std::to_string(probes.dz))
                                              : "none";

            os << "/*\n"
               << " * Pip3D Asset - Baked" << sceneName << "\n"
               << " * Generated automatically by Tools/Bake. Do not edit.\n"
               << " *\n"
               << " * Source          : " << sceneName << "\n"
               << " * Mode            : " << (mode == BakedLightMode::FINAL ? "FINAL" : "FACTORED") << "\n"
               << " * Atlas           : " << atlas.w << "x" << atlas.h << " (" << (atlasBytes / 1024.0) << " KB, "
               << (sparse ? "sparse" : "dense") << ", " << f6(fillPercent) << "% fill)\n"
               << " * Blocks          : uni/ramp/p8/p16/p32 " << atlas.uniformTiles << "/" << atlas.rampTiles << "/"
               << atlas.pal8Tiles << "/" << atlas.pal16Tiles << "/" << atlas.pal32Tiles << "\n"
               << " * UV2             : " << (uv2Bytes / 1024.0) << " KB\n"
               << " * Probes          : " << (probes.valid ? probeDims + " (" + f6(probeBytes / 1024.0) + " KB)" : probeDims) << "\n"
               << " * Total           : " << totalBytes << " bytes (" << (totalBytes / 1024.0) << " KB)\n"
               << " */\n\n";

            os << "#pragma once\n\n"
               << "#include <Pip3D.hpp>\n\n"
               << "namespace pip3D\n{\n"
               << "    namespace Baked" << sceneName << "\n    {\n"
               << "        namespace detail\n        {\n";

            os << "        alignas(16) static constexpr uint16_t lm_pal[] = {\n";
            for (size_t i = 0; i < atlas.palettes.size(); i += 10)
            {
                os << "            ";
                for (size_t k = i; k < i + 10 && k < atlas.palettes.size(); ++k)
                    os << hex4(atlas.palettes[k]) << ", ";
                os << "\n";
            }
            os << "        };\n\n";

            os << "        alignas(16) static constexpr uint8_t lm_idx[] = {\n";
            for (size_t i = 0; i < atlas.indices.size(); i += 16)
            {
                os << "            ";
                for (size_t k = i; k < i + 16 && k < atlas.indices.size(); ++k)
                    os << hex2(atlas.indices[k]) << ", ";
                os << "\n";
            }
            os << "        };\n\n";

            if (sparse)
            {
                os << "        alignas(16) static constexpr uint16_t lm_uni[] = {\n";
                for (size_t i = 0; i < atlas.uniformCol.size(); i += 12)
                {
                    os << "            ";
                    for (size_t k = i; k < i + 12 && k < atlas.uniformCol.size(); ++k)
                        os << hex4(atlas.uniformCol[k]) << ", ";
                    os << "\n";
                }
                os << "        };\n\n";

                if (!atlas.palettes8.empty())
                {
                    os << "        alignas(16) static constexpr uint16_t lm_pal8[] = {\n";
                    for (size_t i = 0; i < atlas.palettes8.size(); i += 8)
                    {
                        os << "            ";
                        for (size_t k = i; k < i + 8 && k < atlas.palettes8.size(); ++k)
                            os << hex4(atlas.palettes8[k]) << ", ";
                        os << "\n";
                    }
                    os << "        };\n\n";

                    os << "        alignas(16) static constexpr uint8_t lm_idx8[] = {\n";
                    for (size_t i = 0; i < atlas.indices8.size(); i += 24)
                    {
                        os << "            ";
                        for (size_t k = i; k < i + 24 && k < atlas.indices8.size(); ++k)
                            os << hex2(atlas.indices8[k]) << ", ";
                        os << "\n";
                    }
                    os << "        };\n\n";
                }

                if (!atlas.ramp.empty())
                {
                    os << "        alignas(16) static constexpr uint16_t lm_ramp[] = {\n";
                    for (size_t i = 0; i < atlas.ramp.size(); i += 10)
                    {
                        os << "            ";
                        for (size_t k = i; k < i + 10 && k < atlas.ramp.size(); ++k)
                            os << hex4(atlas.ramp[k]) << ", ";
                        os << "\n";
                    }
                    os << "        };\n\n";

                    os << "        alignas(16) static constexpr uint8_t lm_ridx[] = {\n";
                    for (size_t i = 0; i < atlas.rampIdx.size(); i += 16)
                    {
                        os << "            ";
                        for (size_t k = i; k < i + 16 && k < atlas.rampIdx.size(); ++k)
                            os << hex2(atlas.rampIdx[k]) << ", ";
                        os << "\n";
                    }
                    os << "        };\n\n";
                }

                if (!atlas.palettes32.empty())
                {
                    os << "        alignas(16) static constexpr uint16_t lm_pal32[] = {\n";
                    for (size_t i = 0; i < atlas.palettes32.size(); i += 10)
                    {
                        os << "            ";
                        for (size_t k = i; k < i + 10 && k < atlas.palettes32.size(); ++k)
                            os << hex4(atlas.palettes32[k]) << ", ";
                        os << "\n";
                    }
                    os << "        };\n\n";

                    os << "        alignas(16) static constexpr uint8_t lm_idx32[] = {\n";
                    for (size_t i = 0; i < atlas.indices32.size(); i += 20)
                    {
                        os << "            ";
                        for (size_t k = i; k < i + 20 && k < atlas.indices32.size(); ++k)
                            os << hex2(atlas.indices32[k]) << ", ";
                        os << "\n";
                    }
                    os << "        };\n\n";
                }

                os << "        alignas(16) static constexpr uint16_t lm_dir[] = {\n";
                for (size_t i = 0; i < atlas.dir.size(); i += 12)
                {
                    os << "            ";
                    for (size_t k = i; k < i + 12 && k < atlas.dir.size(); ++k)
                        os << hex4(atlas.dir[k]) << ", ";
                    os << "\n";
                }
                os << "        };\n\n";
            }

            std::vector<const MeshCacheEntry *> meshOrder;
            meshOrder.reserve(meshCache.size());
            for (const auto &kv : meshCache)
                meshOrder.push_back(&kv.second);
            std::sort(meshOrder.begin(), meshOrder.end(),
                      [](const MeshCacheEntry *a, const MeshCacheEntry *b)
                      { return a->meshKey < b->meshKey; });
            for (const MeshCacheEntry *e : meshOrder)
            {
                os << "        alignas(4) static constexpr LMUVQuant lmuv_m" << e->meshKey << "[] = {\n";
                for (size_t i = 0; i + 2 < e->uv2Mesh.size(); i += 3)
                {
                    os << "            "
                       << "{ " << hex4(e->uv2Mesh[i].u) << ", " << hex4(e->uv2Mesh[i].v) << " }, "
                       << "{ " << hex4(e->uv2Mesh[i + 1].u) << ", " << hex4(e->uv2Mesh[i + 1].v) << " }, "
                       << "{ " << hex4(e->uv2Mesh[i + 2].u) << ", " << hex4(e->uv2Mesh[i + 2].v) << " },\n";
                }
                os << "        };\n\n";
            }

            if (probes.valid)
            {
                os << "        alignas(16) static constexpr uint16_t probe_data[] = {\n";
                for (size_t i = 0; i < probes.probes.size(); i += 10)
                {
                    os << "            ";
                    for (size_t k = i; k < i + 10 && k < probes.probes.size(); ++k)
                        os << hex4(probes.probes[k]) << ", ";
                    os << "\n";
                }
                os << "        };\n\n";
            }

            os << "        }\n\n";

            if (sparse)
            {
                const char *pal8Init = atlas.palettes8.empty() ? "nullptr, nullptr"
                                                               : "detail::lm_pal8, detail::lm_idx8";
                const char *rampInit = atlas.ramp.empty() ? "nullptr, nullptr"
                                                          : "detail::lm_ramp, detail::lm_ridx";
                const char *pal32Init = atlas.palettes32.empty() ? "nullptr, nullptr"
                                                                 : "detail::lm_pal32, detail::lm_idx32";
                os << "        static constexpr LMPaletteAtlas lm_atlas = {\n"
                   << "            detail::lm_pal, detail::lm_idx,\n"
                   << "            " << atlas.w << ", " << atlas.h << ",\n"
                   << "            " << atlas.wBlocks << ", " << atlas.hBlocks << ",\n"
                   << "            detail::lm_uni, detail::lm_dir,\n"
                   << "            " << pal8Init << ",\n"
                   << "            " << rampInit << ",\n"
                   << "            " << pal32Init << "\n"
                   << "        };\n\n";
            }
            else
            {
                os << "        static constexpr LMPaletteAtlas lm_atlas = {\n"
                   << "            detail::lm_pal, detail::lm_idx,\n"
                   << "            " << atlas.w << ", " << atlas.h << ",\n"
                   << "            " << atlas.wBlocks << ", " << atlas.hBlocks << "\n"
                   << "        };\n\n";
            }

            os << "        inline void apply(Renderer &r, std::vector<MeshInstance *> &insts)\n"
               << "        {\n"
               << "            r.setBakedLightMode(BakedLightMode::"
               << (mode == BakedLightMode::FINAL ? "FINAL" : "FACTORED") << ");\n";

            for (size_t i = 0; i < bakes.size(); ++i)
            {
                const InstanceBakeData &b = bakes[i];
                os << "            if (insts.size() > " << i << " && insts[" << i << "])\n"
                   << "            {\n"
                   << "                insts[" << i << "]->setLightmap(&lm_atlas, detail::lmuv_m"
                   << b.meshKey << ", " << b.ax << ", " << b.ay << ", "
                   << b.meshCache->rectW << ", " << b.meshCache->rectH << ");\n";
                os << "            }\n";
            }

            if (probes.valid)
            {
                os << "            static BakedProbeGrid s_probes;\n"
                   << "            s_probes.origin = Vector3(" << f6(probes.origin.x) << "f, "
                   << f6(probes.origin.y) << "f, " << f6(probes.origin.z) << "f);\n"
                   << "            s_probes.cellSize = " << f6(probes.cell) << "f;\n";
                if (probes.cellY > 0.1f && fabsf(probes.cellY - probes.cell) > 0.01f)
                    os << "            s_probes.cellSizeY = " << f6(probes.cellY) << "f;\n";
                os << "            s_probes.dimX = " << probes.dx << ";\n"
                   << "            s_probes.dimY = " << probes.dy << ";\n"
                   << "            s_probes.dimZ = " << probes.dz << ";\n"
                   << "            s_probes.probes = detail::probe_data;\n"
                   << "            s_probes.staticTint = Vector3(" << f6(probes.staticTint.x) << "f, " << f6(probes.staticTint.y) << "f, " << f6(probes.staticTint.z) << "f);\n"
                   << "            s_probes.staticScale = " << f6(probes.staticScale) << "f;\n"
                   << "            r.setBakedProbeGrid(s_probes);\n";
            }

            os << "        }\n"
               << "    }\n}\n";
            os.close();

            std::printf("\033[32m[+]\033[0m Baked %s: \033[1m%s\033[0m (%.1f KB)\n", sceneName, path.c_str(), totalBytes / 1024.0);
            std::printf("    Atlas %ux%u (%.1f KB, %.1f%% fill, %s)  Meshes %zu (%.1f KB UV2)  Probes %s\n",
                        atlas.w, atlas.h, atlasBytes / 1024.0, fillPercent, sparse ? "sparse" : "dense",
                        bakes.size(), uv2Bytes / 1024.0, probeDims.c_str());
        }

        void applyBakedToInstances(BakedLightMode mode,
                                   const std::vector<InstanceBakeData> &bakes,
                                   const std::map<Mesh *, MeshCacheEntry> &meshCache,
                                   const BakeAtlas &atlas,
                                   Renderer &r, const ProbeBake &probes)
        {
            (void)meshCache;
            r.setBakedLightMode(mode);

            static LMPaletteAtlas atlasPal;
            atlasPal.palettes = atlas.palettes.data();
            atlasPal.indices = atlas.indices.data();
            atlasPal.width = static_cast<uint16_t>(atlas.w);
            atlasPal.height = static_cast<uint16_t>(atlas.h);
            atlasPal.wBlocks = static_cast<uint16_t>(atlas.wBlocks);
            atlasPal.hBlocks = static_cast<uint16_t>(atlas.hBlocks);
            atlasPal.uniformCol = atlas.uniformCol.empty() ? nullptr : atlas.uniformCol.data();
            atlasPal.dir = atlas.dir.empty() ? nullptr : atlas.dir.data();
            atlasPal.palettes8 = atlas.palettes8.empty() ? nullptr : atlas.palettes8.data();
            atlasPal.indices8 = atlas.indices8.empty() ? nullptr : atlas.indices8.data();
            atlasPal.ramp = atlas.ramp.empty() ? nullptr : atlas.ramp.data();
            atlasPal.rampIdx = atlas.rampIdx.empty() ? nullptr : atlas.rampIdx.data();
            atlasPal.palettes32 = atlas.palettes32.empty() ? nullptr : atlas.palettes32.data();
            atlasPal.indices32 = atlas.indices32.empty() ? nullptr : atlas.indices32.data();

            for (size_t i = 0; i < bakes.size(); ++i)
            {
                if (!bakes[i].inst)
                    continue;
                bakes[i].inst->setLightmap(&atlasPal, bakes[i].meshCache->uv2Mesh.data(),
                                           static_cast<uint16_t>(bakes[i].ax),
                                           static_cast<uint16_t>(bakes[i].ay),
                                           static_cast<uint16_t>(bakes[i].meshCache->rectW),
                                           static_cast<uint16_t>(bakes[i].meshCache->rectH));
            }

            if (probes.valid)
            {
                static BakedProbeGrid g;
                g.origin = probes.origin;
                g.cellSize = probes.cell;
                g.cellSizeY = probes.cellY > 0.1f ? probes.cellY : probes.cell;
                g.dimX = static_cast<uint16_t>(probes.dx);
                g.dimY = static_cast<uint16_t>(probes.dy);
                g.dimZ = static_cast<uint16_t>(probes.dz);
                g.probes = probes.probes.data();
                g.staticTint = probes.staticTint;
                g.staticScale = probes.staticScale;
                r.setBakedProbeGrid(g);
            }
        }

        void writeBakePreviews(const char *outDir, const char *sceneName,
                               BakedLightMode mode,
                               const std::vector<InstanceBakeData> &bakes,
                               const BakeAtlas &atlas,
                               const ProbeBake &probes)
        {
            pip3d_fs::create_directories(outDir);

            char atlasPath[256];
            std::snprintf(atlasPath, sizeof(atlasPath), "%s/Atlas.png", outDir);
            previewAtlas(atlasPath, atlas, 1);

            char atlasRefPath[256];
            std::snprintf(atlasRefPath, sizeof(atlasRefPath), "%s/AtlasF32.png", outDir);
            previewAtlasReference(atlasRefPath, atlas, 1);

            char atlasSparsePath[256];
            std::snprintf(atlasSparsePath, sizeof(atlasSparsePath), "%s/Sparse.png", outDir);

            uint64_t usedTexels = 0;
            for (const InstanceBakeData &b : bakes)
                usedTexels += static_cast<uint64_t>(b.meshCache->rectW) * b.meshCache->rectH;
            const double fillPct = (atlas.w > 0 && atlas.h > 0)
                                       ? 100.0 * static_cast<double>(usedTexels) / (static_cast<double>(atlas.w) * atlas.h)
                                       : 0.0;
            char meta[320];
            std::snprintf(meta, sizeof(meta),
                          "scene=%s;mode=%s;w=%u;h=%u;fill=%.2f;avg=%.3f;max=%.0f;"
                          "uniform=%u;ramp=%u;pal8=%u;pal16=%u;pal32=%u",
                          sceneName, mode == BakedLightMode::FINAL ? "FINAL" : "FACTORED",
                          atlas.w, atlas.h, fillPct, atlas.avgLSB, atlas.maxLSB,
                          atlas.uniformTiles, atlas.rampTiles, atlas.pal8Tiles,
                          atlas.pal16Tiles, atlas.pal32Tiles);
            previewAtlasSparse(atlasSparsePath, atlas, meta, 1);

            for (size_t i = 0; i < bakes.size(); ++i)
            {
                char path[256];
                std::snprintf(path, sizeof(path), "%s/LM_%zu.png", outDir, i);

                previewLightmap(path, bakes[i].lm, bakes[i].meshCache->rectW,
                                bakes[i].meshCache->rectH, 1);

                std::snprintf(path, sizeof(path), "%s/Ch_%zu", outDir, i);
                previewChannels(path, bakes[i].den, bakes[i].meshCache->unwrap.texels,
                                bakes[i].meshCache->rectW, bakes[i].meshCache->rectH, 1);
            }

            if (probes.valid)
            {
                char path[256];
                const uint32_t groundSlice = std::max(1u, probes.dy / 6);
                std::snprintf(path, sizeof(path), "%s/Probes.png", outDir);
                previewProbeSlice(path, probes, groundSlice, 4);

                if (probes.dy > 3)
                {
                    const uint32_t midSlice = probes.dy / 2;
                    std::snprintf(path, sizeof(path), "%s/Probes_mid.png", outDir);
                    previewProbeSlice(path, probes, midSlice, 4);
                    const uint32_t topSlice = probes.dy - 1;
                    std::snprintf(path, sizeof(path), "%s/Probes_top.png", outDir);
                    previewProbeSlice(path, probes, topSlice, 4);
                }
            }
        }
    }
}
