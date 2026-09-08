#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include <Pip3D.hpp>

namespace pip3D
{
    namespace Bake
    {

        struct BakeTri
        {
            Vector3 v0;
            Vector3 e1;
            Vector3 e2;
            Vector3 centroid;
            Vector3 gn;
            Vector3 albedo;
            const Texture *tex;
            float tu0, tv0, tu1, tv1, tu2, tv2;
            float area;
        };

        struct BakeHit
        {
            uint32_t tri = 0xFFFFFFFFu;
            float t = 0.0f;
            float u = 0.0f;
            float v = 0.0f;
        };

        inline void bakeAlbedoAt(const BakeTri &t, float u, float v,
                                 float &r, float &g, float &b)
        {
            r = t.albedo.x;
            g = t.albedo.y;
            b = t.albedo.z;
            if (t.tex && t.tex->data)
            {
                const float w0 = 1.0f - u - v;
                float tu = t.tu0 * w0 + t.tu1 * u + t.tu2 * v;
                float tv = t.tv0 * w0 + t.tv1 * u + t.tv2 * v;
                tu -= std::floor(tu);
                tv -= std::floor(tv);
                const uint32_t tx = static_cast<uint32_t>(tu * t.tex->widthFlt()) & t.tex->maskU();
                const uint32_t ty = static_cast<uint32_t>(tv * t.tex->heightFlt()) & t.tex->maskV();
                const uint16_t c = t.tex->data[(ty << t.tex->shiftU) | tx];
                r *= static_cast<float>((c >> 11) & 0x1F) * (1.0f / 31.0f);
                g *= static_cast<float>((c >> 5) & 0x3F) * (1.0f / 63.0f);
                b *= static_cast<float>(c & 0x1F) * (1.0f / 31.0f);
            }
        }

        struct BakeAABB
        {
            Vector3 mn = Vector3(1e30f, 1e30f, 1e30f);
            Vector3 mx = Vector3(-1e30f, -1e30f, -1e30f);

            void grow(const Vector3 &p)
            {
                mn.x = std::fmin(mn.x, p.x);
                mn.y = std::fmin(mn.y, p.y);
                mn.z = std::fmin(mn.z, p.z);
                mx.x = std::fmax(mx.x, p.x);
                mx.y = std::fmax(mx.y, p.y);
                mx.z = std::fmax(mx.z, p.z);
            }

            [[nodiscard]] int longestAxis() const
            {
                const float ex = mx.x - mn.x, ey = mx.y - mn.y, ez = mx.z - mn.z;
                if (ex >= ey && ex >= ez)
                    return 0;
                return (ey >= ez) ? 1 : 2;
            }
        };

        class BakeBVH
        {
        public:
            void build(std::vector<BakeTri> tris);
            [[nodiscard]] size_t triangleCount() const { return m_tris.size(); }
            [[nodiscard]] const std::vector<BakeTri> &tris() const { return m_tris; }
            [[nodiscard]] const std::vector<uint32_t> &indices() const { return m_indices; }

            [[nodiscard]] uint64_t trisFingerprint() const noexcept { return m_fpTris; }
            [[nodiscard]] uint64_t nodesFingerprint() const noexcept { return m_fpNodes; }
            [[nodiscard]] uint64_t indicesFingerprint() const noexcept { return m_fpTriIdx; }

            struct BakeNode
            {
                BakeAABB bounds;
                uint32_t left = 0;
                uint32_t right = 0;
                uint32_t triStart = 0;
                uint32_t triCount = 0;
            };
            [[nodiscard]] const std::vector<BakeNode> &nodes() const { return m_nodes; }

            [[nodiscard]] bool occluded(const Vector3 &orig, const Vector3 &dir, float maxDist) const;
            [[nodiscard]] bool trace(const Vector3 &orig, const Vector3 &dir, float maxDist, BakeHit &out) const;

        private:
            static constexpr uint32_t kLeaf = 0xFFFFFFFFu;
            static constexpr uint32_t kLeafMaxTris = 8;
            static constexpr int kMaxDepth = 96;

            std::vector<BakeTri> m_tris;
            std::vector<uint32_t> m_indices;
            std::vector<BakeNode> m_nodes;
            uint64_t m_fpTris = 0, m_fpNodes = 0, m_fpTriIdx = 0;

            struct Ctx
            {
                Vector3 orig;
                Vector3 dir;
                Vector3 invD;
                float maxDist;
            };
            uint32_t buildNode(uint32_t start, uint32_t count);
            [[nodiscard]] static bool slabTest(const BakeAABB &b, const Ctx &ctx, float &tminOut, float &tmaxOut);
            [[nodiscard]] bool triHit(uint32_t ti, const Ctx &ctx, float &tOut, float &uOut, float &vOut) const;
        };

    }
}
