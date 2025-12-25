#ifndef RASTERIZER_H
#define RASTERIZER_H

#include "../../Core/Core.h"
#include "../Display/ZBuffer.h"
#include "Shading.h"
#include <algorithm>

namespace pip3D
{

    class Rasterizer
    {
    public:
        static void fillShadowTriangle(int16_t x0, int16_t y0, float z0,
                                       int16_t x1, int16_t y1, float z1,
                                       int16_t x2, int16_t y2, float z2,
                                       uint16_t shadowColor,
                                       uint8_t alpha,
                                       uint16_t *frameBuffer,
                                       ZBuffer<320, 240> *zBuffer,
                                       const DisplayConfig &config,
                                       bool softEdges = true)
        {
            const int16_t width = config.width;
            const int16_t height = config.height;

            if (!frameBuffer || !zBuffer)
                return;

            if (y0 > y1)
            {
                std::swap(x0, x1);
                std::swap(y0, y1);
                std::swap(z0, z1);
            }
            if (y1 > y2)
            {
                std::swap(x1, x2);
                std::swap(y1, y2);
                std::swap(z1, z2);
            }
            if (y0 > y1)
            {
                std::swap(x0, x1);
                std::swap(y0, y1);
                std::swap(z0, z1);
            }

            if (y0 == y2)
                return;
            if (x0 == x1 && x1 == x2)
                return;

            const uint16_t sr = (shadowColor >> 11) & 0x1F;
            const uint16_t sg = (shadowColor >> 5) & 0x3F;
            const uint16_t sb = shadowColor & 0x1F;

            int16_t dy1 = y1 - y0;
            int16_t dy2 = y2 - y0;

            if (dy1 == 0 && dy2 == 0)
                return;

            float dax_step = 0.0f;
            float dbx_step = 0.0f;
            float daz_step = 0.0f;
            float dbz_step = 0.0f;

            if (dy1)
            {
                float invDy1 = 1.0f / dy1;
                dax_step = (float)(x1 - x0) * invDy1;
                daz_step = (z1 - z0) * invDy1;
            }
            if (dy2)
            {
                float invDy2 = 1.0f / dy2;
                dbx_step = (float)(x2 - x0) * invDy2;
                dbz_step = (z2 - z0) * invDy2;
            }

            float ax = x0, bx = x0;
            float az = z0, bz = z0;

            const float depthScale = 32767.0f;

            if (dy1)
            {
                for (int16_t y = y0; y <= y1; ++y)
                {
                    if (unlikely(y < 0 || y >= height))
                    {
                        ax += dax_step;
                        az += daz_step;
                        bx += dbx_step;
                        bz += dbz_step;
                        continue;
                    }

                    int16_t xa = (int16_t)(ax + 0.5f);
                    int16_t xb = (int16_t)(bx + 0.5f);
                    float za = az;
                    float zb = bz;
                    if (xa > xb)
                    {
                        std::swap(xa, xb);
                        std::swap(za, zb);
                    }

                    int16_t x_start_src = (xa < 0) ? 0 : xa;
                    int16_t x_end_src = (xb >= width) ? width - 1 : xb;

                    if (x_start_src <= x_end_src && x_start_src < width && x_end_src >= 0)
                    {
                        int16_t x_start = x_start_src;
                        int16_t x_end = x_end_src;
                        if (x_start > 0)
                            --x_start;
                        if (x_end < width - 1)
                            ++x_end;

                        uint8_t edgeAlpha = alpha;
                        if (softEdges)
                        {
                            float edgeDist = 1.0f;
                            if (y == y0 || y == y2)
                                edgeDist = 0.5f;
                            if (x_start_src == xa || x_end_src == xb)
                                edgeDist *= 0.7f;
                            edgeAlpha = (uint8_t)(alpha * edgeDist);
                        }

                        const uint16_t invEdgeAlpha = 255 - edgeAlpha;

                        float dx = (float)(xb - xa);
                        float invDx = dx != 0.0f ? 1.0f / dx : 0.0f;
                        float z_step = (zb - za) * invDx;
                        float z = za + z_step * (float)(x_start - xa);

                        size_t index = (size_t)y * width + x_start;
                        for (int16_t x = x_start; x <= x_end; ++x, ++index)
                        {
                            if (!zBuffer->hasGeometry(x, y) || zBuffer->hasShadow(x, y))
                            {
                                z += z_step;
                                continue;
                            }

                            int16_t storedDepth = zBuffer->getRawDepth(x, y);
                            int16_t shadowDepth = (int16_t)(z * depthScale);
                            if (shadowDepth > storedDepth)
                            {
                                z += z_step;
                                continue;
                            }

                            uint16_t bgColor = frameBuffer[index];
                            uint16_t br = (bgColor >> 11) & 0x1F;
                            uint16_t bg = (bgColor >> 5) & 0x3F;
                            uint16_t bb = bgColor & 0x1F;

                            uint16_t r = (br * invEdgeAlpha + sr * edgeAlpha) >> 8;
                            uint16_t g = (bg * invEdgeAlpha + sg * edgeAlpha) >> 8;
                            uint16_t b = (bb * invEdgeAlpha + sb * edgeAlpha) >> 8;

                            frameBuffer[index] = (r << 11) | (g << 5) | b;
                            zBuffer->markShadow(x, y);

                            z += z_step;
                        }
                    }

                    ax += dax_step;
                    az += daz_step;
                    bx += dbx_step;
                    bz += dbz_step;
                }
            }

            dy1 = y2 - y1;
            if (dy1)
            {
                float invDy1 = 1.0f / dy1;
                dax_step = (float)(x2 - x1) * invDy1;
                daz_step = (z2 - z1) * invDy1;
                ax = x1;
                az = z1;

                for (int16_t y = y1 + 1; y <= y2; ++y)
                {
                    if (y < 0 || y >= height)
                    {
                        ax += dax_step;
                        az += daz_step;
                        bx += dbx_step;
                        bz += dbz_step;
                        continue;
                    }

                    int16_t xa = (int16_t)(ax + 0.5f);
                    int16_t xb = (int16_t)(bx + 0.5f);
                    float za = az;
                    float zb = bz;
                    if (xa > xb)
                    {
                        std::swap(xa, xb);
                        std::swap(za, zb);
                    }

                    int16_t x_start_src = (xa < 0) ? 0 : xa;
                    int16_t x_end_src = (xb >= width) ? width - 1 : xb;

                    if (x_start_src <= x_end_src && x_start_src < width && x_end_src >= 0)
                    {
                        int16_t x_start = x_start_src;
                        int16_t x_end = x_end_src;
                        if (x_start > 0)
                            --x_start;
                        if (x_end < width - 1)
                            ++x_end;

                        uint8_t edgeAlpha = alpha;
                        if (softEdges)
                        {
                            float edgeDist = 1.0f;
                            if (y == y0 || y == y2)
                                edgeDist = 0.5f;
                            if (x_start_src == xa || x_end_src == xb)
                                edgeDist *= 0.7f;
                            edgeAlpha = (uint8_t)(alpha * edgeDist);
                        }

                        const uint16_t invEdgeAlpha = 255 - edgeAlpha;

                        float dx = (float)(xb - xa);
                        float invDx = dx != 0.0f ? 1.0f / dx : 0.0f;
                        float z_step = (zb - za) * invDx;
                        float z = za + z_step * (float)(x_start - xa);

                        size_t index = (size_t)y * width + x_start;
                        for (int16_t x = x_start; x <= x_end; ++x, ++index)
                        {
                            if (!zBuffer->hasGeometry(x, y) || zBuffer->hasShadow(x, y))
                            {
                                z += z_step;
                                continue;
                            }

                            int16_t storedDepth = zBuffer->getRawDepth(x, y);
                            int16_t shadowDepth = (int16_t)(z * depthScale);
                            if (shadowDepth > storedDepth)
                            {
                                z += z_step;
                                continue;
                            }

                            uint16_t bgColor = frameBuffer[index];
                            uint16_t br = (bgColor >> 11) & 0x1F;
                            uint16_t bg = (bgColor >> 5) & 0x3F;
                            uint16_t bb = bgColor & 0x1F;

                            uint16_t r = (br * invEdgeAlpha + sr * edgeAlpha) >> 8;
                            uint16_t g = (bg * invEdgeAlpha + sg * edgeAlpha) >> 8;
                            uint16_t b = (bb * invEdgeAlpha + sb * edgeAlpha) >> 8;

                            frameBuffer[index] = (r << 11) | (g << 5) | b;
                            zBuffer->markShadow(x, y);

                            z += z_step;
                        }
                    }

                    ax += dax_step;
                    az += daz_step;
                    bx += dbx_step;
                    bz += dbz_step;
                }
            }
        }

