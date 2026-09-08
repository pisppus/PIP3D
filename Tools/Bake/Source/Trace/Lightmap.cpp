#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>

#include "Trace/Lightmap.hpp"
#include "Core/Noise.hpp"
#include "Trace/Denoise.hpp"
#include "Core/Math.hpp"
#include "Core/Progress.hpp"
#include "GPU/Dispatch.hpp"

namespace pip3D
{
    namespace Bake
    {
        namespace
        {

            void encodeLightmap(InstanceBakeData &ib, uint32_t w, uint32_t h,
                                bool finalMode, const ToneParams &tp, bool dither)
            {
                const size_t n = static_cast<size_t>(w) * h;
                ib.lm.resize(n);
                ib.enc.resize(n * 3);

                if (finalMode)
                {
                    for (uint32_t y = 0; y < h; ++y)
                    {
                        for (uint32_t x = 0; x < w; ++x)
                        {
                            const size_t i = static_cast<size_t>(y) * w + x;
                            float r = ib.den[i * 3 + 0];
                            float g = ib.den[i * 3 + 1];
                            float b = ib.den[i * 3 + 2];
                            toneMapRT(r, g, b, tp);
                            r = std::fmax(0.0f, std::fmin(1.0f, r));
                            g = std::fmax(0.0f, std::fmin(1.0f, g));
                            b = std::fmax(0.0f, std::fmin(1.0f, b));
                            float *e = &ib.enc[i * 3];
                            e[0] = r;
                            e[1] = g;
                            e[2] = b;
                            ib.lm[i] = pack565Q(r, g, b, static_cast<int32_t>(x),
                                                static_cast<int32_t>(y), dither);
                        }
                    }
                }
                else
                {

                    const float kStatic = std::fmax(0.05f, ib.staticMaxLuma);
                    ib.staticScale = kStatic;
                    if (ib.staticSumLuma > 1e-5f)
                        ib.staticSumCol = ib.staticSumCol * (1.0f / ib.staticSumLuma);

                    const Noise::BlueNoise &bn = Noise::blueNoise();
                    for (uint32_t y = 0; y < h; ++y)
                    {
                        for (uint32_t x = 0; x < w; ++x)
                        {
                            const size_t i = static_cast<size_t>(y) * w + x;
                            const float sunVis = ib.den[i * 3 + 0];
                            const float skyAO = ib.den[i * 3 + 1];
                            const float luma = ib.den[i * 3 + 2];

                            auto q = [&](float v, float levels, uint32_t ch) -> uint32_t
                            {
                                v = std::fmax(0.0f, std::fmin(1.0f, v));
                                const float bn01 = dither ? bn.atCh(static_cast<int32_t>(x),
                                                                    static_cast<int32_t>(y), ch)
                                                          : 0.5f;
                                return Noise::quantDither(v, levels, bn01);
                            };

                            const float rv = std::fmax(0.0f, std::fmin(1.0f, sunVis));
                            const float gv = std::fmax(0.0f, std::fmin(1.0f, skyAO));
                            const float bv = std::fmax(0.0f, std::fmin(1.0f, luma / kStatic));
                            float *e = &ib.enc[i * 3];
                            e[0] = rv;
                            e[1] = gv;
                            e[2] = bv;

                            const uint32_t r5 = q(rv, 32.0f, 0);
                            const uint32_t g6 = q(gv, 64.0f, 1);
                            const uint32_t b5 = q(bv, 32.0f, 2);
                            ib.lm[i] = static_cast<uint16_t>((r5 << 11) | (g6 << 5) | b5);
                        }
                    }
                }
            }
        }

