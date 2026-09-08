#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include <Pip3D.hpp>
#include "Geometry/Unwrap.hpp"

namespace pip3D
{
    namespace Bake
    {

        inline void atrousDenoise(std::vector<float> &col,
                                  const std::vector<float> &var,
                                  const std::vector<Vector3> &texelPos,
                                  const std::vector<Vector3> &texelNrm,
                                  const std::vector<TexelHit> &texels,
                                  uint32_t w, uint32_t h,
                                  uint32_t passes, float texelMeters)
        {
            if (col.empty() || w == 0 || h == 0)
                return;
            passes = std::clamp(passes, 1u, 4u);

            const size_t n = static_cast<size_t>(w) * h;
            std::vector<float> tmp(n * 3);
            std::vector<float> varCur(var.begin(), var.end());
            std::vector<float> varTmp(n);

            constexpr float kC0 = 0.375f;
            constexpr float kC1 = 0.25f;
            constexpr float kC2 = 0.0625f;

            const float planeSigma = texelMeters * 4.0f;

            for (uint32_t it = 0; it < passes; ++it)
            {
                const int step = 1 << it;
                const float varScale = static_cast<float>(step * step);
                const float invVar = 1.0f / (std::sqrt(std::max(varScale, 1.0f)) + 1e-6f);

                for (uint32_t y = 0; y < h; ++y)
                {
                    for (uint32_t x = 0; x < w; ++x)
                    {
                        const size_t idx = static_cast<size_t>(y) * w + x;
                        if (texels[idx].face == 0xFFFFFFFFu)
                            continue;

                        const float lc[3] = {col[idx * 3 + 0], col[idx * 3 + 1], col[idx * 3 + 2]};
                        const float lumC = lc[0] * 0.2126f + lc[1] * 0.7152f + lc[2] * 0.0722f;
                        const Vector3 &pC = texelPos[idx];
                        const Vector3 &nC = texelNrm[idx];

                        float o[3] = {lc[0] * kC0, lc[1] * kC0, lc[2] * kC0};
                        float wsum = kC0;
                        float vsum = varCur.empty() ? 0.0f : varCur[idx] * kC0 * kC0;

                        for (int d = 0; d < 4; ++d)
                        {
                            static constexpr int kDx[4] = {1, -1, 0, 0};
                            static constexpr int kDy[4] = {0, 0, 1, -1};
                            const int sx = kDx[d] * step;
                            const int sy = kDy[d] * step;

                            const int nx = static_cast<int>(x) + sx;
                            const int ny = static_cast<int>(y) + sy;
                            if (nx < 0 || ny < 0 ||
                                nx >= static_cast<int>(w) || ny >= static_cast<int>(h))
                                continue;
                            const size_t nidx = static_cast<size_t>(ny) * w + nx;
                            if (texels[nidx].face == 0xFFFFFFFFu)
                                continue;

                            const Vector3 &nP = texelPos[nidx];
                            const Vector3 &nN = texelNrm[nidx];

                            const float ndot = nC.x * nN.x + nC.y * nN.y + nC.z * nN.z;

                            float wNb = std::max(ndot, 0.0f);
                            wNb *= wNb;
                            wNb *= wNb;
                            wNb *= wNb;
                            wNb *= wNb;
                            wNb *= wNb;
                            const float wN = wNb;
                            if (wN < 1e-3f)
                                continue;

                            const float dp = std::fabs((nP.x - pC.x) * nC.x +
                                                       (nP.y - pC.y) * nC.y +
                                                       (nP.z - pC.z) * nC.z);
                            if (dp > planeSigma * static_cast<float>(step))
                                continue;
                            const float wP = std::exp(-(dp * dp) /
                                                      (planeSigma * static_cast<float>(step) *
                                                       planeSigma * static_cast<float>(step)));

                            const float ln[3] = {col[nidx * 3 + 0], col[nidx * 3 + 1], col[nidx * 3 + 2]};
                            const float lumN = ln[0] * 0.2126f + ln[1] * 0.7152f + ln[2] * 0.0722f;
                            const float sig = std::fmax(
                                (varCur.empty() ? 0.0f : varCur[idx]) * invVar * 2.0f, 0.02f);
                            const float dl = (lumN - lumC) / sig;
                            const float wL = std::exp(-dl * dl);

                            const float wt = kC1 * wN * wP * wL;
                            o[0] += ln[0] * wt;
                            o[1] += ln[1] * wt;
                            o[2] += ln[2] * wt;
                            wsum += wt;
                            if (!varCur.empty())
                                vsum += varCur[nidx] * wt * wt;
                        }

                        const float invW = 1.0f / std::fmax(wsum, 1e-6f);
                        tmp[idx * 3 + 0] = o[0] * invW;
                        tmp[idx * 3 + 1] = o[1] * invW;
                        tmp[idx * 3 + 2] = o[2] * invW;
                        if (!varCur.empty())
                            varTmp[idx] = vsum * invW * invW;
                    }
                }
                col.swap(tmp);
                if (!varCur.empty())
                    varCur.swap(varTmp);
            }
        }