        __attribute__((always_inline, hot)) static inline void IRAM_ATTR fillTriangleSmooth(int16_t x0, int16_t y0, float z0,
                                                                                            int16_t x1, int16_t y1, float z1,
                                                                                            int16_t x2, int16_t y2, float z2,
                                                                                            float r0, float g0, float b0,
                                                                                            float r1, float g1, float b1,
                                                                                            float r2, float g2, float b2,
                                                                                            uint16_t *frameBuffer,
                                                                                            ZBuffer<320, 240> *zBuffer,
                                                                                            const DisplayConfig &config)
        {
            const int16_t width = config.width;
            const int16_t height = config.height;

            if (!frameBuffer || !zBuffer)
                return;

            if (y0 > y1)
            {
                std::swap(x0, x1);
                std::swap(y0, y1);
                std::swap(z0, z1);
                std::swap(r0, r1);
                std::swap(g0, g1);
                std::swap(b0, b1);
            }
            if (y1 > y2)
            {
                std::swap(x1, x2);
                std::swap(y1, y2);
                std::swap(z1, z2);
                std::swap(r1, r2);
                std::swap(g1, g2);
                std::swap(b1, b2);
            }
            if (y0 > y1)
            {
                std::swap(x0, x1);
                std::swap(y0, y1);
                std::swap(z0, z1);
                std::swap(r0, r1);
                std::swap(g0, g1);
                std::swap(b0, b1);
            }

            if (y0 == y2)
                return;
            if (x0 == x1 && x1 == x2)
                return;

            int16_t dy1 = y1 - y0;
            int16_t dy2 = y2 - y0;
            if (dy1 == 0 && dy2 == 0)
                return;

            float dax_step = 0, dbx_step = 0;
            float dz1_step = 0, dz2_step = 0;
            float dr1_step = 0, dg1_step = 0, db1_step = 0;
            float dr2_step = 0, dg2_step = 0, db2_step = 0;

            if (dy1)
            {
                float invDy1 = 1.0f / dy1;
                dax_step = (float)(x1 - x0) * invDy1;
                dz1_step = (z1 - z0) * invDy1;
                dr1_step = (r1 - r0) * invDy1;
                dg1_step = (g1 - g0) * invDy1;
                db1_step = (b1 - b0) * invDy1;
            }
            if (dy2)
            {
                float invDy2 = 1.0f / dy2;
                dbx_step = (float)(x2 - x0) * invDy2;
                dz2_step = (z2 - z0) * invDy2;
                dr2_step = (r2 - r0) * invDy2;
                dg2_step = (g2 - g0) * invDy2;
                db2_step = (b2 - b0) * invDy2;
            }

            float ax = x0, bx = x0;
            float az = z0, bz = z0;
            float ar = r0, ag = g0, ab = b0;
            float br = r0, bg = g0, bb = b0;

            if (dy1)
            {
                for (int16_t y = y0; y <= y1; y++)
                {
                    if (y < 0 || y >= height)
                    {
                        ax += dax_step;
                        az += dz1_step;
                        ar += dr1_step;
                        ag += dg1_step;
                        ab += db1_step;
                        bx += dbx_step;
                        bz += dz2_step;
                        br += dr2_step;
                        bg += dg2_step;
                        bb += db2_step;
                        continue;
                    }

                    int16_t xa = (int16_t)(ax + 0.5f);
                    int16_t xb = (int16_t)(bx + 0.5f);
                    float za = az, zb = bz;
                    float ra = ar, ga = ag, ba = ab;
                    float rb = br, gb = bg, bb2 = bb;

                    if (xa > xb)
                    {
                        std::swap(xa, xb);
                        std::swap(za, zb);
                        std::swap(ra, rb);
                        std::swap(ga, gb);
                        std::swap(ba, bb2);
                    }

                    int16_t x_start = (xa < 0) ? 0 : xa;
                    int16_t x_end = (xb >= width) ? width - 1 : xb;

                    if (x_start <= x_end && x_start < width && x_end >= 0)
                    {
                        float dx = (float)(xb - xa);
                        float invDx = dx != 0.0f ? 1.0f / dx : 0.0f;
                        float z_step = (zb - za) * invDx;
                        float r_step = (rb - ra) * invDx;
                        float g_step = (gb - ga) * invDx;
                        float b_step = (bb2 - ba) * invDx;

                        float offset = (float)(x_start - xa);
                        float z = za + z_step * offset;
                        float r = ra + r_step * offset;
                        float g = ga + g_step * offset;
                        float b = ba + b_step * offset;

                        size_t index = (size_t)y * width + x_start;
                        uint16_t span = (uint16_t)(x_end - x_start + 1);
                        int16_t x = x_start;

                        while (span >= 4)
                        {
                            if (zBuffer->testAndSet(x, y, z))
                            {
                                frameBuffer[index] = Shading::applyDithering(r, g, b, x, y);
                            }
                            z += z_step;
                            r += r_step;
                            g += g_step;
                            b += b_step;
                            index++;
                            x++;

                            if (zBuffer->testAndSet(x, y, z))
                            {
                                frameBuffer[index] = Shading::applyDithering(r, g, b, x, y);
                            }
                            z += z_step;
                            r += r_step;
                            g += g_step;
                            b += b_step;
                            index++;
                            x++;

                            if (zBuffer->testAndSet(x, y, z))
                            {
                                frameBuffer[index] = Shading::applyDithering(r, g, b, x, y);
                            }
                            z += z_step;
                            r += r_step;
                            g += g_step;
                            b += b_step;
                            index++;
                            x++;

                            if (zBuffer->testAndSet(x, y, z))
                            {
                                frameBuffer[index] = Shading::applyDithering(r, g, b, x, y);
                            }
                            z += z_step;
                            r += r_step;
                            g += g_step;
                            b += b_step;
                            index++;
                            x++;

                            span -= 4;
                        }

                        while (span > 0)
                        {
                            if (zBuffer->testAndSet(x, y, z))
                            {
                                frameBuffer[index] = Shading::applyDithering(r, g, b, x, y);
                            }
                            z += z_step;
                            r += r_step;
                            g += g_step;
                            b += b_step;
                            index++;
                            x++;
                            span--;
                        }
                    }

                    ax += dax_step;
                    az += dz1_step;
                    ar += dr1_step;
                    ag += dg1_step;
                    ab += db1_step;
                    bx += dbx_step;
                    bz += dz2_step;
                    br += dr2_step;
                    bg += dg2_step;
                    bb += db2_step;
                }
            }

            dy1 = y2 - y1;
            if (dy1)
            {
                float invDy1 = 1.0f / dy1;
                dax_step = (float)(x2 - x1) * invDy1;
                dz1_step = (z2 - z1) * invDy1;
                dr1_step = (r2 - r1) * invDy1;
                dg1_step = (g2 - g1) * invDy1;
                db1_step = (b2 - b1) * invDy1;

                ax = x1;
                az = z1;
                ar = r1;
                ag = g1;
                ab = b1;
                bx = x0 + dbx_step * (float)(y1 - y0);
                bz = z0 + dz2_step * (float)(y1 - y0);
                br = r0 + dr2_step * (float)(y1 - y0);
                bg = g0 + dg2_step * (float)(y1 - y0);
                bb = b0 + db2_step * (float)(y1 - y0);

                for (int16_t y = y1 + 1; y <= y2; y++)
                {
                    if (y < 0 || y >= height)
                    {
                        ax += dax_step;
                        az += dz1_step;
                        ar += dr1_step;
                        ag += dg1_step;
                        ab += db1_step;
                        bx += dbx_step;
                        bz += dz2_step;
                        br += dr2_step;
                        bg += dg2_step;
                        bb += db2_step;
                        continue;
                    }

                    int16_t xa = (int16_t)(ax + 0.5f);
                    int16_t xb = (int16_t)(bx + 0.5f);
                    float za = az, zb = bz;
                    float ra = ar, ga = ag, ba = ab;
                    float rb = br, gb = bg, bb2 = bb;

                    if (xa > xb)
                    {
                        std::swap(xa, xb);
                        std::swap(za, zb);
                        std::swap(ra, rb);
                        std::swap(ga, gb);
                        std::swap(ba, bb2);
                    }

                    int16_t x_start = (xa < 0) ? 0 : xa;
                    int16_t x_end = (xb >= width) ? width - 1 : xb;

                    if (x_start <= x_end && x_start < width && x_end >= 0)
                    {
                        float dx = (float)(xb - xa);
                        float invDx = dx != 0.0f ? 1.0f / dx : 0.0f;
                        float z_step = (zb - za) * invDx;
                        float r_step = (rb - ra) * invDx;
                        float g_step = (gb - ga) * invDx;
                        float b_step = (bb2 - ba) * invDx;

                        float offset = (float)(x_start - xa);
                        float z = za + z_step * offset;
                        float r = ra + r_step * offset;
                        float g = ga + g_step * offset;
                        float b = ba + b_step * offset;

                        size_t index = (size_t)y * width + x_start;
                        uint16_t span = (uint16_t)(x_end - x_start + 1);
                        int16_t x = x_start;

                        while (span >= 4)
                        {
                            if (zBuffer->testAndSet(x, y, z))
                            {
                                frameBuffer[index] = Shading::applyDithering(r, g, b, x, y);
                            }
                            z += z_step;
                            r += r_step;
                            g += g_step;
                            b += b_step;
                            index++;
                            x++;

                            if (zBuffer->testAndSet(x, y, z))
                            {
                                frameBuffer[index] = Shading::applyDithering(r, g, b, x, y);
                            }
                            z += z_step;
                            r += r_step;
                            g += g_step;
                            b += b_step;
                            index++;
                            x++;

                            if (zBuffer->testAndSet(x, y, z))
                            {
                                frameBuffer[index] = Shading::applyDithering(r, g, b, x, y);
                            }
                            z += z_step;
                            r += r_step;
                            g += g_step;
                            b += b_step;
                            index++;
                            x++;

                            if (zBuffer->testAndSet(x, y, z))
                            {
                                frameBuffer[index] = Shading::applyDithering(r, g, b, x, y);
                            }
                            z += z_step;
                            r += r_step;
                            g += g_step;
                            b += b_step;
                            index++;
                            x++;

                            span -= 4;
                        }

                        while (span > 0)
                        {
                            if (zBuffer->testAndSet(x, y, z))
                            {
                                frameBuffer[index] = Shading::applyDithering(r, g, b, x, y);
                            }
                            z += z_step;
                            r += r_step;
                            g += g_step;
                            b += b_step;
                            index++;
                            x++;
                            span--;
                        }
                    }

                    ax += dax_step;
                    az += dz1_step;
                    ar += dr1_step;
                    ag += dg1_step;
                    ab += db1_step;
                    bx += dbx_step;
                    bz += dz2_step;
                    br += dr2_step;
                    bg += dg2_step;
                    bb += db2_step;
                }
            }
        }

