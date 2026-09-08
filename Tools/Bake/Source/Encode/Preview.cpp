#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>
#include <vector>

#include "Preview.hpp"
#include <PipCore/Platforms/Desktop/Runtime.hpp>
#include "Core/Progress.hpp"

namespace pip3D
{
    namespace Bake
    {
        namespace
        {
            const uint32_t *crcTable()
            {
                static uint32_t table[256];
                static std::once_flag flag;
                std::call_once(flag, []
                               {
                    for (uint32_t i = 0; i < 256; ++i) {
                        uint32_t c = i;
                        for (int k = 0; k < 8; ++k) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
                        table[i] = c;
                    } });
                return table;
            }
            uint32_t crc32Update(uint32_t crc, const uint8_t *buf, size_t len)
            {
                const uint32_t *table = crcTable();
                crc ^= 0xFFFFFFFFu;
                for (size_t i = 0; i < len; ++i)
                    crc = table[(crc ^ buf[i]) & 0xFFu] ^ (crc >> 8);
                return crc ^ 0xFFFFFFFFu;
            }

            uint32_t adler32Calc(const uint8_t *data, size_t len)
            {
                uint32_t a = 1, b = 0;
                for (size_t i = 0; i < len; ++i)
                {
                    a = (a + data[i]) % 65521u;
                    b = (b + a) % 65521u;
                }
                return (b << 16) | a;
            }

            void writeBigU32(std::vector<uint8_t> &out, uint32_t v)
            {
                out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
                out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
                out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
                out.push_back(static_cast<uint8_t>(v & 0xFF));
            }

            void writeChunk(std::ofstream &os, const char type[4], const uint8_t *data, uint32_t len)
            {
                uint8_t lenBuf[4] = {
                    static_cast<uint8_t>((len >> 24) & 0xFF),
                    static_cast<uint8_t>((len >> 16) & 0xFF),
                    static_cast<uint8_t>((len >> 8) & 0xFF),
                    static_cast<uint8_t>(len & 0xFF)};
                os.write(reinterpret_cast<const char *>(lenBuf), 4);

                uint32_t crc = crc32Update(0, reinterpret_cast<const uint8_t *>(type), 4);
                if (data && len > 0)
                    crc = crc32Update(crc, data, len);

                os.write(type, 4);
                if (data && len > 0)
                    os.write(reinterpret_cast<const char *>(data), len);

                uint8_t crcBuf[4] = {
                    static_cast<uint8_t>((crc >> 24) & 0xFF),
                    static_cast<uint8_t>((crc >> 16) & 0xFF),
                    static_cast<uint8_t>((crc >> 8) & 0xFF),
                    static_cast<uint8_t>(crc & 0xFF)};
                os.write(reinterpret_cast<const char *>(crcBuf), 4);
            }

            constexpr uint32_t kFilterNone = 0, kFilterSub = 1, kFilterUp = 2, kFilterAvg = 3, kFilterPaeth = 4;

            inline uint8_t paethPredictor(int32_t a, int32_t b, int32_t c) noexcept
            {
                const int32_t p = a + b - c;
                const int32_t pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
                return (pa <= pb && pa <= pc) ? static_cast<uint8_t>(a)
                                              : (pb <= pc ? static_cast<uint8_t>(b) : static_cast<uint8_t>(c));
            }

            uint32_t filterRow(const uint8_t *cur, const uint8_t *prev, uint32_t bpp, uint32_t bppCount,
                               uint32_t filter, uint8_t *out) noexcept
            {
                uint32_t cost = 0;
                for (uint32_t i = 0; i < bpp; ++i)
                {
                    const int32_t x = cur[i];
                    const int32_t a = (i >= bppCount) ? cur[i - bppCount] : 0;
                    const int32_t b = prev[i];
                    const int32_t c = (i >= bppCount) ? prev[i - bppCount] : 0;
                    int32_t v = 0;
                    switch (filter)
                    {
                    case kFilterSub:
                        v = x - a;
                        break;
                    case kFilterUp:
                        v = x - b;
                        break;
                    case kFilterAvg:
                        v = x - ((a + b) >> 1);
                        break;
                    case kFilterPaeth:
                        v = x - paethPredictor(a, b, c);
                        break;
                    default:
                        v = x;
                        break;
                    }
                    const uint8_t u = static_cast<uint8_t>(v);
                    out[i] = u;
                    cost += (u < 128) ? u : (256u - u);
                }
                return cost;
            }

