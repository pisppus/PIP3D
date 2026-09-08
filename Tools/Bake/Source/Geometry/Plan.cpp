#include "Geometry/Plan.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <vector>

#include "Core/Math.hpp"
#include "Core/Noise.hpp"
#include "Geometry/Packing.hpp"
#include "Core/Parallel.hpp"
#include "Trace/Bvh.hpp"

namespace pip3D
{
    namespace Bake
    {
        namespace
        {
            constexpr uint32_t kAtlasMaxDim = 4096;
            constexpr uint32_t kAtlasAlign = 8;
            constexpr uint32_t kProbeMinTp = 16;
            constexpr uint32_t kProbeMaxTp = 96;
            constexpr float kProbeTpFactor = 0.25f;
            constexpr uint32_t kProbeSunRays = 16;
            constexpr uint32_t kProbeSkyRays = 24;
            constexpr float kLumaSunWeight = 0.75f;
            constexpr float kLumaSkyWeight = 0.35f;
            constexpr float kLargeAreaFor1024 = 350.0f;
            constexpr uint32_t kLargeLmMax = 1024;
            constexpr float kWideRadiusThreshold = 8.0f;
            constexpr float kHugeRadiusThreshold = 10.0f;
            constexpr float kFloorSmallArea = 60.0f;
            constexpr float kFloorRange = 400.0f;
            constexpr float kFloorHugeArea = 400.0f;
            constexpr float kFloorWideArea = 180.0f;
            constexpr float kTpmShrinkFactor = 0.94f;
            constexpr float kTpmFloorMin = 4.0f;
            constexpr float kPi2 = 6.2831853f;

            inline uint32_t effectiveLmMax(const MeshCacheEntry &e, const BakeConfig &cfg, float maxRadius = 0.0f)
            {
                uint32_t cap = cfg.lmMax;
                if (e.uniqueArea > kLargeAreaFor1024)
                    cap = std::max(cap, kLargeLmMax);
                if (maxRadius > kHugeRadiusThreshold)
                    cap = std::max(cap, kLargeLmMax);
                if (cap < cfg.lmMin)
                    cap = cfg.lmMin;
                return cap;
            }

            inline float floorBoost(const MeshCacheEntry &e, const BakeConfig &cfg, float maxRadius = 0.0f)
            {
                const float area = e.uniqueArea;
                const bool isWide = maxRadius > kWideRadiusThreshold;
                float areaBoost = 1.0f;
                if (area > kFloorSmallArea)
                {
                    const float t = std::fmin(1.0f, (area - kFloorSmallArea) / kFloorRange);
                    areaBoost = 1.0f + t * (cfg.floorTexelBoost - 1.0f);
                }
                if (isWide)
                    areaBoost = std::fmax(areaBoost, std::sqrt(cfg.floorTexelBoost));
                if (area > kFloorHugeArea || (area > kFloorWideArea && isWide))
                    areaBoost = std::fmax(areaBoost, cfg.floorTexelBoost);
                return std::fmax(1.0f, areaBoost);
            }

