#pragma once

#include <map>
#include <vector>

#include <Pip3D.hpp>
#include "Core/Config.hpp"
#include "Core/Data.hpp"
#include "Core/Math.hpp"
#include "Trace/Bvh.hpp"

namespace pip3D
{
    namespace Bake
    {
        struct SceneLighting
        {
            bool hasSun = false;
            Vector3 sunDirTo = Vector3(0.0f, 1.0f, 0.0f);
            Vector3 sunCol = Vector3(0.0f, 0.0f, 0.0f);
            Color skyTop, skyHor, skyGnd;
            float skyLevel = 0.0f;
            float skyNeut = 0.0f;

            Vector3 skyAvgCol = Vector3(0.0f, 0.0f, 0.0f);

            float wrapTerm = 0.0f;
            float diffScale = 1.0f;
            float smoothEps = 0.10f;
            ToneParams tone;

            std::vector<StaticLightSrc> staticLights;
        };

        SceneLighting collectLighting(Renderer &r, const BakeConfig &cfg);

        void collectInstances(std::vector<MeshInstance *> &instances,
                              const BakeConfig &cfg,
                              std::map<Mesh *, MeshCacheEntry> &meshCache,
                              std::vector<InstanceBakeData> &bakes,
                              std::vector<BakeTri> &worldTris,
                              std::vector<EmissiveTriLight> &emissives,
                              float &emissiveTotalArea);
    }
}
