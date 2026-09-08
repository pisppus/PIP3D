#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <vector>

#include <Pip3D.hpp>

namespace pip3D
{
    namespace Bake
    {

        struct LMCoord
        {
            float u = 0.0f, v = 0.0f;
        };

        struct TexelHit
        {
            float b1 = 0.0f, b2 = 0.0f;
            uint32_t face = 0xFFFFFFFFu;
        };

        struct ChartPlan
        {
            uint32_t faceCount = 0;
            float scaleS = 1.0f;
            float usedW = 1.0f;
            float usedH = 1.0f;

            std::vector<uint32_t> dupOf;
            std::vector<uint32_t> chartOf;
            std::vector<float> chartMul;
            std::vector<float> chartDev;
            std::vector<Vector3> chartN;
            std::vector<float> baseW, baseH;
            std::vector<float> chartMinU, chartMinV;
            float gutter = 0.0f;

            struct FaceUV
            {
                float u[3], v[3];
            };
            std::vector<FaceUV> fuv;
            std::vector<std::vector<uint32_t>> chartFaces;
            std::vector<float> chartX, chartY;
        };

        struct UnwrapResult
        {
            uint32_t faceCount = 0;
            uint32_t rectW = 8, rectH = 8;
            std::vector<LMCoord> cornerUV;
            std::vector<TexelHit> texels;
            float metersPerTexelU = 1.0f;
            float metersPerTexelV = 1.0f;
        };

        inline int dominantAxis(const Vector3 &n)
        {
            const float ax = std::fabs(n.x), ay = std::fabs(n.y), az = std::fabs(n.z);
            if (ax >= ay && ax >= az)
                return 0;
            return (ay >= az) ? 1 : 2;
        }

        inline void hashTri(const Vector3 &p0, const Vector3 &p1, const Vector3 &p2,
                            std::array<uint64_t, 3> &out)
        {
            auto q = [](float v) -> uint64_t
            { return static_cast<uint64_t>(static_cast<int64_t>(v * 4096.0f)); };
            auto mix = [&](const Vector3 &p) -> uint64_t
            {
                uint64_t h = (q(p.x) * 0x9E3779B97F4A7C15ull) ^
                             (q(p.y) * 0xC2B2AE3D27D4EB4Full) ^
                             (q(p.z) * 0x165667B19E3779F9ull);
                h ^= h >> 30;
                h *= 0xBF58476D1CE4E5B9ull;
                h ^= h >> 27;
                h *= 0x94D049BB133111EBull;
                h ^= h >> 31;
                return h;
            };
            out = {mix(p0), mix(p1), mix(p2)};
            std::sort(out.begin(), out.end());
        }

        inline float clamp01u(float x)
        {
            return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
        }

        bool shelfPackCharts(ChartPlan &plan, float S, bool useMul);
        void repackChartsWithMul(ChartPlan &plan);
        ChartPlan planCharts(const std::vector<Vector3> &localPos,
                             const std::vector<uint32_t> &indices,
                             uint32_t faceCount,
                             uint32_t targetRes,
                             bool adaptiveTexels = true);
        UnwrapResult rasterizeCharts(const ChartPlan &plan, uint32_t rectW, uint32_t rectH);
    }
}
