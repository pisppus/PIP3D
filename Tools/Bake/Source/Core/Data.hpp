#pragma once

#include <cstdint>
#include <map>
#include <vector>

#include <Pip3D.hpp>
#include "Geometry/Unwrap.hpp"

namespace pip3D
{
    namespace Bake
    {

        struct StaticLightSrc
        {
            Vector3 pos;
            Vector3 color;
            float rangeSq;
            float invRangeSq;
        };

        struct EmissiveTriLight
        {
            Vector3 v0, e1, e2, n;
            float area;
            Vector3 color;
        };

        struct MeshCacheEntry
        {
            std::vector<Vector3> localPos;
            std::vector<uint32_t> indices;
            std::vector<Vector3> localNormals;
            float uniqueArea = 0.0f;
            UnwrapResult unwrap;
            uint32_t rectW = 64;
            uint32_t rectH = 64;
            bool unwrapped = false;
            uint32_t meshKey = 0;
            std::vector<LMUVQuant> uv2Mesh;
        };

        struct InstanceBakeData
        {
            MeshInstance *inst = nullptr;
            MeshCacheEntry *meshCache = nullptr;
            uint32_t meshKey = 0;
            std::vector<Vector3> worldPos;
            std::vector<Vector3> worldNrm;
            std::vector<Vector3> texelPos;
            std::vector<Vector3> texelNrm;
            std::vector<float> den;
            std::vector<float> varLuma;
            std::vector<float> enc;
            std::vector<float> staticLuma;
            std::vector<Vector3> staticCol;
            std::vector<uint16_t> lm;
            uint32_t ax = 0, ay = 0;
            std::vector<LMUVQuant> uv2Instance;
            float staticMaxLuma = 0.0f;
            Vector3 staticSumCol = Vector3(0, 0, 0);
            float staticSumLuma = 0.0f;
            float staticScale = 1.0f;
            uint32_t validTexels = 0;
        };

        struct BakeAtlas
        {
            uint32_t w = 0, h = 0;
            std::vector<uint16_t> data;
            std::vector<float> f32;

            std::vector<uint16_t> palettes;
            std::vector<uint16_t> palettes8;
            std::vector<uint8_t> indices;
            std::vector<uint8_t> indices8;
            std::vector<uint16_t> ramp;
            std::vector<uint8_t> rampIdx;
            std::vector<uint16_t> palettes32;
            std::vector<uint8_t> indices32;
            std::vector<uint16_t> uniformCol;
            std::vector<uint16_t> dir;

            uint32_t wBlocks = 0, hBlocks = 0;
            uint32_t uniformTiles = 0;
            uint32_t rampTiles = 0;
            uint32_t pal8Tiles = 0;
            uint32_t pal16Tiles = 0;
            uint32_t pal32Tiles = 0;
            float avgLSB = 0.0f;
            float maxLSB = 0.0f;

            [[nodiscard]] bool encoded() const { return !dir.empty() && (!palettes.empty() || !palettes8.empty() || !uniformCol.empty()); }
        };

        struct ProbeBake
        {
            bool valid = false;
            Vector3 origin = Vector3(0, 0, 0);
            float cell = 4.0f;
            float cellY = 0.0f;
            uint32_t dx = 0, dy = 0, dz = 0;
            std::vector<uint16_t> probes;
            std::vector<float> lumaBuf;
            std::vector<float> colBuf;
            Vector3 staticTint = Vector3(1, 1, 1);
            float staticScale = 1.0f;
        };
    }
}