        inline void blurLightmap(std::vector<float> &col,
                                 const std::vector<Vector3> &texelPos,
                                 const std::vector<Vector3> &texelNrm,
                                 const std::vector<TexelHit> &texels,
                                 uint32_t w, uint32_t h,
                                 uint32_t passes, float sigma, float texelMeters)
        {
            if (col.empty() || w == 0 || h == 0 || passes == 0)
                return;
            passes = std::clamp(passes, 1u, 3u);
            sigma = std::fmax(0.5f, std::fmin(2.5f, sigma));
            const size_t n = static_cast<size_t>(w) * h;
            std::vector<float> tmp(n * 3);

            const float w0 = 0.402f, w1 = 0.244f, w2 = 0.054f;
            const float planeSigma = texelMeters * 3.0f * sigma;

            auto blurAxis = [&](uint32_t stride)
            {
                const uint32_t outerMax = (stride == 1u) ? h : w;
                const uint32_t innerMax = (stride == 1u) ? w : h;
                for (uint32_t o = 0; o < outerMax; ++o)
                {
                    for (uint32_t in = 0; in < innerMax; ++in)
                    {
                        const size_t idx = (stride == 1u) ? static_cast<size_t>(o) * w + in
                                                          : static_cast<size_t>(in) * w + o;
                        if (texels[idx].face == 0xFFFFFFFFu)
                            continue;
                        const Vector3 &pC = texelPos[idx];
                        const Vector3 &nC = texelNrm[idx];
                        float acc[3] = {col[idx * 3 + 0] * w0, col[idx * 3 + 1] * w0, col[idx * 3 + 2] * w0};
                        float wsum = w0;
                        for (int dir = -2; dir <= 2; ++dir)
                        {
                            if (dir == 0)
                                continue;
                            const int ni = static_cast<int>(in) + dir;
                            if (ni < 0 || ni >= static_cast<int>(innerMax))
                                continue;
                            const size_t nidx = (stride == 1u) ? static_cast<size_t>(o) * w + static_cast<uint32_t>(ni)
                                                               : static_cast<size_t>(ni) * w + o;
                            if (texels[nidx].face == 0xFFFFFFFFu)
                                continue;
                            const Vector3 &nN = texelNrm[nidx];
                            const float ndot = nC.x * nN.x + nC.y * nN.y + nC.z * nN.z;
                            if (ndot < 0.92f)
                                continue;
                            const Vector3 &pN = texelPos[nidx];
                            const float dp = std::fabs((pN.x - pC.x) * nC.x + (pN.y - pC.y) * nC.y + (pN.z - pC.z) * nC.z);
                            if (dp > planeSigma)
                                continue;
                            const float kw = (std::abs(dir) == 1) ? w1 : w2;
                            acc[0] += col[nidx * 3 + 0] * kw;
                            acc[1] += col[nidx * 3 + 1] * kw;
                            acc[2] += col[nidx * 3 + 2] * kw;
                            wsum += kw;
                        }
                        const float inv = 1.0f / wsum;
                        tmp[idx * 3 + 0] = acc[0] * inv;
                        tmp[idx * 3 + 1] = acc[1] * inv;
                        tmp[idx * 3 + 2] = acc[2] * inv;
                    }
                }
                for (size_t i = 0; i < n; ++i)
                    if (texels[i].face != 0xFFFFFFFFu)
                    {
                        col[i * 3 + 0] = tmp[i * 3 + 0];
                        col[i * 3 + 1] = tmp[i * 3 + 1];
                        col[i * 3 + 2] = tmp[i * 3 + 2];
                    }
            };

            for (uint32_t pass = 0; pass < passes; ++pass)
            {
                blurAxis(1u);
                blurAxis(w);
            }
        }

        inline void dilateChannelsRect(std::vector<float> &den,
                                       const std::vector<TexelHit> &texels,
                                       uint32_t w, uint32_t h, uint32_t passes)
        {
            if (den.empty() || w == 0 || h == 0)
                return;
            const size_t n = static_cast<size_t>(w) * h;
            std::vector<uint8_t> filled(n, 0);
            for (size_t i = 0; i < n; ++i)
                filled[i] = (texels[i].face != 0xFFFFFFFFu) ? 1 : 0;

            for (uint32_t pass = 0; pass < passes; ++pass)
            {
                bool anyGrew = false;
                std::vector<uint8_t> nextFilled = filled;
                for (uint32_t y = 0; y < h; ++y)
                {
                    for (uint32_t x = 0; x < w; ++x)
                    {
                        const size_t idx = static_cast<size_t>(y) * w + x;
                        if (filled[idx])
                            continue;
                        float sum[3] = {0.0f, 0.0f, 0.0f};
                        int cnt = 0;
                        static constexpr int kDx[4] = {1, -1, 0, 0};
                        static constexpr int kDy[4] = {0, 0, 1, -1};
                        for (int d = 0; d < 4; ++d)
                        {
                            const int nx = static_cast<int>(x) + kDx[d];
                            const int ny = static_cast<int>(y) + kDy[d];
                            if (nx < 0 || ny < 0 ||
                                nx >= static_cast<int>(w) || ny >= static_cast<int>(h))
                                continue;
                            const size_t ni = static_cast<size_t>(ny) * w + nx;
                            if (filled[ni])
                            {
                                for (int c = 0; c < 3; ++c)
                                    sum[c] += den[ni * 3 + c];
                                ++cnt;
                            }
                        }
                        if (cnt > 0)
                        {
                            const float inv = 1.0f / static_cast<float>(cnt);
                            for (int c = 0; c < 3; ++c)
                                den[idx * 3 + c] = sum[c] * inv;
                            nextFilled[idx] = 1;
                            anyGrew = true;
                        }
                    }
                }
                filled = std::move(nextFilled);
                if (!anyGrew)
                    break;
            }
        }
    }
}
