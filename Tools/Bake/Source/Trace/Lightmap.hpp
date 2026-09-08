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

        void bakeInstanceLightmap(InstanceBakeData &ib, size_t bakeIdx, size_t bakeCount,
                                  const BakeBVH &bvh, const SceneLighting &light,
                                  const std::vector<EmissiveTriLight> &emissives,
                                  float emissiveTotalArea,
                                  const BakeConfig &cfg, bool finalMode);
    }
}