            class BitWriter
            {
            public:
                explicit BitWriter(std::vector<uint8_t> &out) : m_out(out) {}

                void put(uint32_t value, uint32_t bits) noexcept
                {
                    m_acc |= static_cast<uint64_t>(value & ((1u << bits) - 1u)) << m_bits;
                    m_bits += bits;
                    while (m_bits >= 8)
                    {
                        m_out.push_back(static_cast<uint8_t>(m_acc & 0xFFu));
                        m_acc >>= 8;
                        m_bits -= 8;
                    }
                }

                void putCode(uint32_t code, uint32_t len) noexcept
                {
                    uint32_t rev = 0;
                    for (uint32_t i = 0; i < len; ++i)
                        rev |= ((code >> i) & 1u) << (len - 1u - i);
                    put(rev, len);
                }

                void flush() noexcept
                {
                    if (m_bits > 0)
                    {
                        m_out.push_back(static_cast<uint8_t>(m_acc & 0xFFu));
                        m_acc = 0;
                        m_bits = 0;
                    }
                }

            private:
                std::vector<uint8_t> &m_out;
                uint64_t m_acc = 0;
                uint32_t m_bits = 0;
            };

            inline uint32_t litLenCode(uint32_t sym, uint32_t &len) noexcept
            {
                if (sym < 144)
                {
                    len = 8;
                    return 0x30u + sym;
                }
                if (sym < 256)
                {
                    len = 9;
                    return 0x190u + (sym - 144);
                }
                if (sym < 280)
                {
                    len = 7;
                    return sym - 256;
                }
                len = 8;
                return 0xC0u + (sym - 280);
            }

            constexpr uint32_t kLengthBase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
                                                  35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
            constexpr uint8_t kLengthExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
                                                  3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
            constexpr uint32_t kDistBase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
                                                257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
                                                8193, 12289, 16385, 24577};
            constexpr uint8_t kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
                                                7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

            uint32_t lengthSymbol(uint32_t matchLen) noexcept
            {
                uint32_t s = 28;
                while (kLengthBase[s] > matchLen)
                    --s;
                return 257u + s;
            }

            uint32_t distSymbol(uint32_t dist) noexcept
            {
                uint32_t s = 29;
                while (kDistBase[s] > dist)
                    --s;
                return s;
            }

            constexpr uint32_t kHashBits = 15;
            constexpr uint32_t kHashSize = 1u << kHashBits;
            constexpr uint32_t kWindowSize = 32768;
            constexpr uint32_t kMaxChain = 192;
            constexpr uint32_t kNiceLength = 128;
            constexpr uint32_t kMinMatch = 3;
            constexpr uint32_t kMaxMatch = 258;

            inline uint32_t hash3(const uint8_t *p) noexcept
            {
                return ((static_cast<uint32_t>(p[0]) << 10) ^ (static_cast<uint32_t>(p[1]) << 5) ^
                        static_cast<uint32_t>(p[2])) &
                       (kHashSize - 1u);
            }