            void probeChartGradients(const InstanceBakeData &ib, const ChartPlan &pl,
                                     const BakeBVH &bvh, const Vector3 &sunDirTo,
                                     const Vector3 &sunT1, const Vector3 &sunT2,
                                     const BakeConfig &cfg,
                                     const std::vector<StaticLightSrc> &statics,
                                     uint32_t threads,
                                     bool hasSun,
                                     std::vector<float> &gradOut)
            {
                const uint32_t chartCount = static_cast<uint32_t>(pl.chartFaces.size());
                if (chartCount == 0)
                    return;
                gradOut.assign(chartCount, 0.0f);

                const float rawTp = std::sqrt(std::fmax(1.0f, ib.meshCache->uniqueArea)) * cfg.texelsPerMeter * kProbeTpFactor;
                const uint32_t Tp = static_cast<uint32_t>(std::fmax(static_cast<float>(kProbeMinTp),
                                                                    std::fmin(static_cast<float>(kProbeMaxTp), rawTp)));
                const uint32_t rwp = std::clamp(static_cast<uint32_t>(std::ceil(pl.usedW * Tp)) + 2, 4u, 512u);
                const uint32_t rhp = std::clamp(static_cast<uint32_t>(std::ceil(pl.usedH * Tp)) + 2, 4u, 512u);

                const UnwrapResult uw = rasterizeCharts(pl, rwp, rhp);
                const size_t n = static_cast<size_t>(rwp) * rhp;
                std::vector<float> luma(n, 0.0f);

                auto probeRow = [&](uint32_t y)
                {
                    for (uint32_t x = 0; x < rwp; ++x)
                    {
                        const TexelHit &th = uw.texels[y * rwp + x];
                        if (th.face == 0xFFFFFFFFu)
                            continue;

                        const Vector3 &p0 = ib.worldPos[th.face * 3 + 0];
                        const Vector3 &p1 = ib.worldPos[th.face * 3 + 1];
                        const Vector3 &p2 = ib.worldPos[th.face * 3 + 2];
                        const float b0 = 1.0f - th.b1 - th.b2;
                        const Vector3 P = p0 * b0 + p1 * th.b1 + p2 * th.b2;

                        const Vector3 &n0 = ib.worldNrm[th.face * 3 + 0];
                        const Vector3 &n1 = ib.worldNrm[th.face * 3 + 1];
                        const Vector3 &n2 = ib.worldNrm[th.face * 3 + 2];
                        Vector3 N = n0 * b0 + n1 * th.b1 + n2 * th.b2;
                        const float nl = std::sqrt(N.x * N.x + N.y * N.y + N.z * N.z);
                        if (nl < 1e-6f)
                            continue;
                        N = N * (1.0f / nl);

                        const Vector3 sp = P + N * cfg.bias;
                        const float rot = Noise::blueNoise().atCh(static_cast<int32_t>(x), static_cast<int32_t>(y), 3) * kPi2;

                        Vector3 st1, st2;
                        tangentBasis(N, st1, st2);

                        float sunVis = 0.0f;
                        if (hasSun)
                        {
                            for (uint32_t s = 0; s < kProbeSunRays; ++s)
                            {
                                const Vector3 dir = sunDiskDir(sunDirTo, st1, st2, s, kProbeSunRays, rot, cfg.sunAngularRadius);
                                if (!bvh.occluded(sp, dir, cfg.aoMaxDist))
                                    ++sunVis;
                            }
                            sunVis /= static_cast<float>(kProbeSunRays);
                        }

                        float sky = 0.0f;
                        for (uint32_t s = 0; s < kProbeSkyRays; ++s)
                        {
                            const Vector3 dir = cosineHemiDir(N, st1, st2, s, kProbeSkyRays, rot);
                            BakeHit hit;
                            if (bvh.trace(sp, dir, cfg.aoMaxDist, hit))
                            {
                                const float nd = hit.t / cfg.aoMaxDist;
                                sky += 1.0f - (1.0f - nd) * (1.0f - nd);
                            }
                            else
                            {
                                sky += 1.0f;
                            }
                        }
                        sky /= static_cast<float>(kProbeSkyRays);

                        float stat = 0.0f;
                        for (const StaticLightSrc &sl : statics)
                        {
                            Vector3 toL = sl.pos - sp;
                            const float dSq = toL.x * toL.x + toL.y * toL.y + toL.z * toL.z;
                            if (dSq > sl.rangeSq || dSq < 1e-6f)
                                continue;
                            const float dist = std::sqrt(dSq);
                            toL = toL * (1.0f / dist);
                            if (toL.x * N.x + toL.y * N.y + toL.z * N.z <= 0.0f)
                                continue;
                            if (!bvh.occluded(sp, toL, dist - cfg.bias))
                                stat += std::fmin(1.0f, sl.color.x * 0.3f + sl.color.y * 0.5f + sl.color.z * 0.2f);
                        }

                        luma[y * rwp + x] = sunVis * kLumaSunWeight + sky * kLumaSkyWeight + stat;
                    }
                };

                parallelFor(rhp, threads, [&](uint32_t yBegin, uint32_t yEnd)
                            {
                                for (uint32_t y = yBegin; y < yEnd; ++y)
                                    probeRow(y); });

                std::vector<float> tmp(luma);
                for (uint32_t pass = 0; pass < 2; ++pass)
                {
                    for (uint32_t y = 0; y < rhp; ++y)
                        for (uint32_t x = 0; x < rwp; ++x)
                        {
                            const size_t idx = static_cast<size_t>(y) * rwp + x;
                            if (uw.texels[idx].face == 0xFFFFFFFFu)
                                continue;
                            float acc = 0.0f;
                            int cnt = 0;
                            for (int dy = -1; dy <= 1; ++dy)
                                for (int dx = -1; dx <= 1; ++dx)
                                {
                                    const int nx = static_cast<int>(x) + dx;
                                    const int ny = static_cast<int>(y) + dy;
                                    if (nx < 0 || ny < 0 || nx >= static_cast<int>(rwp) || ny >= static_cast<int>(rhp))
                                        continue;
                                    const size_t ni = static_cast<size_t>(ny) * rwp + nx;
                                    if (uw.texels[ni].face == 0xFFFFFFFFu)
                                        continue;
                                    acc += luma[ni];
                                    ++cnt;
                                }
                            tmp[idx] = cnt ? acc / static_cast<float>(cnt) : luma[idx];
                        }
                    luma.swap(tmp);
                }

                for (uint32_t y = 0; y < rhp; ++y)
                {
                    for (uint32_t x = 0; x < rwp; ++x)
                    {
                        const TexelHit &th = uw.texels[y * rwp + x];
                        if (th.face == 0xFFFFFFFFu)
                            continue;
                        const uint32_t c = pl.chartOf[th.face];
                        const float l = luma[y * rwp + x];
                        constexpr int kDx[2] = {1, 0};
                        constexpr int kDy[2] = {0, 1};
                        for (int d = 0; d < 2; ++d)
                        {
                            const int nx = static_cast<int>(x) + kDx[d];
                            const int ny = static_cast<int>(y) + kDy[d];
                            if (nx >= static_cast<int>(rwp) || ny >= static_cast<int>(rhp))
                                continue;
                            const size_t ni = static_cast<size_t>(ny) * rwp + nx;
                            if (uw.texels[ni].face == 0xFFFFFFFFu)
                                continue;
                            const float g = std::fabs(luma[ni] - l);
                            if (g > gradOut[c])
                                gradOut[c] = g;
                        }
                    }
                }
            }

