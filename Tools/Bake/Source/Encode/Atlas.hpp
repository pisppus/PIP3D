#pragma once

#include <map>
#include <vector>

#include <Pip3D.hpp>
#include "Core/Data.hpp"
#include "Geometry/Unwrap.hpp"

namespace pip3D
{
    namespace Bake
    {

        [[nodiscard]] uint16_t decodeAtlasTexel(const BakeAtlas &atlas, uint32_t x, uint32_t y);

        bool encodePaletteAtlas(BakeAtlas &atlas);
        void blurAtlas(BakeAtlas &atlas, uint32_t passes = 2);

        void writeBakedHeader(const char *sceneName, BakedLightMode mode,
                              const std::vector<InstanceBakeData> &bakes,
                              const std::map<Mesh *, MeshCacheEntry> &meshCache,
                              const BakeAtlas &atlas,
                              const ProbeBake &probes);

        void writeBakePreviews(const char *outDir, const char *sceneName,
                               BakedLightMode mode,
                               const std::vector<InstanceBakeData> &bakes,
                               const BakeAtlas &atlas,
                               const ProbeBake &probes);

        void applyBakedToInstances(BakedLightMode mode,
                                   const std::vector<InstanceBakeData> &bakes,
                                   const std::map<Mesh *, MeshCacheEntry> &meshCache,
                                   const BakeAtlas &atlas,
                                   Renderer &r, const ProbeBake &probes);
    }
}
