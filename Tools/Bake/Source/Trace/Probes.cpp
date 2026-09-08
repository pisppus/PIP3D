#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "Trace/Probes.hpp"
#include "Core/Noise.hpp"
#include "Core/Parallel.hpp"
#include "Core/Math.hpp"
#include "Core/Progress.hpp"

namespace pip3D
{
    namespace Bake
    {
        namespace
        {
            struct ProbeCtx
            {
                const BakeBVH *bvh;
                Vector3 sunDirTo;
                Vector3 sunCol;
                Color skyTop, skyHor, skyGnd;
                float skyLevel;
                float skyNeutralize;
                float sunRadius;
                float aoMaxDist;
                float bias;
                uint32_t sunRays;
                uint32_t skyRays;
                uint32_t giRays;
                uint32_t giSkyNee;
                float giGain;
                float giMaxAlbedo;
                float giDesat;
                const std::vector<StaticLightSrc> *statics;
                const std::vector<EmissiveTriLight> *emissives;
                float emissiveTotalArea;
                uint32_t emissiveSamples;
                float wrapTerm;
                float diffScale;
                float smoothEps;
                bool hasSun;
            };

            void encodeProbes(ProbeBake &out)
            {
                Vector3 sumCol(0.0f, 0.0f, 0.0f);
                float sumLuma = 0.0f, maxLuma = 0.0f;
                for (size_t i = 0; i < out.lumaBuf.size(); ++i)
                {
                    const float l = out.lumaBuf[i];
                    if (l > 1e-6f)
                    {
                        sumCol = sumCol + Vector3(out.colBuf[i * 3 + 0],
                                                  out.colBuf[i * 3 + 1],
                                                  out.colBuf[i * 3 + 2]);
                        sumLuma += l;
                    }
                    if (l > maxLuma)
                        maxLuma = l;
                }
                const float kStatic = std::fmax(0.05f, maxLuma);
                out.staticScale = kStatic;
                if (sumLuma > 1e-5f)
                    out.staticTint = sumCol * (1.0f / sumLuma);

                const Noise::BlueNoise &bn = Noise::blueNoise();
                const uint32_t dx = out.dx, dy = out.dy;
                const uint32_t dxy = dx * dy;
                for (size_t i = 0; i < out.probes.size(); ++i)
                {
                    const uint32_t x = static_cast<uint32_t>(i % dx);
                    const uint32_t y = static_cast<uint32_t>((i / dx) % dy);
                    const uint32_t z = static_cast<uint32_t>(i / dxy);
                    const int32_t bx = static_cast<int32_t>(x & 63);
                    const int32_t by = static_cast<int32_t>((y ^ (z * 17u)) & 63);
                    const float v = std::fmax(0.0f, std::fmin(1.0f, out.lumaBuf[i] / kStatic));
                    const uint32_t b5 = Noise::quantDither(v, 32.0f, bn.at(bx, by));
                    out.probes[i] = static_cast<uint16_t>(out.probes[i] | b5);
                }
            }
        }