        void bakeInstanceLightmap(InstanceBakeData &ib, size_t bakeIdx, size_t bakeCount,
                                  const BakeBVH &bvh, const SceneLighting &light,
                                  const std::vector<EmissiveTriLight> &emissives,
                                  float emissiveTotalArea,
                                  const BakeConfig &cfg, bool finalMode)
        {
            const UnwrapResult &uw = ib.meshCache->unwrap;
            const uint32_t rw = uw.rectW;
            const uint32_t rh = uw.rectH;
            const size_t texelCount = static_cast<size_t>(rw) * rh;
            const float texelMeters = std::fmax(
                1e-4f, std::sqrt(uw.metersPerTexelU * uw.metersPerTexelV));

            ib.lm.assign(texelCount, 0);
            ib.den.assign(texelCount * 3, 0.0f);
            ib.varLuma.assign(texelCount, 0.0f);
            ib.texelPos.assign(texelCount, Vector3(0.0f, 0.0f, 0.0f));
            ib.texelNrm.assign(texelCount, Vector3(0.0f, 1.0f, 0.0f));
            ib.staticLuma.assign(texelCount, 0.0f);
            ib.staticCol.assign(texelCount, Vector3(0.0f, 0.0f, 0.0f));

            char currentStatus[48];
            std::snprintf(currentStatus, sizeof(currentStatus), "(%zu/%zu) LM %ux%u",
                          bakeIdx + 1, bakeCount, rw, rh);

            Vector3 sunT1, sunT2;
            tangentBasis(light.sunDirTo, sunT1, sunT2);

            for (uint32_t y = 0; y < rh; ++y)
            {
                for (uint32_t x = 0; x < rw; ++x)
                {
                    const TexelHit &th = uw.texels[y * rw + x];
                    if (th.face == 0xFFFFFFFFu)
                        continue;

                    const Vector3 &p0 = ib.worldPos[th.face * 3 + 0];
                    const Vector3 &p1 = ib.worldPos[th.face * 3 + 1];
                    const Vector3 &p2 = ib.worldPos[th.face * 3 + 2];
                    const float b0 = 1.0f - th.b1 - th.b2;
                    const Vector3 pos = p0 * b0 + p1 * th.b1 + p2 * th.b2;

                    const Vector3 &n0 = ib.worldNrm[th.face * 3 + 0];
                    const Vector3 &n1 = ib.worldNrm[th.face * 3 + 1];
                    const Vector3 &n2 = ib.worldNrm[th.face * 3 + 2];
                    Vector3 nrm = n0 * b0 + n1 * th.b1 + n2 * th.b2;
                    const float nl = std::sqrt(nrm.x * nrm.x + nrm.y * nrm.y + nrm.z * nrm.z);
                    if (nl < 1e-6f)
                        continue;
                    nrm = nrm * (1.0f / nl);
                    Vector3 gn = cross3(p1 - p0, p2 - p0);
                    const float gl = std::sqrt(gn.x * gn.x + gn.y * gn.y + gn.z * gn.z);
                    if (gl > 1e-9f)
                    {
                        gn = gn * (1.0f / gl);
                        const float agree = nrm.x * gn.x + nrm.y * gn.y + nrm.z * gn.z;
                        if (agree < 0.866f)
                            nrm = (agree < 0.0f) ? gn * -1.0f : gn;
                    }

                    ib.texelPos[y * rw + x] = pos;
                    ib.texelNrm[y * rw + x] = nrm;
                }
            }

            const bool skipRays = (std::getenv("PIP3D_BAKE_NORAY") != nullptr);

            if (!skipRays)
            {
                Gpu::GpuBaker &gpu = Gpu::globalGpuBaker();
                if (!gpu.bakeTexels(cfg, bvh, ib, rw, rh, light,
                                    sunT1, sunT2, emissives, emissiveTotalArea,
                                    finalMode, currentStatus))
                {
                    throw std::runtime_error(
                        "GPU dispatch failed while baking instance " + std::to_string(bakeIdx + 1) + "/" + std::to_string(bakeCount) + " - no CPU fallback, bake aborted");
                }
            }
            else
            {
                for (uint32_t y = 0; y < rh; ++y)
                    progressBar().advance(rw, currentStatus);
            }

            ib.validTexels = 0;
            size_t highVar = 0;
            for (size_t i = 0; i < texelCount; ++i)
            {
                if (uw.texels[i].face == 0xFFFFFFFFu)
                    continue;
                ++ib.validTexels;
                if (ib.varLuma[i] > 0.008f)
                    ++highVar;
                const float luma = ib.den[i * 3 + 2];
                if (finalMode)
                    continue;
                if (luma > 1e-6f)
                {
                    ib.staticSumCol = ib.staticSumCol + ib.staticCol[i];
                    ib.staticSumLuma += luma;
                    if (luma > ib.staticMaxLuma)
                        ib.staticMaxLuma = luma;
                }
            }
            uint32_t denoisePasses = std::clamp(cfg.denoisePasses, 1u, 4u);
            if (highVar * 8 > ib.validTexels)
                denoisePasses = std::min(4u, denoisePasses + 1u);

            if (cfg.denoisePasses > 0)
                atrousDenoise(ib.den, ib.varLuma, ib.texelPos, ib.texelNrm,
                              uw.texels, rw, rh, denoisePasses,
                              texelMeters);
            if (cfg.lightmapBlurPasses > 0)
                blurLightmap(ib.den, ib.texelPos, ib.texelNrm, uw.texels, rw, rh,
                             std::clamp(cfg.lightmapBlurPasses, 1u, 3u),
                             cfg.lightmapBlurSigma, texelMeters);
            dilateChannelsRect(ib.den, uw.texels, rw, rh, 32);
            encodeLightmap(ib, rw, rh, finalMode, light.tone, cfg.dither);
        }
    }
}
