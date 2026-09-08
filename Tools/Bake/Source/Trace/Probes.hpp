#pragma once

#include <vector>

#include <Pip3D.hpp>
#include "Core/Config.hpp"
#include "Core/Data.hpp"
#include "Trace/Bvh.hpp"
#include "Scene/Collect.hpp"

namespace pip3D
{
    namespace Bake
    {

        namespace ProbeTrace
        {
            inline constexpr float kAnisoY = 2.0f;
            inline constexpr float kInteriorTestFactor = 1.5f;
            inline constexpr float kSkyOpenThreshold = 0.6f;
            inline constexpr float kSunOpenThreshold = 0.5f;
        }

        Vector3 computeProbeGrid(const BakeBVH &bvh, const BakeConfig &cfg,
                                 uint32_t &dx, uint32_t &dy, uint32_t &dz);

        void bakeProbes(const BakeBVH &bvh, const SceneLighting &light,
                        const std::vector<EmissiveTriLight> &emissives,
                        float emissiveTotalArea,
                        const BakeConfig &cfg, uint32_t threads,
                        ProbeBake &out);
    }
}