            void applyTexelsPerMeter(std::map<Mesh *, MeshCacheEntry> &meshCache,
                                     const std::map<Mesh *, ChartPlan> &plans,
                                     const std::map<Mesh *, float> &meshMaxRadius,
                                     const BakeConfig &cfg, float tpm)
            {
                for (auto &kv : meshCache)
                {
                    MeshCacheEntry &e = kv.second;
                    auto itR = meshMaxRadius.find(kv.first);
                    const float r = (itR != meshMaxRadius.end()) ? itR->second : 0.0f;
                    const uint32_t effMax = effectiveLmMax(e, cfg, r);
                    const float boost = floorBoost(e, cfg, r);
                    float side = std::sqrt(std::fmax(1.0f, e.uniqueArea)) * tpm * boost;
                    side = std::fmax(static_cast<float>(cfg.lmMin), std::fmin(static_cast<float>(effMax), side));
                    const uint32_t T = std::max(4u, static_cast<uint32_t>(side + 0.5f));
                    const ChartPlan &pl = plans.at(kv.first);
                    const uint32_t rw = std::clamp(static_cast<uint32_t>(std::ceil(pl.usedW * T)) + 2, 4u, kAtlasMaxDim);
                    const uint32_t rh = std::clamp(static_cast<uint32_t>(std::ceil(pl.usedH * T)) + 2, 4u, kAtlasMaxDim);
                    e.rectW = rw;
                    e.rectH = rh;
                }
            }

            struct RectItem
            {
                uint32_t w, h;
                size_t bakeIdx;
                Mesh *mesh;
            };