            void deflateFixed(const uint8_t *data, size_t n, std::vector<uint8_t> &out)
            {
                std::vector<uint32_t> head(kHashSize, 0);
                std::vector<uint32_t> prev(kWindowSize, 0);

                BitWriter bw(out);
                bw.put(1u, 1u);
                bw.put(1u, 2u);

                auto emitSym = [&bw](uint32_t sym)
                {
                    uint32_t len = 0;
                    const uint32_t code = litLenCode(sym, len);
                    bw.putCode(code, len);
                };
                auto emitMatch = [&bw](uint32_t length, uint32_t dist)
                {
                    const uint32_t ls = lengthSymbol(length);
                    const uint32_t li = ls - 257u;
                    uint32_t len = 0;
                    const uint32_t code = litLenCode(ls, len);
                    bw.putCode(code, len);
                    if (kLengthExtra[li])
                        bw.put(length - kLengthBase[li], kLengthExtra[li]);
                    const uint32_t ds = distSymbol(dist);
                    bw.putCode(ds, 5);
                    if (kDistExtra[ds])
                        bw.put(dist - kDistBase[ds], kDistExtra[ds]);
                };

                size_t pos = 0;
                while (pos < n)
                {
                    uint32_t bestLen = 0, bestDist = 0;
                    if (pos + kMinMatch <= n)
                    {
                        const uint32_t h = hash3(data + pos);
                        uint32_t cur = head[h];
                        const size_t maxLen = std::min<size_t>(kMaxMatch, n - pos);
                        for (uint32_t chain = kMaxChain; cur != 0 && chain > 0; --chain)
                        {
                            const size_t cp = cur - 1;
                            if (pos - cp >= kWindowSize)
                                break;
                            if (bestLen < maxLen && data[cp + bestLen] == data[pos + bestLen])
                            {
                                uint32_t l = 0;
                                while (l < maxLen && data[cp + l] == data[pos + l])
                                    ++l;
                                if (l > bestLen)
                                {
                                    bestLen = l;
                                    bestDist = static_cast<uint32_t>(pos - cp);
                                    if (l >= kNiceLength)
                                        break;
                                }
                            }
                            cur = prev[cp & (kWindowSize - 1u)];
                        }
                    }

                    if (bestLen >= kMinMatch)
                    {
                        emitMatch(bestLen, bestDist);
                        const size_t end = std::min(pos + bestLen, n);
                        for (size_t ip = pos; ip < end; ++ip)
                        {
                            if (ip + kMinMatch <= n)
                            {
                                const uint32_t h = hash3(data + ip);
                                prev[ip & (kWindowSize - 1u)] = head[h];
                                head[h] = static_cast<uint32_t>(ip) + 1;
                            }
                        }
                        pos += bestLen;
                    }
                    else
                    {
                        emitSym(data[pos]);
                        if (pos + kMinMatch <= n)
                        {
                            const uint32_t h = hash3(data + pos);
                            prev[pos & (kWindowSize - 1u)] = head[h];
                            head[h] = static_cast<uint32_t>(pos) + 1;
                        }
                        ++pos;
                    }
                }
                emitSym(256);
                bw.flush();
            }
        }

