#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <thread>
#include <vector>

#include "Core/Config.hpp"
#include "Geometry/Plan.hpp"
#include "Trace/Bvh.hpp"
#include "Trace/Lightmap.hpp"
#include "Trace/Probes.hpp"
#include "Encode/Atlas.hpp"
#include "Encode/Preview.hpp"
#include "Core/Math.hpp"
#include "Core/Progress.hpp"
#include "GPU/Dispatch.hpp"
#include <Pip3D.hpp>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace pip3d_fs = std::filesystem;

namespace pip3D
{
    namespace Bake
    {
        int runBake(Renderer &r, std::vector<MeshInstance *> &instances,
                    const BakeConfig &cfg)
        {
            const auto tStart = std::chrono::steady_clock::now();

#if defined(_WIN32)
            if (!std::getenv("PIP3D_BAKE_LOG"))
            {
                std::freopen("CONOUT$", "w", stdout);
                SetConsoleOutputCP(CP_UTF8);
                SetConsoleCP(CP_UTF8);
            }
#else
            std::freopen("/dev/tty", "w", stdout);
#endif

            {
                pip3d_fs::path outP(cfg.outDir);
                pip3d_fs::path outAbs = pip3d_fs::absolute(outP);

                if (outAbs.string().size() <= 3 || outAbs == pip3d_fs::current_path())
                {
                    std::printf("\033[91m[-] Refusing to clean unsafe outDir: %s\033[0m\n", cfg.outDir);
                    return 1;
                }
                if (pip3d_fs::exists(outAbs))
                {
                    std::error_code ec;
                    pip3d_fs::remove_all(outAbs, ec);
                }
                pip3d_fs::create_directories(outAbs);
            }

            BakedLightMode mode = cfg.mode;
            if (cfg.modeAuto)
            {
                const BakedLightMode rm = r.getBakedLightMode();
                if (rm != BakedLightMode::OFF)
                    mode = rm;
            }
            if (mode == BakedLightMode::OFF)
            {
                std::printf("\033[33m[!] Mode is OFF - nothing to bake.\033[0m\n");
                return 0;
            }
            const bool finalMode = (mode == BakedLightMode::FINAL);

            uint32_t hwThreads = std::max(1u, std::thread::hardware_concurrency());
            if (const char *tv = std::getenv("PIP3D_BAKE_THREADS"))
            {
                const int t = std::atoi(tv);
                if (t > 0)
                    hwThreads = static_cast<uint32_t>(t);
            }

            std::printf("\033[36m[Pip3D]\033[0m Baking scene '\033[1m%s\033[0m' (mode: \033[32m%s\033[0m, threads: %u, out: %s)\n",
                        cfg.sceneName, finalMode ? "FINAL" : "FACTORED", hwThreads, cfg.outDir);

            if (std::getenv("PIP3D_BAKE_NORAY") == nullptr && !Gpu::globalGpuBaker().init())
                return 1;

            SceneLighting light = collectLighting(r, cfg);
            if (!light.hasSun)
                std::printf("\033[33m[!] Scene has no directional light - baking sun-free.\033[0m\n");

            std::map<Mesh *, MeshCacheEntry> meshCache;
            std::vector<InstanceBakeData> bakes;
            std::vector<BakeTri> worldTris;
            std::vector<EmissiveTriLight> emissives;
            float emissiveTotalArea = 0.0f;

            collectInstances(instances, cfg, meshCache, bakes, worldTris,
                             emissives, emissiveTotalArea);

            if (bakes.empty())
            {
                std::printf("\033[91m[-] Scene has no visible non-emissive instances - nothing to bake.\033[0m\n");
                Gpu::globalGpuBaker().shutdown();
                return 1;
            }

            BakeBVH bvh;
            bvh.build(std::move(worldTris));

            std::printf("\033[36m[Pip3D]\033[0m BVH ready: %zu triangles, %zu mesh instances to bake\n",
                        bvh.triangleCount(), bakes.size());

            std::map<Mesh *, ChartPlan> plans;
            createChartPlans(meshCache, plans, cfg);

            BakeAtlas atlas;
            if (!buildAtlasPlan(bvh, light, cfg, hwThreads, meshCache, plans, bakes, atlas))
            {
                Gpu::globalGpuBaker().shutdown();
                return 1;
            }

            uint64_t totalWorkUnits = 0;
            for (const auto &b : bakes)
                totalWorkUnits += static_cast<uint64_t>(b.meshCache->rectW) *
                                  b.meshCache->rectH;

            if (cfg.bakeProbes && cfg.probeCell > 0.1f)
            {
                uint32_t dx, dy, dz;
                computeProbeGrid(bvh, cfg, dx, dy, dz);
                totalWorkUnits += static_cast<uint64_t>(dx) * dy * dz * 64ull;
            }

            progressBar().init(totalWorkUnits);

            try
            {
                for (size_t bakeIdx = 0; bakeIdx < bakes.size(); ++bakeIdx)
                {
                    InstanceBakeData &ib = bakes[bakeIdx];
                    MeshCacheEntry &mc = *ib.meshCache;

                    bakeInstanceLightmap(ib, bakeIdx, bakes.size(), bvh, light,
                                         emissives, emissiveTotalArea,
                                         cfg, finalMode);

                    const UnwrapResult &uw = mc.unwrap;
                    const uint32_t rw = uw.rectW;
                    const uint32_t rh = uw.rectH;
                    for (uint32_t y = 0; y < rh; ++y)
                    {
                        const size_t srcRow = static_cast<size_t>(y) * rw;
                        const size_t dstRow = static_cast<size_t>(ib.ay + y) * atlas.w + ib.ax;
                        std::memcpy(&atlas.data[dstRow], &ib.lm[srcRow], static_cast<size_t>(rw) * sizeof(uint16_t));
                        if (!atlas.f32.empty())
                            std::memcpy(&atlas.f32[dstRow * 3], &ib.enc[srcRow * 3],
                                        static_cast<size_t>(rw) * 3 * sizeof(float));
                    }
                    if (mc.uv2Mesh.empty())
                    {
                        const uint32_t faceN = static_cast<uint32_t>(mc.indices.size() / 3);
                        mc.uv2Mesh.resize(static_cast<size_t>(faceN) * 3);
                        for (uint32_t f = 0; f < faceN; ++f)
                        {
                            for (int k = 0; k < 3; ++k)
                            {
                                float uu = uw.cornerUV[f * 3 + k].u;
                                float vv = uw.cornerUV[f * 3 + k].v;
                                uu = std::fmax(0.0f, std::fmin(1.0f, uu));
                                vv = std::fmax(0.0f, std::fmin(1.0f, vv));
                                mc.uv2Mesh[f * 3 + k].u = static_cast<uint16_t>(uu * 65535.0f + 0.5f);
                                mc.uv2Mesh[f * 3 + k].v = static_cast<uint16_t>(vv * 65535.0f + 0.5f);
                            }
                        }
                    }
                }
            }
            catch (const std::exception &e)
            {
                if (progressBar().isActive())
                    progressBar().logf("\033[91m[-] %s\033[0m", e.what());
                else
                    std::printf("\033[91m[-] %s\033[0m\n", e.what());
                return 1;
            }

            blurAtlas(atlas);
            if (!encodePaletteAtlas(atlas))
            {
                progressBar().finish("Failed");
                Gpu::globalGpuBaker().shutdown();
                return 1;
            }

            ProbeBake probes;
            if (cfg.bakeProbes && cfg.probeCell > 0.1f)
                bakeProbes(bvh, light, emissives, emissiveTotalArea, cfg, hwThreads, probes);

            progressBar().finish("Bake Complete");

            const auto tEndCalc = std::chrono::steady_clock::now();

            if (cfg.writeHeader)
            {
                if (!std::getenv("PIP3D_BAKE_EXPORTOFF") && !std::getenv("PIP3D_BAKE_NOHEADER"))
                    writeBakedHeader(cfg.sceneName, mode, bakes, meshCache, atlas, probes);
            }
            if (!std::getenv("PIP3D_BAKE_EXPORTOFF"))
                writeBakePreviews(cfg.outDir, cfg.sceneName, mode, bakes, atlas, probes);

            if (std::getenv("PIP3D_BAKE_SHOT"))
            {
                std::vector<MeshInstance *> insts;
                for (const InstanceBakeData &b : bakes)
                    insts.push_back(b.inst);
                auto dumpShot = [&](const char *name)
                {
                    char shotPath[256];
                    std::snprintf(shotPath, sizeof(shotPath), "%s/%s", cfg.outDir, name);
                    renderAndDumpFrame(shotPath, r, insts);
                };
                dumpShot("Before.png");
                if (cfg.applyToInstances)
                {
                    applyBakedToInstances(mode, bakes, meshCache, atlas, r, probes);
                    dumpShot("After.png");
                }
            }
            else if (cfg.applyToInstances)
            {
                applyBakedToInstances(mode, bakes, meshCache, atlas, r, probes);
            }

            Gpu::globalGpuBaker().shutdown();

            char tbuf[32];
            formatHumanTime(tbuf, sizeof(tbuf),
                            std::chrono::duration<float>(tEndCalc - tStart).count(), false);
            std::printf("\033[36m[Pip3D]\033[0m Complete: \033[32m%s\033[0m -> %s/\n", tbuf, cfg.outDir);
            return 0;
        }
    }
}
