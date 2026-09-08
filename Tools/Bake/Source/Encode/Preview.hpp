#pragma once

#include <cstdint>
#include <vector>

#include <Pip3D.hpp>
#include "Encode/Atlas.hpp"

namespace pip3D
{
    namespace Bake
    {
        bool writePNG(const char *path, uint32_t w, uint32_t h,
                      const uint8_t *rgbTopDown);

        void previewAtlas(const char *path, const BakeAtlas &atlas, uint32_t scale = 1);

        void previewAtlasReference(const char *path, const BakeAtlas &atlas, uint32_t scale = 1);

        uint32_t previewAtlasSparse(const char *path, const BakeAtlas &atlas,
                                    char *metadata, uint32_t scale = 1);

        void previewLightmap(const char *path, const std::vector<uint16_t> &lm,
                             uint32_t w, uint32_t h, uint32_t scale);

        void previewChannels(const char *pathPrefix, const std::vector<float> &den,
                             const std::vector<TexelHit> &texels,
                             uint32_t w, uint32_t h, uint32_t scale);

        void previewProbeSlice(const char *path, const ProbeBake &probes,
                               uint32_t ySlice, uint32_t scale);

        void previewFrame565(const char *path, const uint16_t *fb,
                             uint32_t w, uint32_t h);

        void renderAndDumpFrame(const char *path, Renderer &r,
                                std::vector<MeshInstance *> &instances);
    }
}