            bool tryPackAtlasSorted(const std::vector<RectItem> &items, uint32_t aw, uint32_t ah,
                                    std::vector<InstanceBakeData> &bakes, BakeAtlas &atlas,
                                    std::map<Mesh *, bool> &rotatedOut)
            {
                MaxRectsPacker packer(aw, ah);
                std::map<Mesh *, bool> meshRotated;
                uint32_t maxX = 0, maxY = 0;
                std::vector<std::pair<uint32_t, uint32_t>> footprint(items.size());
                for (const RectItem &it : items)
                {

                    auto rit = meshRotated.find(it.mesh);
                    const bool locked = rit != meshRotated.end();
                    const bool lockedRot = locked && rit->second;

                    uint32_t px = 0, py = 0;
                    bool rot = false;
                    if (lockedRot)
                    {
                        if (!packer.insert(it.h, it.w, px, py, rot) || rot)
                            return false;
                    }
                    else if (locked)
                    {
                        if (!packer.insert(it.w, it.h, px, py, rot) || rot)
                            return false;
                    }
                    else if (!packer.insert(it.w, it.h, px, py, rot))
                        return false;

                    meshRotated.emplace(it.mesh, rot);
                    bakes[it.bakeIdx].ax = px;
                    bakes[it.bakeIdx].ay = py;

                    const bool transposed = rot || lockedRot;
                    footprint[it.bakeIdx] = transposed ? std::make_pair(it.h, it.w)
                                                       : std::make_pair(it.w, it.h);
                    maxX = std::max(maxX, px + (transposed ? it.h : it.w));
                    maxY = std::max(maxY, py + (transposed ? it.w : it.h));
                }

                int64_t nOverlap = 0;
                for (const RectItem &a : items)
                {
                    const uint32_t aw2 = bakes[a.bakeIdx].ax, ah2 = bakes[a.bakeIdx].ay;
                    const uint32_t fw2 = footprint[a.bakeIdx].first, fh2 = footprint[a.bakeIdx].second;
                    for (const RectItem &b : items)
                    {
                        if (a.bakeIdx >= b.bakeIdx)
                            continue;
                        const uint32_t bw2 = bakes[b.bakeIdx].ax, bh2 = bakes[b.bakeIdx].ay;
                        const uint32_t fw3 = footprint[b.bakeIdx].first, fh3 = footprint[b.bakeIdx].second;
                        const int64_t ox = static_cast<int64_t>(std::min(aw2 + fw2, bw2 + fw3)) -
                                           static_cast<int64_t>(std::max(aw2, bw2));
                        const int64_t oy = static_cast<int64_t>(std::min(ah2 + fh2, bh2 + fh3)) -
                                           static_cast<int64_t>(std::max(ah2, bh2));
                        if (ox > 0 && oy > 0)
                            ++nOverlap;
                    }
                }
                if (nOverlap > 0)
                {
                    std::printf("\033[91m[-] Atlas packer produced %lld overlapping rects (bin %ux%u)\033[0m\n",
                                static_cast<long long>(nOverlap), aw, ah);
                    return false;
                }
                rotatedOut = std::move(meshRotated);
                atlas.w = std::min(aw, (maxX + kAtlasAlign - 1) & ~(kAtlasAlign - 1));
                atlas.h = std::min(ah, (maxY + kAtlasAlign - 1) & ~(kAtlasAlign - 1));
                return true;
            }

            std::vector<std::pair<uint32_t, uint32_t>> buildCandidateDims(uint64_t needArea)
            {
                constexpr uint32_t kStep = 8;
                auto snapUp = [](uint32_t v) -> uint32_t
                { return (v + kStep - 1) / kStep * kStep; };

                std::vector<std::pair<uint32_t, uint32_t>> out;
                out.reserve(64);
                const uint64_t a0 = needArea + needArea / 10;
                uint64_t a = std::max<uint64_t>(64ull * 64ull, a0);
                constexpr uint64_t kCap = static_cast<uint64_t>(kAtlasMaxDim) * kAtlasMaxDim;
                constexpr float kRatios[] = {1.0f, 4.0f / 3.0f, 1.5f, 16.0f / 9.0f, 2.0f, 8.0f / 3.0f, 4.0f};

                for (uint32_t ladder = 0; ladder < 24 && a <= kCap; ++ladder)
                {
                    for (float rt : kRatios)
                    {
                        const uint32_t h = snapUp(static_cast<uint32_t>(std::sqrt(static_cast<double>(a) / rt)));
                        const uint32_t w = snapUp(static_cast<uint32_t>(std::ceil(static_cast<float>(h) * rt)));
                        if (w == 0 || h == 0 || w > kAtlasMaxDim || h > kAtlasMaxDim)
                            continue;
                        out.emplace_back(w, h);
                    }
                    a = a * 115ull / 100ull + 1024;
                }

                std::sort(out.begin(), out.end(), [](const std::pair<uint32_t, uint32_t> &A, const std::pair<uint32_t, uint32_t> &B)
                          {
                              const uint64_t arA = static_cast<uint64_t>(A.first) * A.second;
                              const uint64_t arB = static_cast<uint64_t>(B.first) * B.second;
                              if (arA != arB)
                                  return arA < arB;
                              const float la = std::fabs(std::log2(static_cast<float>(A.first) / static_cast<float>(A.second)));
                              const float lb = std::fabs(std::log2(static_cast<float>(B.first) / static_cast<float>(B.second)));
                              return la < lb; });
                out.erase(std::unique(out.begin(), out.end()), out.end());
                return out;
            }
        }

