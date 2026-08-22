#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "Core/Platform.hpp"
#include "Math/Algebra.hpp"
#include "Rendering/Buffers/ZBuffer.hpp"
#include "Rendering/Lighting/Fog.hpp"
#include "Rendering/Resources/Texture.hpp"

#if PIP3D_DEBUG_BILLBOARD
#include "Debug/Logging.hpp"
#endif

namespace pip3D
{
    enum BillboardBlend : uint8_t
    {
        BB_BLEND_OPAQUE = 0,
        BB_BLEND_CUTOUT = 1,
        BB_BLEND_ALPHA = 2,
        BB_BLEND_ADDITIVE = 3
    };

    namespace Rasterizer
    {
        __attribute__((hot)) inline void fillTriangleBillboard(
            float x0, float y0, float z0,
            float x1, float y1, float z1,
            float x2, float y2, float z2,
            float u0, float v0,
            float u1, float v1,
            float u2, float v2,
            float w0, float w1, float w2,
            float lr0, float lg0, float lb0,
            float lr1, float lg1, float lb1,
            float lr2, float lg2, float lb2,
            const Texture &tex,
            BillboardBlend blendMode,
            uint16_t chromaKey,
            uint8_t alphaByte,
            bool writeZ,
            uint16_t *frameBuffer,
            ZBuffer *zBuffer,
            const DisplayConfig &cfg)
        {
            const int16_t width = cfg.width;
            const int16_t height = cfg.height;

            if (unlikely(!frameBuffer || !zBuffer))
            {
#if PIP3D_DEBUG_BILLBOARD
                LOGW(::pip3D::Debug::LOG_MODULE_RENDER,
                     "[BB-rast] null buffer fb=%p zb=%p",
                     static_cast<void *>(frameBuffer), static_cast<void *>(zBuffer));
#endif
                return;
            }

            if (y0 > y1)
            {
                std::swap(x0, x1);
                std::swap(y0, y1);
                std::swap(z0, z1);
                std::swap(u0, u1);
                std::swap(v0, v1);
                std::swap(w0, w1);
                std::swap(lr0, lr1);
                std::swap(lg0, lg1);
                std::swap(lb0, lb1);
            }
            if (y1 > y2)
            {
                std::swap(x1, x2);
                std::swap(y1, y2);
                std::swap(z1, z2);
                std::swap(u1, u2);
                std::swap(v1, v2);
                std::swap(w1, w2);
                std::swap(lr1, lr2);
                std::swap(lg1, lg2);
                std::swap(lb1, lb2);
            }
            if (y0 > y1)
            {
                std::swap(x0, x1);
                std::swap(y0, y1);
                std::swap(z0, z1);
                std::swap(u0, u1);
                std::swap(v0, v1);
                std::swap(w0, w1);
                std::swap(lr0, lr1);
                std::swap(lg0, lg1);
                std::swap(lb0, lb1);
            }

            if (y0 == y2)
                return;
            if (unlikely(x0 == x1 && x1 == x2))
                return;

            const float dx02 = x0 - x2;
            const float dy12 = y1 - y2;
            const float dy02 = y0 - y2;
            const float dx12 = x1 - x2;
            const float det = dx02 * dy12 - dy02 * dx12;
            if (unlikely(fabsf(det) < 1e-6f))
                return;
            const float invDet = FastMath::fastReciprocal(det);

            const float dz02 = z0 - z2;
            const float dz12 = z1 - z2;
            const float dz_dx = (dz02 * dy12 - dy02 * dz12) * invDet;
            const float dz_dy = (dx02 * dz12 - dz02 * dx12) * invDet;
            const float dz_dx_scaled = dz_dx;
            const float dz_dy_scaled = dz_dy;
            const float z2_scaled = z2;
            const int32_t z_step = static_cast<int32_t>(dz_dx_scaled * 16384.0f);

            const float texW = tex.widthFlt();
            const float texH = tex.heightFlt();
            const float tu0 = u0 * texW, tu1 = u1 * texW, tu2 = u2 * texW;
            const float tv0 = v0 * texH, tv1 = v1 * texH, tv2 = v2 * texH;

            const float q0 = FastMath::fastReciprocal(w0);
            const float q1 = FastMath::fastReciprocal(w1);
            const float q2 = FastMath::fastReciprocal(w2);
            const float uoz0 = tu0 * q0, uoz1 = tu1 * q1, uoz2 = tu2 * q2;
            const float voz0 = tv0 * q0, voz1 = tv1 * q1, voz2 = tv2 * q2;

            const float dq02 = q0 - q2, dq12 = q1 - q2;
            const float dq_dx = (dq02 * dy12 - dy02 * dq12) * invDet;
            const float dq_dy = (dx02 * dq12 - dq02 * dx12) * invDet;

            const float duoz02 = uoz0 - uoz2, duoz12 = uoz1 - uoz2;
            const float duoz_dx = (duoz02 * dy12 - dy02 * duoz12) * invDet;
            const float duoz_dy = (dx02 * duoz12 - duoz02 * dx12) * invDet;

            const float dvoz02 = voz0 - voz2, dvoz12 = voz1 - voz2;
            const float dvoz_dx = (dvoz02 * dy12 - dy02 * dvoz12) * invDet;
            const float dvoz_dy = (dx02 * dvoz12 - dvoz02 * dx12) * invDet;

            const bool flatLight =
                (fabsf(lr0 - lr1) < 1e-3f && fabsf(lr1 - lr2) < 1e-3f &&
                 fabsf(lg0 - lg1) < 1e-3f && fabsf(lg1 - lg2) < 1e-3f &&
                 fabsf(lb0 - lb1) < 1e-3f && fabsf(lb1 - lb2) < 1e-3f);
            const uint32_t flatR5 = flatLight ? static_cast<uint32_t>(clamp(lr0, 0.0f, 1.0f) * 31.0f + 0.5f) : 0;
            uint32_t flatG6 = flatLight ? static_cast<uint32_t>(clamp(lg0, 0.0f, 1.0f) * 63.0f + 0.5f) : 0;
            const uint32_t flatB5 = flatLight ? static_cast<uint32_t>(clamp(lb0, 0.0f, 1.0f) * 31.0f + 0.5f) : 0;
            if (flatLight && flatG6 < 16u)
                flatG6 &= ~1u;

            float dlr_dx = 0, dlg_dx = 0, dlb_dx = 0;
            int32_t lr_step = 0, lg_step = 0, lb_step = 0;
            if (!flatLight)
            {
                const float dlr02 = lr0 - lr2, dlr12 = lr1 - lr2;
                const float dlg02 = lg0 - lg2, dlg12 = lg1 - lg2;
                const float dlb02 = lb0 - lb2, dlb12 = lb1 - lb2;
                dlr_dx = (dlr02 * dy12 - dy02 * dlr12) * invDet;
                dlg_dx = (dlg02 * dy12 - dy02 * dlg12) * invDet;
                dlb_dx = (dlb02 * dy12 - dy02 * dlb12) * invDet;
                lr_step = static_cast<int32_t>(dlr_dx * 65536.0f);
                lg_step = static_cast<int32_t>(dlg_dx * 65536.0f);
                lb_step = static_cast<int32_t>(dlb_dx * 65536.0f);
            }

            const uint16_t *const __restrict__ texData = tex.data;
            const uint32_t texShiftU = tex.shiftU;
            const uint32_t texMaskU = tex.maskU();
            const uint32_t texMaskV = tex.maskV();

            const bool doBlend = (blendMode == BB_BLEND_ALPHA) && (alphaByte > 0) && (alphaByte < 255);
            const uint32_t alpha5 = doBlend ? static_cast<uint32_t>(alphaByte >> 3) : 0u;
            const uint32_t invAlpha5 = doBlend ? (32u - alpha5) : 0u;

            const bool isAdditive = (blendMode == BB_BLEND_ADDITIVE);
            const uint32_t addI5 = isAdditive ? static_cast<uint32_t>(alphaByte >> 3) : 0u;

            const bool fogEnabled = g_fogState.enabled;
            const float fogWScale = g_fogState.wScale;
            const float fogWorldNear = g_fogState.worldNear;
            const float fogWorldScale32 = g_fogState.worldScale32;
            const uint16_t fogColor = g_fogState.color;
            const uint32_t fogColorRb = g_fogState.color_rb;
            const uint32_t fogColorG = g_fogState.color_g;

            uint16_t *__restrict__ zbBase = zBuffer->data();

#if PIP3D_DEBUG_BILLBOARD
            uint32_t dbgSpan = 0, dbgCutout = 0, dbgZFail = 0, dbgWritten = 0;
#endif

            auto drawSpan = [&](int y, int16_t x_start, int16_t x_end)
            {
                if (x_start > x_end)
                    return;

                const float dy_factor = (static_cast<float>(y) + 0.5f - y2);
                const float z_dy_term = z2_scaled + dy_factor * dz_dy_scaled;

                const float pxStart = (static_cast<float>(x_start) + 0.5f) - x2;
                float q = q2 + dy_factor * dq_dy + pxStart * dq_dx;
                float uoz = uoz2 + dy_factor * duoz_dy + pxStart * duoz_dx;
                float voz = voz2 + dy_factor * dvoz_dy + pxStart * dvoz_dx;

                const float z_scaled_start = z_dy_term + pxStart * dz_dx_scaled;
                int32_t z_val = static_cast<int32_t>(z_scaled_start * 16384.0f);

                int32_t lr_cur = 0, lg_cur = 0, lb_cur = 0;
                if (!flatLight)
                {
                    lr_cur = static_cast<int32_t>((lr2 + dlr_dx * pxStart) * 65536.0f);
                    lg_cur = static_cast<int32_t>((lg2 + dlg_dx * pxStart) * 65536.0f);
                    lb_cur = static_cast<int32_t>((lb2 + dlb_dx * pxStart) * 65536.0f);
                }

                size_t index = static_cast<size_t>(y) * width + x_start;
                uint16_t *__restrict__ zb = zbBase + index;
                uint16_t *__restrict__ fb = frameBuffer + index;

                for (int16_t x = x_start; x <= x_end; ++x)
                {
#if PIP3D_DEBUG_BILLBOARD
                    ++dbgSpan;
#endif
                    const float inv_q = FastMath::fastReciprocal(q);
                    const float u = uoz * inv_q;
                    const float v = voz * inv_q;
                    const uint32_t tu = static_cast<uint32_t>(u) & texMaskU;
                    const uint32_t tv = static_cast<uint32_t>(v) & texMaskV;
                    const uint16_t texel = texData[(tv << texShiftU) | tu];

                    if (blendMode == BB_BLEND_CUTOUT && texel == chromaKey)
                    {
#if PIP3D_DEBUG_BILLBOARD
                        ++dbgCutout;
#endif
                        q += dq_dx;
                        uoz += duoz_dx;
                        voz += dvoz_dx;
                        z_val += z_step;
                        if (!flatLight)
                        {
                            lr_cur += lr_step;
                            lg_cur += lg_step;
                            lb_cur += lb_step;
                        }
                        ++zb;
                        ++fb;
                        continue;
                    }

                    const uint16_t d = static_cast<uint16_t>(z_val >> 14);
                    const uint16_t cur = *zb & Z_DEPTH_MASK;

                    if (blendMode == BB_BLEND_ALPHA && doBlend)
                    {

                        if (d <= cur)
                        {
#if PIP3D_DEBUG_BILLBOARD
                            ++dbgZFail;
#endif
                            q += dq_dx;
                            uoz += duoz_dx;
                            voz += dvoz_dx;
                            z_val += z_step;
                            if (!flatLight)
                            {
                                lr_cur += lr_step;
                                lg_cur += lg_step;
                                lb_cur += lb_step;
                            }
                            ++zb;
                            ++fb;
                            continue;
                        }

                        if (writeZ)
                            *zb = d;

                        uint16_t lit;
                        if (flatLight)
                        {
                            const Color tc(texel);
                            lit = static_cast<uint16_t>(((tc.r5() * flatR5) >> 5) << 11 |
                                                        ((tc.g6() * flatG6) >> 6) << 5 |
                                                        ((tc.b5() * flatB5) >> 5));
                        }
                        else
                        {
                            const int32_t r5 = lr_cur >> 16, g6 = lg_cur >> 16, b5 = lb_cur >> 16;
                            const uint32_t r5c = r5 < 0 ? 0 : (r5 > 31 ? 31 : r5);
                            const uint32_t g6c = g6 < 0 ? 0 : (g6 > 63 ? 63 : g6);
                            const uint32_t b5c = b5 < 0 ? 0 : (b5 > 31 ? 31 : b5);
                            const uint32_t r = (texel >> 11) & 0x1F;
                            const uint32_t g = (texel >> 5) & 0x3F;
                            const uint32_t b = texel & 0x1F;
                            lit = static_cast<uint16_t>(((r * r5c) >> 5) << 11 |
                                                        ((g * g6c) >> 6) << 5 |
                                                        ((b * b5c) >> 5));
                        }

                        if (fogEnabled)
                        {
                            float z_eye;
                            if (d == 0)
                                z_eye = 1e30f;
                            else
                                z_eye = fogWScale * FastMath::fastReciprocal(static_cast<float>(d));
                            const float fogF = (z_eye - fogWorldNear) * fogWorldScale32;
                            const int32_t fa = static_cast<int32_t>(fogF);
                            if (fa >= 32)
                                lit = fogColor;
                            else if (fa > 0)
                            {
                                const uint32_t ifa = 32u - static_cast<uint32_t>(fa);
                                const uint32_t f32 = static_cast<uint32_t>(fa);
                                const uint32_t rb = (((lit & 0xF81F) * ifa + fogColorRb * f32) >> 5) & 0xF81F;
                                const uint32_t gg = (((lit & 0x07E0) * ifa + fogColorG * f32) >> 5) & 0x07E0;
                                lit = static_cast<uint16_t>(rb | gg);
                            }
                        }

                        const uint32_t dst = *fb;
                        const uint32_t rb = (((dst & 0xF81F) * invAlpha5 + (lit & 0xF81F) * alpha5) >> 5) & 0xF81F;
                        const uint32_t gg = (((dst & 0x07E0) * invAlpha5 + (lit & 0x07E0) * alpha5) >> 5) & 0x07E0;
                        *fb = static_cast<uint16_t>(rb | gg);
#if PIP3D_DEBUG_BILLBOARD
                        ++dbgWritten;
#endif
                    }
                    else if (isAdditive)
                    {
                        if (texel == 0u)
                        {
#if PIP3D_DEBUG_BILLBOARD
                            ++dbgCutout;
#endif
                            q += dq_dx;
                            uoz += duoz_dx;
                            voz += dvoz_dx;
                            z_val += z_step;
                            if (!flatLight)
                            {
                                lr_cur += lr_step;
                                lg_cur += lg_step;
                                lb_cur += lb_step;
                            }
                            ++zb;
                            ++fb;
                            continue;
                        }

                        if (d <= cur)
                        {
#if PIP3D_DEBUG_BILLBOARD
                            ++dbgZFail;
#endif
                            q += dq_dx;
                            uoz += duoz_dx;
                            voz += dvoz_dx;
                            z_val += z_step;
                            if (!flatLight)
                            {
                                lr_cur += lr_step;
                                lg_cur += lg_step;
                                lb_cur += lb_step;
                            }
                            ++zb;
                            ++fb;
                            continue;
                        }

                        uint16_t lit;
                        if (flatLight)
                        {
                            const Color tc(texel);
                            lit = static_cast<uint16_t>(((tc.r5() * flatR5) >> 5) << 11 |
                                                        ((tc.g6() * flatG6) >> 6) << 5 |
                                                        ((tc.b5() * flatB5) >> 5));
                        }
                        else
                        {
                            const int32_t r5 = lr_cur >> 16, g6 = lg_cur >> 16, b5 = lb_cur >> 16;
                            const uint32_t r5c = r5 < 0 ? 0 : (r5 > 31 ? 31 : r5);
                            const uint32_t g6c = g6 < 0 ? 0 : (g6 > 63 ? 63 : g6);
                            const uint32_t b5c = b5 < 0 ? 0 : (b5 > 31 ? 31 : b5);
                            const uint32_t r = (texel >> 11) & 0x1F;
                            const uint32_t g = (texel >> 5) & 0x3F;
                            const uint32_t b = texel & 0x1F;
                            lit = static_cast<uint16_t>(((r * r5c) >> 5) << 11 |
                                                        ((g * g6c) >> 6) << 5 |
                                                        ((b * b5c) >> 5));
                        }

                        if (fogEnabled)
                        {
                            float z_eye;
                            if (d == 0)
                                z_eye = 1e30f;
                            else
                                z_eye = fogWScale * FastMath::fastReciprocal(static_cast<float>(d));
                            const float fogF = (z_eye - fogWorldNear) * fogWorldScale32;
                            const int32_t fa = static_cast<int32_t>(fogF);
                            if (fa >= 32)
                                lit = fogColor;
                            else if (fa > 0)
                            {
                                const uint32_t ifa = 32u - static_cast<uint32_t>(fa);
                                const uint32_t f32 = static_cast<uint32_t>(fa);
                                const uint32_t rb = (((lit & 0xF81F) * ifa + fogColorRb * f32) >> 5) & 0xF81F;
                                const uint32_t gg = (((lit & 0x07E0) * ifa + fogColorG * f32) >> 5) & 0x07E0;
                                lit = static_cast<uint16_t>(rb | gg);
                            }
                        }

                        const uint32_t dst = *fb;
                        const uint32_t litRb = lit & 0xF81F;
                        const uint32_t litG = lit & 0x07E0;
                        const uint32_t addRb = (litRb * addI5) >> 5;
                        const uint32_t addG = (litG * addI5) >> 5;
                        const uint32_t sumRb = (dst & 0xF81F) + addRb;
                        const uint32_t sumG = (dst & 0x07E0) + addG;
                        const uint32_t outRb = (sumRb > 0xF81Fu) ? 0xF81Fu : (sumRb & 0xF81F);
                        const uint32_t outG = (sumG > 0x07E0u) ? 0x07E0u : (sumG & 0x07E0);
                        *fb = static_cast<uint16_t>(outRb | outG);
#if PIP3D_DEBUG_BILLBOARD
                        ++dbgWritten;
#endif
                    }
                    else
                    {

                        if (d > cur)
                        {
                            *zb = d;

                            uint16_t lit;
                            if (flatLight)
                            {
                                const Color tc(texel);
                                lit = static_cast<uint16_t>(((tc.r5() * flatR5) >> 5) << 11 |
                                                            ((tc.g6() * flatG6) >> 6) << 5 |
                                                            ((tc.b5() * flatB5) >> 5));
                            }
                            else
                            {
                                const int32_t r5 = lr_cur >> 16, g6 = lg_cur >> 16, b5 = lb_cur >> 16;
                                const uint32_t r5c = r5 < 0 ? 0 : (r5 > 31 ? 31 : r5);
                                const uint32_t g6c = g6 < 0 ? 0 : (g6 > 63 ? 63 : g6);
                                const uint32_t b5c = b5 < 0 ? 0 : (b5 > 31 ? 31 : b5);
                                const uint32_t r = (texel >> 11) & 0x1F;
                                const uint32_t g = (texel >> 5) & 0x3F;
                                const uint32_t b = texel & 0x1F;
                                lit = static_cast<uint16_t>(((r * r5c) >> 5) << 11 |
                                                            ((g * g6c) >> 6) << 5 |
                                                            ((b * b5c) >> 5));
                            }

                            if (fogEnabled)
                            {
                                float z_eye;
                                if (d == 0)
                                    z_eye = 1e30f;
                                else
                                    z_eye = fogWScale * FastMath::fastReciprocal(static_cast<float>(d));
                                const float fogF = (z_eye - fogWorldNear) * fogWorldScale32;
                                const int32_t fa = static_cast<int32_t>(fogF);
                                if (fa >= 32)
                                    lit = fogColor;
                                else if (fa > 0)
                                {
                                    const uint32_t ifa = 32u - static_cast<uint32_t>(fa);
                                    const uint32_t f32 = static_cast<uint32_t>(fa);
                                    const uint32_t rb = (((lit & 0xF81F) * ifa + fogColorRb * f32) >> 5) & 0xF81F;
                                    const uint32_t gg = (((lit & 0x07E0) * ifa + fogColorG * f32) >> 5) & 0x07E0;
                                    lit = static_cast<uint16_t>(rb | gg);
                                }
                            }

                            *fb = lit;
#if PIP3D_DEBUG_BILLBOARD
                            ++dbgWritten;
#endif
                        }
#if PIP3D_DEBUG_BILLBOARD
                        else
                            ++dbgZFail;
#endif
                    }

                    q += dq_dx;
                    uoz += duoz_dx;
                    voz += dvoz_dx;
                    z_val += z_step;
                    if (!flatLight)
                    {
                        lr_cur += lr_step;
                        lg_cur += lg_step;
                        lb_cur += lb_step;
                    }
                    ++zb;
                    ++fb;
                }
            };

            const float dy02_val = y2 - y0;
            const float dy01_val = y1 - y0;
            const float dy12_val = y2 - y1;

            const int32_t step02 = (fabsf(dy02_val) > 1e-6f) ? static_cast<int32_t>(((x2 - x0) / dy02_val) * 65536.0f) : 0;
            const int32_t step01 = (fabsf(dy01_val) > 1e-6f) ? static_cast<int32_t>(((x1 - x0) / dy01_val) * 65536.0f) : 0;
            const int32_t step12 = (fabsf(dy12_val) > 1e-6f) ? static_cast<int32_t>(((x2 - x1) / dy12_val) * 65536.0f) : 0;

            const int startY = static_cast<int>(ceilf(y0 - 0.5f));
            const int midY = static_cast<int>(ceilf(y1 - 0.5f));
            const int endY = static_cast<int>(ceilf(y2 - 0.5f));

            int yA0 = startY < 0 ? 0 : startY;
            int yA1 = midY > height ? height : midY;

            if (yA0 < yA1)
            {
                const float dyInitA = (static_cast<float>(yA0) + 0.5f) - y0;
                const float slope02 = (fabsf(dy02_val) > 1e-6f) ? (x2 - x0) / dy02_val : 0.0f;
                const float slope01 = (fabsf(dy01_val) > 1e-6f) ? (x1 - x0) / dy01_val : 0.0f;
                int32_t x02 = static_cast<int32_t>((x0 + slope02 * dyInitA) * 65536.0f);
                int32_t x01 = static_cast<int32_t>((x0 + slope01 * dyInitA) * 65536.0f);

                for (int y = yA0; y < yA1; ++y)
                {
                    int16_t xs = static_cast<int16_t>((x02 + 32767) >> 16);
                    int16_t xe = static_cast<int16_t>((x01 - 32769) >> 16);
                    if (xs > xe)
                        std::swap(xs, xe);
                    if (xs < 0)
                        xs = 0;
                    if (xe >= width)
                        xe = static_cast<int16_t>(width - 1);
                    drawSpan(y, xs, xe);
                    x02 += step02;
                    x01 += step01;
                }
            }

            int yB0 = midY < 0 ? 0 : midY;
            int yB1 = endY > height ? height : endY;

            if (yB0 < yB1)
            {
                const float dyInitB = (static_cast<float>(yB0) + 0.5f) - y1;
                const float dyInitBlong = (static_cast<float>(yB0) + 0.5f) - y0;
                const float slope02 = (fabsf(dy02_val) > 1e-6f) ? (x2 - x0) / dy02_val : 0.0f;
                const float slope12 = (fabsf(dy12_val) > 1e-6f) ? (x2 - x1) / dy12_val : 0.0f;
                int32_t x12 = static_cast<int32_t>((x1 + slope12 * dyInitB) * 65536.0f);
                int32_t x02b = static_cast<int32_t>((x0 + slope02 * dyInitBlong) * 65536.0f);

                for (int y = yB0; y < yB1; ++y)
                {
                    int16_t xs = static_cast<int16_t>((x02b + 32767) >> 16);
                    int16_t xe = static_cast<int16_t>((x12 - 32769) >> 16);
                    if (xs > xe)
                        std::swap(xs, xe);
                    if (xs < 0)
                        xs = 0;
                    if (xe >= width)
                        xe = static_cast<int16_t>(width - 1);
                    drawSpan(y, xs, xe);
                    x02b += step02;
                    x12 += step12;
                }
            }

#if PIP3D_DEBUG_BILLBOARD
            LOGI(::pip3D::Debug::LOG_MODULE_RENDER,
                 "[BB-rast] y=[%d..%d..%d] span=%u cutout=%u zfail=%u written=%u mode=%d",
                 static_cast<int>(y0), static_cast<int>(y1), static_cast<int>(y2),
                 dbgSpan, dbgCutout, dbgZFail, dbgWritten, static_cast<int>(blendMode));
#endif
        }
    }
}