        Vector3 computeProbeGrid(const BakeBVH &bvh, const BakeConfig &cfg,
                                 uint32_t &outDx, uint32_t &outDy, uint32_t &outDz)
        {
            Vector3 mn(1e30f, 1e30f, 1e30f), mx(-1e30f, -1e30f, -1e30f);
            for (const BakeTri &t : bvh.tris())
            {
                const Vector3 p1 = t.v0 + t.e1, p2 = t.v0 + t.e2;
                mn = Vector3(std::fmin(mn.x, std::fmin(t.v0.x, std::fmin(p1.x, p2.x))),
                             std::fmin(mn.y, std::fmin(t.v0.y, std::fmin(p1.y, p2.y))),
                             std::fmin(mn.z, std::fmin(t.v0.z, std::fmin(p1.z, p2.z))));
                mx = Vector3(std::fmax(mx.x, std::fmax(t.v0.x, std::fmax(p1.x, p2.x))),
                             std::fmax(mx.y, std::fmax(t.v0.y, std::fmax(p1.y, p2.y))),
                             std::fmax(mx.z, std::fmax(t.v0.z, std::fmax(p1.z, p2.z))));
            }

            const float cell = cfg.probeCell;
            const float cellY = cell * ProbeTrace::kAnisoY;
            mn = mn - Vector3(cell * 0.5f, cellY * 0.5f, cell * 0.5f);
            mx = mx + Vector3(cell * 0.5f, cellY * 0.5f, cell * 0.5f);

            outDx = std::clamp(static_cast<uint32_t>((mx.x - mn.x) / cell) + 1, 4u, 64u);
            outDy = std::clamp(static_cast<uint32_t>((mx.y - mn.y) / cellY) + 3, 4u, 32u);
            outDz = std::clamp(static_cast<uint32_t>((mx.z - mn.z) / cell) + 1, 4u, 64u);
            return mn;
        }