        static void fillTriangle(int16_t x0, int16_t y0, float z0,
                                 int16_t x1, int16_t y1, float z1,
                                 int16_t x2, int16_t y2, float z2,
                                 uint16_t color,
                                 uint16_t *frameBuffer,
                                 ZBuffer<320, 240> *zBuffer,
                                 const DisplayConfig &config)
        {
            const int16_t width = config.width;
            const int16_t height = config.height;

            if (!frameBuffer || !zBuffer)
                return;
            if (y0 > y1)
            {
                std::swap(x0, x1);
                std::swap(y0, y1);
                std::swap(z0, z1);
            }
            if (y1 > y2)
            {
                std::swap(x1, x2);
                std::swap(y1, y2);
                std::swap(z1, z2);
            }
            if (y0 > y1)
            {
                std::swap(x0, x1);
                std::swap(y0, y1);
                std::swap(z0, z1);
            }

            if (y0 == y2)
                return;
            if (x0 == x1 && x1 == x2)
                return;

            int16_t dy1 = y1 - y0;
            int16_t dy2 = y2 - y0;

            if (dy1 == 0 && dy2 == 0)
                return;

            float dax_step = 0, dbx_step = 0;
            float dz1_step = 0, dz2_step = 0;

            if (dy1)
            {
                dax_step = (float)(x1 - x0) / dy1;
                dz1_step = (z1 - z0) / dy1;
            }
            if (dy2)
            {
                dbx_step = (float)(x2 - x0) / dy2;
                dz2_step = (z2 - z0) / dy2;
            }

            float ax = x0, bx = x0;
            float az = z0, bz = z0;

            if (dy1)
            {
                for (int16_t y = y0; y <= y1; y++)
                {
                    if (y < 0 || y >= height)
                    {
                        ax += dax_step;
                        az += dz1_step;
                        bx += dbx_step;
                        bz += dz2_step;
                        continue;
                    }

                    int16_t xa = (int16_t)(ax + 0.5f);
                    int16_t xb = (int16_t)(bx + 0.5f);
                    float za = az, zb = bz;

                    if (xa > xb)
                    {
                        std::swap(xa, xb);
                        std::swap(za, zb);
                    }

                    int16_t x_start = (xa < 0) ? 0 : xa;
                    int16_t x_end = (xb >= width) ? width - 1 : xb;

                    if (x_start <= x_end && x_start < width && x_end >= 0)
                    {
                        float z_step = (xb - xa) != 0 ? (zb - za) / (xb - xa) : 0;
                        float z = za + z_step * (x_start - xa);
                        zBuffer->testAndSetScanline(y, x_start, x_end, z, z_step, frameBuffer, color);
                    }

                    ax += dax_step;
                    az += dz1_step;
                    bx += dbx_step;
                    bz += dz2_step;
                }
            }

            dy1 = y2 - y1;
            if (dy1)
            {
                dax_step = (float)(x2 - x1) / dy1;
                dz1_step = (z2 - z1) / dy1;
                ax = x1;
                az = z1;

                for (int16_t y = y1 + 1; y <= y2; y++)
                {
                    if (y < 0 || y >= height)
                    {
                        ax += dax_step;
                        az += dz1_step;
                        bx += dbx_step;
                        bz += dz2_step;
                        continue;
                    }

                    int16_t xa = (int16_t)(ax + 0.5f);
                    int16_t xb = (int16_t)(bx + 0.5f);
                    float za = az, zb = bz;

                    if (xa > xb)
                    {
                        std::swap(xa, xb);
                        std::swap(za, zb);
                    }

                    int16_t x_start = (xa < 0) ? 0 : xa;
                    int16_t x_end = (xb >= width) ? width - 1 : xb;

                    if (x_start <= x_end && x_start < width && x_end >= 0)
                    {
                        float z_step = (xb - xa) != 0 ? (zb - za) / (xb - xa) : 0;
                        float z = za + z_step * (x_start - xa);
                        zBuffer->testAndSetScanline(y, x_start, x_end, z, z_step, frameBuffer, color);
                    }

                    ax += dax_step;
                    az += dz1_step;
                    bx += dbx_step;
                    bz += dz2_step;
                }
            }
        }
    };

}

#endif