        bool writePNGDepth(const char *path, uint32_t w, uint32_t h, const uint8_t *rgb,
                           uint32_t bytesPerSample, const char *metadata = nullptr)
        {
            std::ofstream os(path, std::ios::binary);
            if (!os)
                return false;

            const uint8_t pngSig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
            os.write(reinterpret_cast<const char *>(pngSig), 8);

            std::vector<uint8_t> ihdr;
            writeBigU32(ihdr, w);
            writeBigU32(ihdr, h);
            ihdr.push_back(static_cast<uint8_t>(bytesPerSample * 8));
            ihdr.push_back(2);
            ihdr.push_back(0);
            ihdr.push_back(0);
            ihdr.push_back(0);
            writeChunk(os, "IHDR", ihdr.data(), static_cast<uint32_t>(ihdr.size()));

            if (metadata && *metadata)
            {
                std::vector<uint8_t> text;
                const char *keyword = "Pip3D";
                text.insert(text.end(), keyword, keyword + 5);
                text.push_back(0);
                text.insert(text.end(), metadata, metadata + std::strlen(metadata));
                writeChunk(os, "tEXt", text.data(), static_cast<uint32_t>(text.size()));
            }

            const uint32_t stride = w * 3 * bytesPerSample;
            const size_t rawScanlineBytes = static_cast<size_t>(stride) + 1;
            const size_t rawDataSize = rawScanlineBytes * h;
            std::vector<uint8_t> rawData(rawDataSize);
            const std::vector<uint8_t> zeroRow(stride, 0);
            std::vector<uint8_t> curRow(stride), bestRow(stride);

            for (uint32_t y = 0; y < h; ++y)
            {
                const uint8_t *src = &rgb[static_cast<size_t>(y) * stride];
                const uint8_t *prevRow = (y > 0) ? &rgb[static_cast<size_t>(y - 1) * stride] : zeroRow.data();

                uint32_t bestFilter = kFilterNone;
                uint32_t bestCost = 0xFFFFFFFFu;
                for (uint32_t f = 0; f <= 4; ++f)
                {
                    const uint32_t cost = filterRow(src, prevRow, stride, 3 * bytesPerSample, f, curRow.data());
                    if (cost < bestCost)
                    {
                        bestCost = cost;
                        bestFilter = f;
                        std::swap(curRow, bestRow);
                    }
                }
                const size_t rowStart = static_cast<size_t>(y) * rawScanlineBytes;
                rawData[rowStart] = static_cast<uint8_t>(bestFilter);
                std::memcpy(&rawData[rowStart + 1], bestRow.data(), stride);
            }

            std::vector<uint8_t> idat;
            idat.push_back(0x78);
            idat.push_back(0x9C);
            deflateFixed(rawData.data(), rawData.size(), idat);

            const uint32_t adler = adler32Calc(rawData.data(), rawData.size());
            writeBigU32(idat, adler);
            writeChunk(os, "IDAT", idat.data(), static_cast<uint32_t>(idat.size()));
            writeChunk(os, "IEND", nullptr, 0);
            return true;
        }

        bool writePNG(const char *path, uint32_t w, uint32_t h, const uint8_t *rgbTopDown)
        {
            return writePNGDepth(path, w, h, rgbTopDown, 1);
        }

        constexpr uint8_t kClassCol[5][3] = {
            {90, 230, 90},
            {255, 220, 60},
            {70, 200, 255},
            {200, 110, 255},
            {255, 120, 60},
        };
        constexpr uint8_t kErrCol[3] = {255, 0, 0};

        void previewAtlas(const char *path, const BakeAtlas &atlas, uint32_t scale)
        {
            if (atlas.w == 0 || atlas.h == 0 || atlas.data.empty())
                return;
            scale = std::max(1u, scale);
            const bool encoded = atlas.encoded();
            const uint32_t outW = atlas.w * scale;
            const uint32_t outH = atlas.h * scale;
            std::vector<uint8_t> rgb(static_cast<size_t>(outW) * outH * 3);

            for (uint32_t y = 0; y < outH; ++y)
            {
                for (uint32_t x = 0; x < outW; ++x)
                {
                    const uint16_t c = encoded ? decodeAtlasTexel(atlas, x / scale, y / scale)
                                               : atlas.data[(y / scale) * atlas.w + (x / scale)];
                    uint8_t *px = &rgb[(static_cast<size_t>(y) * outW + x) * 3];
                    const uint32_t r5 = (c >> 11) & 0x1F;
                    const uint32_t g6 = (c >> 5) & 0x3F;
                    const uint32_t b5 = c & 0x1F;
                    px[0] = static_cast<uint8_t>((r5 << 3) | (r5 >> 2));
                    px[1] = static_cast<uint8_t>((g6 << 2) | (g6 >> 4));
                    px[2] = static_cast<uint8_t>((b5 << 3) | (b5 >> 2));
                }
            }
            writePNG(path, outW, outH, rgb.data());
        }