        void bakeProbes(const BakeBVH &bvh, const SceneLighting &light,
                        const std::vector<EmissiveTriLight> &emissives,
                        float emissiveTotalArea,
                        const BakeConfig &cfg, uint32_t threads,
                        ProbeBake &out)
        {
            const float cell = cfg.probeCell;
            const float cellY = cell * ProbeTrace::kAnisoY;
            uint32_t dx, dy, dz;
            const Vector3 mn = computeProbeGrid(bvh, cfg, dx, dy, dz);

            out.valid = true;
            out.origin = mn;
            out.cell = cell;
            out.cellY = cellY;
            out.dx = dx;
            out.dy = dy;
            out.dz = dz;
            out.probes.assign(static_cast<size_t>(dx) * dy * dz, 0);
            out.lumaBuf.assign(out.probes.size(), 0.0f);
            out.colBuf.assign(out.probes.size() * 3, 0.0f);

            ProbeCtx pc;
            pc.bvh = &bvh;
            pc.sunDirTo = light.sunDirTo;
            pc.sunCol = light.sunCol;
            pc.skyTop = light.skyTop;
            pc.skyHor = light.skyHor;
            pc.skyGnd = light.skyGnd;
            pc.skyLevel = light.skyLevel;
            pc.skyNeutralize = light.skyNeut;
            pc.sunRadius = cfg.sunAngularRadius;
            pc.aoMaxDist = cfg.aoMaxDist;
            pc.bias = cfg.bias;
            pc.sunRays = std::max(1u, cfg.sunRays / 16);
            pc.skyRays = cfg.probeSkyRays ? cfg.probeSkyRays : 32u;
            pc.giRays = std::max(1u, cfg.giRays / 16);
            pc.giSkyNee = cfg.giSkyNeeRays ? cfg.giSkyNeeRays : 8u;
            pc.giGain = cfg.giGain;
            pc.giMaxAlbedo = cfg.giMaxAlbedo;
            pc.giDesat = cfg.giDesat;
            pc.statics = &light.staticLights;
            pc.emissives = &emissives;
            pc.emissiveTotalArea = emissiveTotalArea;
            pc.emissiveSamples = cfg.emissiveSamples;
            pc.wrapTerm = light.wrapTerm;
            pc.diffScale = light.diffScale;
            pc.smoothEps = light.smoothEps;
            pc.hasSun = light.hasSun;

            const Vector3 up(0.0f, 1.0f, 0.0f);
            Vector3 upT1, upT2;
            tangentBasis(up, upT1, upT2);
            Vector3 sunT1, sunT2;
            tangentBasis(pc.sunDirTo, sunT1, sunT2);

            UnifiedProgressBar &bar = progressBar();

            std::atomic<uint64_t> probeProgress{0};
            parallelFor(dz, threads, [&](uint32_t z0, uint32_t z1)
                        {
                uint32_t rng = Noise::pcg3d(z0, 0xBEEFu, 12345u);
                uint64_t localAdv = 0;
                for (uint32_t z = z0; z < z1; ++z)
                {
                    for (uint32_t y = 0; y < dy; ++y)
                    {
                        for (uint32_t x = 0; x < dx; ++x)
                        {
                            const Vector3 pos(
                                mn.x + static_cast<float>(x) * cell,
                                mn.y + static_cast<float>(y) * cellY,
                                mn.z + static_cast<float>(z) * cell);

                            float sunVis = 0.0f;
                            if (pc.hasSun)
                            {
                                const float rot = rand01(rng) * 6.2831853f;
                                uint32_t vis = 0;
                                for (uint32_t s = 0; s < pc.sunRays; ++s)
                                {
                                    const Vector3 dir = sunDiskDir(
                                        pc.sunDirTo, sunT1, sunT2, s, pc.sunRays,
                                        rot, pc.sunRadius);
                                    if (!pc.bvh->occluded(pos, dir, pc.aoMaxDist))
                                        ++vis;
                                }
                                sunVis = static_cast<float>(vis) / static_cast<float>(pc.sunRays);
                            }

                            float skyAO = 0.0f;
                            {
                                const float rot = rand01(rng) * 6.2831853f;
                                for (uint32_t s = 0; s < pc.skyRays; ++s)
                                {
                                    const Vector3 dir = cosineHemiDir(
                                        up, upT1, upT2, s, pc.skyRays, rot);
                                    BakeHit hit;
                                    if (pc.bvh->trace(pos, dir, pc.aoMaxDist, hit))
                                    {
                                        const float normDist = hit.t / pc.aoMaxDist;
                                        const float occ = (1.0f - normDist) * (1.0f - normDist);
                                        skyAO += (1.0f - occ);
                                    }
                                    else
                                    {
                                        skyAO += 1.0f;
                                    }
                                }
                                skyAO /= static_cast<float>(pc.skyRays);
                            }

                            float giSun = 0.0f, giSky = 0.0f;
                            if (pc.giRays > 0)
                            {
                                const float rot = rand01(rng) * 6.2831853f;
                                for (uint32_t s = 0; s < pc.giRays; ++s)
                                {
                                    const Vector3 dir = cosineHemiDir(
                                        up, upT1, upT2, s, pc.giRays, rot);
                                    BakeHit hit;
                                    if (!pc.bvh->trace(pos, dir, 250.0f, hit))
                                        continue;
                                    const BakeTri &ht = pc.bvh->tris()[hit.tri];
                                    Vector3 hn = ht.gn;
                                    if (hn.x * dir.x + hn.y * dir.y + hn.z * dir.z > 0.0f)
                                        hn = hn * -1.0f;
                                    const Vector3 hp = pos + dir * hit.t + hn * pc.bias;

                                    float ar, ag, ab;
                                    bakeAlbedoAt(ht, hit.u, hit.v, ar, ag, ab);
                                    giAlbedoControl(ar, ag, ab, pc.giMaxAlbedo, pc.giDesat);
                                    const float albedoLum = Noise::lum709(ar, ag, ab);

                                    const float hndl = hn.x * pc.sunDirTo.x +
                                                       hn.y * pc.sunDirTo.y +
                                                       hn.z * pc.sunDirTo.z;
                                    const float hndlW = smoothPositiveRT(
                                        hndl + pc.wrapTerm, pc.smoothEps) * pc.diffScale;
                                    if (hndlW > 1e-4f && pc.hasSun)
                                    {
                                        const Vector3 sd = sunJitterDir(pc.sunDirTo, sunT1, sunT2, rng, pc.sunRadius);
                                        if (!pc.bvh->occluded(hp, sd, pc.aoMaxDist))
                                            giSun += albedoLum * hndlW;
                                    }

                                    {
                                        Vector3 ht1, ht2;
                                        tangentBasis(hn, ht1, ht2);
                                        const float hrot = rand01(rng) * 6.2831853f;
                                        uint32_t open = 0;
                                        for (uint32_t k = 0; k < pc.giSkyNee; ++k)
                                        {
                                            const Vector3 d = cosineHemiDir(hn, ht1, ht2, k, pc.giSkyNee, hrot);
                                            if (!pc.bvh->occluded(hp, d, pc.aoMaxDist))
                                                ++open;
                                        }
                                        giSky += albedoLum * static_cast<float>(open) /
                                                 static_cast<float>(pc.giSkyNee);
                                    }
                                }
                                giSun /= static_cast<float>(pc.giRays);
                                giSky /= static_cast<float>(pc.giRays);
                                giSun = std::fmin(giSun, 1.0f);
                                giSky = std::fmin(giSky, 1.0f);
                            }

                            Vector3 staticCol(0.0f, 0.0f, 0.0f);
                            for (const StaticLightSrc &sl : *pc.statics)
                            {
                                Vector3 toL = sl.pos - pos;
                                const float dSq = toL.x * toL.x + toL.y * toL.y + toL.z * toL.z;
                                if (dSq > sl.rangeSq || dSq < 1e-6f)
                                    continue;
                                const float dist = std::sqrt(dSq);
                                toL = toL * (1.0f / dist);
                                if (pc.bvh->occluded(pos, toL, dist - pc.bias))
                                    continue;
                                const float atten = 1.0f / (1.0f + dSq * sl.invRangeSq);
                                staticCol = staticCol + sl.color * (atten);
                            }
                            staticCol = staticCol + sampleEmissiveTris(
                                pos, nullptr, *pc.emissives, pc.emissiveTotalArea,
                                pc.emissiveSamples, pc.bias, rng, *pc.bvh);

                            const float luma = Noise::lum709(staticCol.x, staticCol.y, staticCol.z);
                            const float rTotal = std::fmin(1.0f, sunVis + giSun * pc.giGain);
                            const float gTotal = std::fmin(1.0f, skyAO + giSky * pc.giGain);
                            const uint32_t r5 = static_cast<uint32_t>(rTotal * 31.0f + 0.5f);
                            const uint32_t g6 = static_cast<uint32_t>(gTotal * 63.0f + 0.5f);
                            const size_t idx = (static_cast<size_t>(z) * out.dy + y) * out.dx + x;
                            out.probes[idx] = static_cast<uint16_t>((r5 << 11) | (g6 << 5));
                            out.lumaBuf[idx] = luma;
                            out.colBuf[idx * 3 + 0] = staticCol.x;
                            out.colBuf[idx * 3 + 1] = staticCol.y;
                            out.colBuf[idx * 3 + 2] = staticCol.z;

                            localAdv += 64;
                        }
                    }
                    if (localAdv >= 64 * 64)
                    {
                        bar.advance(localAdv, "Probes Grid");
                        localAdv = 0;
                    }
                }
                if (localAdv) bar.advance(localAdv, "Probes Grid"); });

            {
                const uint32_t N = dx * dy * dz;
                std::vector<uint8_t> valid(N, 1);
                const float testDist = cell * ProbeTrace::kInteriorTestFactor;
                const Vector3 dirs[6] = {Vector3(1, 0, 0), Vector3(-1, 0, 0), Vector3(0, 1, 0),
                                         Vector3(0, -1, 0), Vector3(0, 0, 1), Vector3(0, 0, -1)};
                uint32_t invalidCnt = 0;
                for (uint32_t z = 0; z < dz; ++z)
                    for (uint32_t y = 0; y < dy; ++y)
                        for (uint32_t x = 0; x < dx; ++x)
                        {
                            const size_t idx = (static_cast<size_t>(z) * dy + y) * dx + x;
                            const Vector3 pos(mn.x + static_cast<float>(x) * cell,
                                              mn.y + static_cast<float>(y) * cellY,
                                              mn.z + static_cast<float>(z) * cell);
                            if (y == 0)
                            {
                                valid[idx] = 0;
                                ++invalidCnt;
                                continue;
                            }
                            const uint16_t c = out.probes[idx];
                            const float sky = static_cast<float>((c >> 5) & 0x3F) * (1.0f / 63.0f);
                            const float sun = static_cast<float>((c >> 11) & 0x1F) * (1.0f / 31.0f);
                            if (sky > ProbeTrace::kSkyOpenThreshold || sun > ProbeTrace::kSunOpenThreshold)
                                continue;
                            uint32_t hits = 0;
                            for (int d = 0; d < 6; ++d)
                            {
                                BakeHit hit;
                                if (bvh.trace(pos, dirs[d], testDist, hit) && hit.t < testDist)
                                    ++hits;
                            }
                            if (hits == 6)
                            {
                                valid[idx] = 0;
                                ++invalidCnt;
                            }
                        }
                if (invalidCnt)
                {
                    const uint32_t invalidStart = invalidCnt;

                    std::vector<uint16_t> srcProbes = out.probes;
                    std::vector<float> srcLuma = out.lumaBuf;
                    std::vector<float> srcCol = out.colBuf;
                    for (uint32_t iter = 0; iter < 4 && invalidCnt; ++iter)
                    {
                        uint32_t filled = 0;
                        for (uint32_t z = 0; z < dz; ++z)
                            for (uint32_t y = 0; y < dy; ++y)
                                for (uint32_t x = 0; x < dx; ++x)
                                {
                                    const size_t idx = (static_cast<size_t>(z) * dy + y) * dx + x;
                                    if (valid[idx])
                                        continue;
                                    uint32_t bestDist = 999;
                                    size_t best = SIZE_MAX;
                                    for (int dz2 = -2; dz2 <= 2; ++dz2)
                                        for (int dy2 = -2; dy2 <= 2; ++dy2)
                                            for (int dx2 = -2; dx2 <= 2; ++dx2)
                                            {
                                                if (!dx2 && !dy2 && !dz2)
                                                    continue;
                                                const int nx = static_cast<int>(x) + dx2;
                                                const int ny = static_cast<int>(y) + dy2;
                                                const int nz = static_cast<int>(z) + dz2;
                                                if (nx < 0 || ny < 0 || nz < 0 ||
                                                    nx >= static_cast<int>(dx) || ny >= static_cast<int>(dy) || nz >= static_cast<int>(dz))
                                                    continue;
                                                const size_t nIdx = (static_cast<size_t>(nz) * dy + ny) * dx + nx;
                                                if (!valid[nIdx])
                                                    continue;
                                                const uint32_t d = static_cast<uint32_t>(std::abs(dx2) + std::abs(dy2) + std::abs(dz2));
                                                if (d < bestDist)
                                                {
                                                    bestDist = d;
                                                    best = nIdx;
                                                    if (d == 1)
                                                        break;
                                                }
                                            }
                                    if (best != SIZE_MAX)
                                    {
                                        out.probes[idx] = srcProbes[best];
                                        out.lumaBuf[idx] = srcLuma[best];
                                        out.colBuf[idx * 3 + 0] = srcCol[best * 3 + 0];
                                        out.colBuf[idx * 3 + 1] = srcCol[best * 3 + 1];
                                        out.colBuf[idx * 3 + 2] = srcCol[best * 3 + 2];
                                        valid[idx] = 2;
                                        ++filled;
                                    }
                                }
                        for (size_t i = 0; i < N; ++i)
                            if (valid[i] == 2)
                                valid[i] = 1;
                        srcProbes = out.probes;
                        srcLuma = out.lumaBuf;
                        srcCol = out.colBuf;
                        invalidCnt -= filled;
                        if (!filled)
                            break;
                    }
                    progressBar().logf("\033[36m[Pip3D]\033[0m Probes: filled %u enclosed interior cells, %u remain invalid",
                                       static_cast<unsigned>(invalidStart - invalidCnt),
                                       static_cast<unsigned>(invalidCnt));
                }
            }

            encodeProbes(out);
        }
    }
}