        void createChartPlans(const std::map<Mesh *, MeshCacheEntry> &meshCache,
                              std::map<Mesh *, ChartPlan> &plans,
                              const BakeConfig &cfg)
        {
            for (const auto &kv : meshCache)
            {
                const MeshCacheEntry &e = kv.second;
                const uint32_t effMax = effectiveLmMax(e, cfg);
                const float boost = floorBoost(e, cfg, 0.0f);
                float side = std::sqrt(std::fmax(1.0f, e.uniqueArea)) * cfg.texelsPerMeter * boost;
                side = std::fmax(static_cast<float>(cfg.lmMin), std::fmin(static_cast<float>(effMax), side));
                const uint32_t T = std::max(4u, static_cast<uint32_t>(side + 0.5f));
                plans[kv.first] = planCharts(e.localPos, e.indices, static_cast<uint32_t>(e.indices.size() / 3), T, false);
            }
        }

        bool buildAtlasPlan(const BakeBVH &bvh,
                            const SceneLighting &light,
                            const BakeConfig &cfg, uint32_t threads,
                            std::map<Mesh *, MeshCacheEntry> &meshCache,
                            std::map<Mesh *, ChartPlan> &plans,
                            std::vector<InstanceBakeData> &bakes,
                            BakeAtlas &atlas)
        {
            if (cfg.adaptiveTexels)
            {
                std::map<Mesh *, std::vector<float>> gradAccum;
                Vector3 sunT1, sunT2;
                tangentBasis(light.sunDirTo, sunT1, sunT2);
                for (auto &b : bakes)
                {
                    Mesh *m = b.inst->getMesh();
                    ChartPlan &pl = plans[m];
                    std::vector<float> grad(pl.chartFaces.size(), 0.0f);
                    probeChartGradients(b, pl, bvh, light.sunDirTo, sunT1, sunT2, cfg, light.staticLights, threads, light.hasSun, grad);
                    auto it = gradAccum.find(m);
                    if (it == gradAccum.end())
                        gradAccum.emplace(m, grad);
                    else
                        for (size_t c = 0; c < grad.size() && c < it->second.size(); ++c)
                            it->second[c] = std::fmax(it->second[c], grad[c]);
                }
                constexpr float kGradThr[] = {0.012f, 0.035f, 0.10f, 0.22f, 0.35f};
                constexpr float kGradMul[] = {0.25f, 0.50f, 0.75f, 1.00f, 1.25f, 1.50f};
                for (auto &kv : gradAccum)
                {
                    ChartPlan &pl = plans[kv.first];
                    const std::vector<float> &grad = kv.second;
                    for (size_t c = 0; c < pl.chartMul.size() && c < grad.size(); ++c)
                    {
                        const float g = grad[c];
                        float mul = kGradMul[5];
                        for (size_t t = 0; t < 5; ++t)
                        {
                            if (g < kGradThr[t])
                            {
                                mul = kGradMul[t];
                                break;
                            }
                        }
                        if (pl.chartDev[c] > 0.25f && mul < 0.75f)
                            mul = 0.75f;
                        pl.chartMul[c] = mul;
                    }
                    repackChartsWithMul(pl);
                }
            }

            const float tpmTarget = cfg.texelsPerMeter;
            const float tpmFloor = std::fmin(tpmTarget, std::fmax(kTpmFloorMin, cfg.minTexelsPerMeter));

            std::map<Mesh *, float> meshMaxRadius;
            for (auto &b : bakes)
            {
                if (!b.inst || !b.meshCache)
                    continue;
                Mesh *m = b.inst->getMesh();
                const float r = b.inst->radius();
                auto it = meshMaxRadius.find(m);
                if (it == meshMaxRadius.end() || r > it->second)
                    meshMaxRadius[m] = r;
            }

            std::map<Mesh *, bool> rotatedMeshes;

            auto packAt = [&](float tpmScale) -> bool
            {
                applyTexelsPerMeter(meshCache, plans, meshMaxRadius, cfg, tpmScale);
                uint64_t needArea = 0;
                for (const auto &b : bakes)
                    needArea += static_cast<uint64_t>(b.meshCache->rectW) * b.meshCache->rectH;
                auto dims = buildCandidateDims(needArea);

                std::vector<RectItem> base;
                base.reserve(bakes.size());
                for (size_t bi = 0; bi < bakes.size(); ++bi)
                    base.push_back({bakes[bi].meshCache->rectW, bakes[bi].meshCache->rectH,
                                    bi, bakes[bi].inst->getMesh()});

                using RectLess = bool (*)(const RectItem &, const RectItem &);
                constexpr RectLess orderings[3] = {
                    [](const RectItem &a, const RectItem &b)
                    {
                        const uint32_t am = std::max(a.w, a.h), bm = std::max(b.w, b.h);
                        if (am != bm)
                            return am > bm;
                        return a.w * a.h > b.w * b.h;
                    },
                    [](const RectItem &a, const RectItem &b)
                    {
                        const uint64_t aa = static_cast<uint64_t>(a.w) * a.h;
                        const uint64_t ab = static_cast<uint64_t>(b.w) * b.h;
                        if (aa != ab)
                            return aa > ab;
                        return std::max(a.w, a.h) > std::max(b.w, b.h);
                    },
                    [](const RectItem &a, const RectItem &b)
                    {
                        if (a.h != b.h)
                            return a.h > b.h;
                        return a.w > b.w;
                    },
                };

                std::vector<RectItem> items;
                for (const auto &d : dims)
                {
                    for (const auto &cmp : orderings)
                    {
                        items = base;
                        std::sort(items.begin(), items.end(), cmp);
                        if (tryPackAtlasSorted(items, d.first, d.second, bakes, atlas, rotatedMeshes))
                            return true;
                    }
                }
                return false;
            };

            bool planned = false;
            float tpm = tpmTarget;
            while (true)
            {
                if (packAt(tpm))
                {
                    planned = true;
                    break;
                }
                if (tpm <= tpmFloor)
                    break;
                tpm = std::fmax(tpmFloor, tpm * kTpmShrinkFactor);
            }

            float tpmFinal = tpm;

            if (!planned)
            {
                std::printf("\033[91m[-] Scene too large for %ux%u atlas even at %.1f texels/m. Reduce PIP3D_BAKE_TEXELS.\033[0m\n",
                            kAtlasMaxDim, kAtlasMaxDim, tpmFloor);
                return false;
            }

            for (const auto &kv : rotatedMeshes)
            {
                if (!kv.second)
                    continue;
                auto it = meshCache.find(kv.first);
                if (it != meshCache.end())
                    std::swap(it->second.rectW, it->second.rectH);
            }

            for (auto &kv : meshCache)
            {
                MeshCacheEntry &e = kv.second;
                if (e.unwrapped)
                    continue;
                e.unwrap = rasterizeCharts(plans[kv.first], e.rectW, e.rectH);
                e.unwrapped = true;
            }

            atlas.data.assign(static_cast<size_t>(atlas.w) * atlas.h, 0);
            atlas.f32.assign(static_cast<size_t>(atlas.w) * atlas.h * 3, 0.0f);

            if (cfg.adaptiveTexels)
            {
                size_t nQ = 0, nHalf = 0, nThreeQ = 0, nFull = 0, nBoost = 0;
                for (const auto &kv : plans)
                    for (const float m : kv.second.chartMul)
                    {
                        if (m <= 0.25f)
                            ++nQ;
                        else if (m <= 0.5f)
                            ++nHalf;
                        else if (m <= 0.75f)
                            ++nThreeQ;
                        else if (m <= 1.0f)
                            ++nFull;
                        else
                            ++nBoost;
                    }
                std::printf("\033[36m[Pip3D]\033[0m Adaptive texels: charts 0.25x=%zu, 0.5x=%zu, 0.75x=%zu, 1.0x=%zu, boost(1.25..1.5x)=%zu\n",
                            nQ, nHalf, nThreeQ, nFull, nBoost);
            }

            uint64_t usedPx = 0;
            for (const auto &b : bakes)
                usedPx += static_cast<uint64_t>(b.meshCache->rectW) * b.meshCache->rectH;
            std::printf("\033[36m[Pip3D]\033[0m Atlas plan: \033[1m%ux%u\033[0m (%.1f%% packed, %.1f texels/m)\n",
                        atlas.w, atlas.h,
                        atlas.w * atlas.h > 0 ? 100.0 * static_cast<double>(usedPx) / static_cast<double>(atlas.w * atlas.h) : 0.0,
                        tpmFinal);

            return true;
        }
    }
}