        void previewAtlasReference(const char *path, const BakeAtlas &atlas, uint32_t scale)
        {
            const uint32_t w = atlas.w, h = atlas.h;
            if (w == 0 || h == 0 || atlas.f32.size() != static_cast<size_t>(w) * h * 3)
                return;
            scale = std::max(1u, scale);
            const uint32_t outW = w * scale;
            const uint32_t outH = h * scale;
            std::vector<uint8_t> rgb(static_cast<size_t>(outW) * outH * 6);

            for (uint32_t y = 0; y < outH; ++y)
            {
                for (uint32_t x = 0; x < outW; ++x)
                {
                    const float *src = &atlas.f32[(static_cast<size_t>(y / scale) * w + (x / scale)) * 3];
                    uint8_t *px = &rgb[(static_cast<size_t>(y) * outW + x) * 6];
                    for (int c = 0; c < 3; ++c)
                    {
                        float v = src[c];
                        v = (v < 0.0f) ? 0.0f : (v > 1.0f ? 1.0f : v);
                        const uint16_t s = static_cast<uint16_t>(v * 65535.0f + 0.5f);
                        px[c * 2 + 0] = static_cast<uint8_t>(s >> 8);
                        px[c * 2 + 1] = static_cast<uint8_t>(s & 0xFF);
                    }
                }
            }
            writePNGDepth(path, outW, outH, rgb.data(), 2);
        }

        uint32_t previewAtlasSparse(const char *path, const BakeAtlas &atlas,
                                    char *metadata, uint32_t scale)
        {
            if (!atlas.encoded() || atlas.w == 0 || atlas.h == 0)
                return 0;
            scale = std::max(1u, scale);
            const uint32_t outW = atlas.w * scale;
            const uint32_t outH = atlas.h * scale;
            std::vector<uint8_t> rgb(static_cast<size_t>(outW) * outH * 3);

            constexpr float kClassMix = 0.45f;
            uint8_t classMix[5][3];
            for (int c = 0; c < 5; ++c)
                for (int k = 0; k < 3; ++k)
                    classMix[c][k] = static_cast<uint8_t>(kClassCol[c][k] * kClassMix + 0.5f);
            constexpr float kOrigMix = 1.0f - kClassMix;

            const bool haveF32 = atlas.f32.size() == static_cast<size_t>(atlas.w) * atlas.h * 3;
            uint32_t errPixels = 0;

            for (uint32_t y = 0; y < atlas.h; ++y)
            {
                for (uint32_t x = 0; x < atlas.w; ++x)
                {
                    const uint32_t block = (y >> 3) * atlas.wBlocks + (x >> 3);
                    const uint16_t d = atlas.dir[block];
                    uint32_t cls;
                    if ((d & 0xC000u) == 0xC000u)
                        cls = 0;
                    else if (d & 0x8000u)
                        cls = 1;
                    else if (d & 0x4000u)
                        cls = 2;
                    else if (d & 0x2000u)
                        cls = 3;
                    else
                        cls = 4;

                    const uint16_t c = decodeAtlasTexel(atlas, x, y);
                    const uint32_t r5 = (c >> 11) & 0x1F;
                    const uint32_t g6 = (c >> 5) & 0x3F;
                    const uint32_t b5 = c & 0x1F;

                    bool errTexel = false;
                    if (haveF32)
                    {
                        const float *f = &atlas.f32[(static_cast<size_t>(y) * atlas.w + x) * 3];
                        auto toLvl = [](float v, float levels)
                        {
                            v = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
                            return static_cast<uint32_t>(v * levels + 0.5f);
                        };
                        const uint32_t sr = toLvl(f[0], 31.0f), sg = toLvl(f[1], 63.0f), sb = toLvl(f[2], 31.0f);
                        const uint32_t dr = (sr > r5) ? sr - r5 : r5 - sr;
                        const uint32_t dg = (sg > g6) ? sg - g6 : g6 - sg;
                        const uint32_t db = (sb > b5) ? sb - b5 : b5 - sb;
                        errTexel = std::max(dr, std::max(dg, db)) >= 2;
                    }

                    uint8_t col[3];
                    if (errTexel)
                    {
                        col[0] = kErrCol[0];
                        col[1] = kErrCol[1];
                        col[2] = kErrCol[2];
                        ++errPixels;
                    }
                    else
                    {

                        const uint32_t r8 = (r5 << 3) | (r5 >> 2);
                        const uint32_t g8 = (g6 << 2) | (g6 >> 4);
                        const uint32_t b8 = (b5 << 3) | (b5 >> 2);
                        col[0] = static_cast<uint8_t>(r8 * kOrigMix + classMix[cls][0] + 0.5f);
                        col[1] = static_cast<uint8_t>(g8 * kOrigMix + classMix[cls][1] + 0.5f);
                        col[2] = static_cast<uint8_t>(b8 * kOrigMix + classMix[cls][2] + 0.5f);
                    }

                    for (uint32_t sy = 0; sy < scale; ++sy)
                    {
                        uint8_t *row = &rgb[(static_cast<size_t>(y * scale + sy) * outW + x * scale) * 3];
                        for (uint32_t sx = 0; sx < scale; ++sx)
                        {
                            row[sx * 3 + 0] = col[0];
                            row[sx * 3 + 1] = col[1];
                            row[sx * 3 + 2] = col[2];
                        }
                    }
                }
            }
            if (metadata)
            {
                const size_t len = std::strlen(metadata);
                std::snprintf(metadata + len, 32, ";drift=%u", errPixels);
            }
            writePNGDepth(path, outW, outH, rgb.data(), 1, metadata);
            if (errPixels)
                progressBar().logf("\033[36m[Pip3D]\033[0m Sparse map: %u texels with 2+ LSB drift highlighted in red", errPixels);
            return errPixels;
        }

