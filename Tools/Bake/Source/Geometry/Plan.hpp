#pragma once

#include <cstdint>
#include <map>
#include <vector>

#include <Pip3D.hpp>
#include "Core/Config.hpp"
#include "Core/Data.hpp"
#include "Geometry/Unwrap.hpp"
#include "Scene/Collect.hpp"

namespace pip3D
{
    namespace Bake
    {
        void createChartPlans(const std::map<Mesh *, MeshCacheEntry> &meshCache,
                              std::map<Mesh *, ChartPlan> &plans,
                              const BakeConfig &cfg);

        bool buildAtlasPlan(const BakeBVH &bvh,
                            const SceneLighting &light,
                            const BakeConfig &cfg, uint32_t threads,
                            std::map<Mesh *, MeshCacheEntry> &meshCache,
                            std::map<Mesh *, ChartPlan> &plans,
                            std::vector<InstanceBakeData> &bakes,
                            BakeAtlas &atlas);
    }
}