        void previewLightmap(const char *path, const std::vector<uint16_t> &lm,
                             uint32_t w, uint32_t h, uint32_t scale)
        {
            if (lm.empty() || w == 0 || h == 0)
                return;

            const uint32_t outW = w * scale;
            const uint32_t outH = h * scale;
            std::vector<uint8_t> rgb(static_cast<size_t>(outW) * outH * 3);

            for (uint32_t y = 0; y < outH; ++y)
            {
                for (uint32_t x = 0; x < outW; ++x)
                {
                    const uint16_t c = lm[static_cast<size_t>(y / scale) * w + (x / scale)];
                    uint8_t *px = &rgb[(static_cast<size_t>(y) * outW + x) * 3];
                    const uint32_t r5 = (c >> 11) & 0x1F;
                    const uint32_t g6 = (c >> 5) & 0x3F;
                    const uint32_t b5 = c & 0x1F;
                    px[0] = static_cast<uint8_t>((r5 << 3) | (r5 >> 2));
                    px[1] = static_cast<uint8_t>((g6 << 2) | (g6 >> 4));
                    px[2] = static_cast<uint8_t>((b5 << 3) | (b5 >> 2));
                }
            }
            writePNG(path, outW, outH, rgb.data());
        }

        void previewChannels(const char *pathPrefix, const std::vector<float> &den,
                             const std::vector<TexelHit> &texels,
                             uint32_t w, uint32_t h, uint32_t scale)
        {
            if (den.empty() || w == 0 || h == 0)
                return;
            const char *names[3] = {"Sun", "Sky", "Stat"};
            const uint32_t outW = w * scale;
            const uint32_t outH = h * scale;

            for (int c = 0; c < 3; ++c)
            {
                std::vector<float> vals;
                vals.reserve(static_cast<size_t>(w) * h);
                for (size_t i = 0; i < texels.size(); ++i)
                {
                    if (texels[i].face != 0xFFFFFFFFu)
                        vals.push_back(den[i * 3 + c]);
                }
                float mx = 1e-4f;
                if (!vals.empty())
                {

                    const size_t k = (vals.size() * 99) / 100;
                    std::nth_element(vals.begin(), vals.begin() + static_cast<long>(k), vals.end());
                    mx = std::fmax(1e-4f, vals[k]);
                }

                std::vector<uint8_t> rgb(static_cast<size_t>(outW) * outH * 3);
                for (uint32_t y = 0; y < outH; ++y)
                {
                    for (uint32_t x = 0; x < outW; ++x)
                    {
                        const size_t ti = static_cast<size_t>(y / scale) * w + (x / scale);
                        const float v = std::fmin(1.0f, den[ti * 3 + c] / mx);
                        uint8_t *px = &rgb[(static_cast<size_t>(y) * outW + x) * 3];
                        const uint8_t g = static_cast<uint8_t>(v * 255.0f + 0.5f);
                        px[0] = px[1] = px[2] = g;
                    }
                }
                char path[256];
                std::snprintf(path, sizeof(path), "%s_%s.png", pathPrefix, names[c]);
                writePNG(path, outW, outH, rgb.data());
            }
        }

        void previewProbeSlice(const char *path, const ProbeBake &probes,
                               uint32_t ySlice, uint32_t scale)
        {
            if (!probes.valid)
                return;
            const uint32_t cell = std::max(1u, scale);
            const uint32_t gap = 1;
            const uint32_t w = probes.dx * cell + (probes.dx + 1) * gap;
            const uint32_t h = probes.dz * cell + (probes.dz + 1) * gap;
            std::vector<uint8_t> rgb(static_cast<size_t>(w) * h * 3, 22);

            for (uint32_t z = 0; z < probes.dz; ++z)
            {
                for (uint32_t x = 0; x < probes.dx; ++x)
                {
                    const uint16_t c = probes.probes[(static_cast<size_t>(z) * probes.dy + ySlice) * probes.dx + x];
                    const uint32_t r5 = (c >> 11) & 0x1F;
                    const uint32_t g6 = (c >> 5) & 0x3F;
                    const uint32_t b5 = c & 0x1F;
                    const uint8_t r = static_cast<uint8_t>((r5 << 3) | (r5 >> 2));
                    const uint8_t g = static_cast<uint8_t>((g6 << 2) | (g6 >> 4));
                    const uint8_t b = static_cast<uint8_t>((b5 << 3) | (b5 >> 2));
                    const uint32_t baseX = gap + x * (cell + gap);
                    const uint32_t baseY = gap + z * (cell + gap);
                    for (uint32_t sy = 0; sy < cell; ++sy)
                        for (uint32_t sx = 0; sx < cell; ++sx)
                        {
                            uint8_t *dst = &rgb[(static_cast<size_t>(baseY + sy) * w + baseX + sx) * 3];
                            dst[0] = r;
                            dst[1] = g;
                            dst[2] = b;
                        }
                }
            }
            writePNG(path, w, h, rgb.data());
        }

        void previewFrame565(const char *path, const uint16_t *fb, uint32_t w, uint32_t h)
        {
            if (!fb)
                return;
            std::vector<uint8_t> rgb(static_cast<size_t>(w) * h * 3);
            for (uint32_t i = 0; i < w * h; ++i)
            {
                const uint16_t c = (fb[i] << 8) | (fb[i] >> 8);
                const uint32_t r5 = (c >> 11) & 0x1F;
                const uint32_t g6 = (c >> 5) & 0x3F;
                const uint32_t b5 = c & 0x1F;
                rgb[i * 3 + 0] = static_cast<uint8_t>((r5 << 3) | (r5 >> 2));
                rgb[i * 3 + 1] = static_cast<uint8_t>((g6 << 2) | (g6 >> 4));
                rgb[i * 3 + 2] = static_cast<uint8_t>((b5 << 3) | (b5 >> 2));
            }
            writePNG(path, w, h, rgb.data());
        }

        void renderAndDumpFrame(const char *path, Renderer &r,
                                std::vector<MeshInstance *> &instances)
        {
            r.updateCameraView();
            if (std::getenv("PIP3D_BAKE_ONLYFLOOR") && instances.size() > 1)
                instances.resize(1);
            std::vector<BandCullItem> items;
            r.buildBandCullList(instances, items);
            for (int band = 0; band < SCREEN_BAND_COUNT; ++band)
            {
                r.beginFrameBand(band);
                r.fillSkyGradient();
                r.drawBandInstances(band, items.data(), items.size());
                r.drawSky();
                r.endFrameBand(band);
            }
            auto &rt = pipcore::desktop::Runtime::instance();
            previewFrame565(path, rt.frame565().data(), rt.width(), rt.height());
        }
    }
}