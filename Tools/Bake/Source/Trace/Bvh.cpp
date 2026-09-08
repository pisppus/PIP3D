#include "Trace/Bvh.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include <Pip3D.hpp>
#include "Core/Math.hpp"

namespace pip3D
{
    namespace Bake
    {
        void BakeBVH::build(std::vector<BakeTri> tris)
        {
            m_tris = std::move(tris);
            m_indices.resize(m_tris.size());
            for (uint32_t i = 0; i < m_indices.size(); ++i)
                m_indices[i] = i;

            m_nodes.clear();
            m_nodes.reserve(m_tris.empty() ? 8 : m_tris.size() * 2 + 8);
            if (!m_indices.empty())
                buildNode(0, static_cast<uint32_t>(m_indices.size()));
            m_nodes.shrink_to_fit();

            m_fpTris = fnv1a64(m_tris.data(), m_tris.size() * sizeof(BakeTri));
            m_fpNodes = fnv1a64(m_nodes.data(), m_nodes.size() * sizeof(BakeNode));
            m_fpTriIdx = fnv1a64(m_indices.data(), m_indices.size() * sizeof(uint32_t));
        }

        bool BakeBVH::occluded(const Vector3 &orig, const Vector3 &dir, float maxDist) const
        {
            if (m_nodes.empty())
                return false;
            Ctx ctx;
            ctx.orig = orig;
            ctx.dir = dir;
            ctx.invD = Vector3(
                1.0f / (std::fabs(dir.x) < 1e-12f ? (dir.x < 0 ? -1e-12f : 1e-12f) : dir.x),
                1.0f / (std::fabs(dir.y) < 1e-12f ? (dir.y < 0 ? -1e-12f : 1e-12f) : dir.y),
                1.0f / (std::fabs(dir.z) < 1e-12f ? (dir.z < 0 ? -1e-12f : 1e-12f) : dir.z));
            ctx.maxDist = maxDist;

            uint32_t stack[kMaxDepth];
            int sp = 0;
            stack[sp++] = 0;
            while (sp > 0)
            {
                const BakeNode &node = m_nodes[stack[--sp]];
                float tmin, tmax;
                if (!slabTest(node.bounds, ctx, tmin, tmax))
                    continue;
                if (node.left == kLeaf)
                {
                    for (uint32_t i = node.triStart; i < node.triStart + node.triCount; ++i)
                    {
                        float t, u, v;
                        if (triHit(m_indices[i], ctx, t, u, v))
                            return true;
                    }
                }
                else
                {

                    if (sp + 2 > kMaxDepth)
                    {

                        continue;
                    }
                    stack[sp++] = node.left;
                    stack[sp++] = node.right;
                }
            }
            return false;
        }

        bool BakeBVH::trace(const Vector3 &orig, const Vector3 &dir, float maxDist, BakeHit &out) const
        {
            if (m_nodes.empty())
                return false;
            Ctx ctx;
            ctx.orig = orig;
            ctx.dir = dir;
            ctx.invD = Vector3(
                1.0f / (std::fabs(dir.x) < 1e-12f ? (dir.x < 0 ? -1e-12f : 1e-12f) : dir.x),
                1.0f / (std::fabs(dir.y) < 1e-12f ? (dir.y < 0 ? -1e-12f : 1e-12f) : dir.y),
                1.0f / (std::fabs(dir.z) < 1e-12f ? (dir.z < 0 ? -1e-12f : 1e-12f) : dir.z));
            ctx.maxDist = maxDist;

            float bestT = maxDist;
            uint32_t bestTri = 0xFFFFFFFFu;
            float bestU = 0.0f, bestV = 0.0f;

            uint32_t stack[kMaxDepth];
            int sp = 0;
            stack[sp++] = 0;
            while (sp > 0)
            {
                const BakeNode &node = m_nodes[stack[--sp]];
                float tmin, tmax;
                if (!slabTest(node.bounds, ctx, tmin, tmax))
                    continue;
                if (tmin > bestT)
                    continue;
                if (node.left == kLeaf)
                {
                    for (uint32_t i = node.triStart; i < node.triStart + node.triCount; ++i)
                    {
                        float t, u, v;
                        if (triHit(m_indices[i], ctx, t, u, v) && t < bestT)
                        {
                            bestT = t;
                            bestTri = m_indices[i];
                            bestU = u;
                            bestV = v;
                        }
                    }
                }
                else
                {

                    if (sp + 2 > kMaxDepth)
                        continue;
                    float lMin, lMax, rMin, rMax;
                    const bool lHit = slabTest(m_nodes[node.left].bounds, ctx, lMin, lMax) && lMin <= bestT;
                    const bool rHit = slabTest(m_nodes[node.right].bounds, ctx, rMin, rMax) && rMin <= bestT;
                    if (lHit && rHit)
                    {
                        if (lMin < rMin)
                        {
                            stack[sp++] = node.right;
                            stack[sp++] = node.left;
                        }
                        else
                        {
                            stack[sp++] = node.left;
                            stack[sp++] = node.right;
                        }
                    }
                    else if (lHit)
                        stack[sp++] = node.left;
                    else if (rHit)
                        stack[sp++] = node.right;
                }
            }

            if (bestTri == 0xFFFFFFFFu)
                return false;
            out.tri = bestTri;
            out.t = bestT;
            out.u = bestU;
            out.v = bestV;
            return true;
        }

        uint32_t BakeBVH::buildNode(uint32_t start, uint32_t count)
        {
            const uint32_t nodeIdx = static_cast<uint32_t>(m_nodes.size());
            m_nodes.emplace_back();

            BakeAABB bounds;
            for (uint32_t i = start; i < start + count; ++i)
            {
                const BakeTri &t = m_tris[m_indices[i]];
                bounds.grow(t.v0);
                bounds.grow(t.v0 + t.e1);
                bounds.grow(t.v0 + t.e2);
            }
            m_nodes[nodeIdx].bounds = bounds;

            if (count <= kLeafMaxTris)
            {
                m_nodes[nodeIdx].left = kLeaf;
                m_nodes[nodeIdx].right = kLeaf;
                m_nodes[nodeIdx].triStart = start;
                m_nodes[nodeIdx].triCount = count;
                return nodeIdx;
            }

            constexpr uint32_t kBins = 16;
            struct Bin
            {
                BakeAABB bounds{};
                uint32_t count = 0;
            };

            BakeAABB centBounds;
            for (uint32_t i = start; i < start + count; ++i)
                centBounds.grow(m_tris[m_indices[i]].centroid);

            const int axis = centBounds.longestAxis();
            const float cMin = axis == 0 ? centBounds.mn.x : (axis == 1 ? centBounds.mn.y : centBounds.mn.z);
            const float cMax = axis == 0 ? centBounds.mx.x : (axis == 1 ? centBounds.mx.y : centBounds.mx.z);
            const float extent = cMax - cMin;

            if (extent < 1e-6f)
            {
                m_nodes[nodeIdx].left = kLeaf;
                m_nodes[nodeIdx].right = kLeaf;
                m_nodes[nodeIdx].triStart = start;
                m_nodes[nodeIdx].triCount = count;
                return nodeIdx;
            }

            Bin bins[kBins]{};
            auto binOf = [this, axis, cMin, extent](uint32_t triIdx) -> uint32_t
            {
                const Vector3 &c = m_tris[triIdx].centroid;
                const float cAxis = axis == 0 ? c.x : (axis == 1 ? c.y : c.z);
                uint32_t b = static_cast<uint32_t>((cAxis - cMin) / extent * static_cast<float>(kBins));
                return b >= kBins ? kBins - 1u : b;
            };
            for (uint32_t i = start; i < start + count; ++i)
            {
                const uint32_t idx = m_indices[i];
                Bin &bin = bins[binOf(idx)];
                bin.count++;
                const BakeTri &t = m_tris[idx];
                bin.bounds.grow(t.v0);
                bin.bounds.grow(t.v0 + t.e1);
                bin.bounds.grow(t.v0 + t.e2);
            }

            auto area = [](const BakeAABB &bb) -> float
            {
                const float dx = bb.mx.x - bb.mn.x;
                const float dy = bb.mx.y - bb.mn.y;
                const float dz = bb.mx.z - bb.mn.z;
                return 2.0f * (dx * dy + dy * dz + dz * dx);
            };

            BakeAABB leftAcc[kBins], rightAcc[kBins];
            uint32_t leftCnt[kBins]{}, rightCnt[kBins]{};
            BakeAABB acc;
            uint32_t cnt = 0;
            for (uint32_t i = 0; i < kBins; ++i)
            {
                if (bins[i].count)
                {

                    acc.grow(bins[i].bounds.mn);
                    acc.grow(bins[i].bounds.mx);
                    cnt += bins[i].count;
                }
                leftAcc[i] = acc;
                leftCnt[i] = cnt;
            }
            acc = BakeAABB();
            cnt = 0;
            for (int i = static_cast<int>(kBins) - 1; i >= 0; --i)
            {
                if (bins[static_cast<uint32_t>(i)].count)
                {
                    acc.grow(bins[static_cast<uint32_t>(i)].bounds.mn);
                    acc.grow(bins[static_cast<uint32_t>(i)].bounds.mx);
                    cnt += bins[static_cast<uint32_t>(i)].count;
                }
                rightAcc[static_cast<uint32_t>(i)] = acc;
                rightCnt[static_cast<uint32_t>(i)] = cnt;
            }

            float bestCost = 1e30f;
            int bestSplit = -1;
            const float parentArea = area(bounds);
            const float invParent = parentArea > 1e-12f ? 1.0f / parentArea : 1.0f;
            for (uint32_t i = 0; i + 1 < kBins; ++i)
            {
                if (leftCnt[i] == 0 || rightCnt[i + 1] == 0)
                    continue;
                const float cost = 0.125f + (area(leftAcc[i]) * static_cast<float>(leftCnt[i]) +
                                             area(rightAcc[i + 1]) * static_cast<float>(rightCnt[i + 1])) *
                                                invParent;
                if (cost < bestCost)
                {
                    bestCost = cost;
                    bestSplit = static_cast<int>(i);
                }
            }

            auto centroidLess = [this, axis](uint32_t a, uint32_t b)
            {
                const Vector3 &ca = m_tris[a].centroid;
                const Vector3 &cb = m_tris[b].centroid;
                const float fa = axis == 0 ? ca.x : (axis == 1 ? ca.y : ca.z);
                const float fb = axis == 0 ? cb.x : (axis == 1 ? cb.y : cb.z);
                return fa < fb;
            };

            const uint32_t leafCost = static_cast<float>(count);
            if (bestSplit == -1 || bestCost >= leafCost)
            {

                const uint32_t mid = start + count / 2;
                std::nth_element(m_indices.begin() + start, m_indices.begin() + mid,
                                 m_indices.begin() + start + count, centroidLess);
                if (mid == start || mid == start + count)
                {
                    m_nodes[nodeIdx].left = kLeaf;
                    m_nodes[nodeIdx].right = kLeaf;
                    m_nodes[nodeIdx].triStart = start;
                    m_nodes[nodeIdx].triCount = count;
                    return nodeIdx;
                }
                const uint32_t leftIdx = buildNode(start, mid - start);
                const uint32_t rightIdx = buildNode(mid, start + count - mid);
                m_nodes[nodeIdx].left = leftIdx;
                m_nodes[nodeIdx].right = rightIdx;
                return nodeIdx;
            }

            auto midIt = std::partition(m_indices.begin() + start, m_indices.begin() + start + count,
                                        [&](uint32_t idx)
                                        { return static_cast<int>(binOf(idx)) <= bestSplit; });
            uint32_t mid = static_cast<uint32_t>(midIt - m_indices.begin());
            if (mid == start || mid == start + count)
            {
                mid = start + count / 2;
                std::nth_element(m_indices.begin() + start, m_indices.begin() + mid,
                                 m_indices.begin() + start + count, centroidLess);
            }
            if (mid == start || mid == start + count)
            {
                m_nodes[nodeIdx].left = kLeaf;
                m_nodes[nodeIdx].right = kLeaf;
                m_nodes[nodeIdx].triStart = start;
                m_nodes[nodeIdx].triCount = count;
                return nodeIdx;
            }

            const uint32_t leftIdx = buildNode(start, mid - start);
            const uint32_t rightIdx = buildNode(mid, start + count - mid);
            m_nodes[nodeIdx].left = leftIdx;
            m_nodes[nodeIdx].right = rightIdx;
            return nodeIdx;
        }

        bool BakeBVH::slabTest(const BakeAABB &b, const Ctx &ctx, float &tminOut, float &tmaxOut)
        {
            float tmin = 0.0f, tmax = ctx.maxDist;

            const float ox = ctx.orig.x, oy = ctx.orig.y, oz = ctx.orig.z;
            const float idx = ctx.invD.x, idy = ctx.invD.y, idz = ctx.invD.z;

            for (int a = 0; a < 3; ++a)
            {
                const float o = a == 0 ? ox : (a == 1 ? oy : oz);
                const float id = a == 0 ? idx : (a == 1 ? idy : idz);
                const float mn = a == 0 ? b.mn.x : (a == 1 ? b.mn.y : b.mn.z);
                const float mx = a == 0 ? b.mx.x : (a == 1 ? b.mx.y : b.mx.z);

                float t1 = (mn - o) * id;
                float t2 = (mx - o) * id;
                if (t1 > t2)
                    std::swap(t1, t2);
                tmin = std::fmax(tmin, t1);
                tmax = std::fmin(tmax, t2);
                if (tmin > tmax)
                    return false;
            }
            tminOut = tmin;
            tmaxOut = tmax;
            return true;
        }

        bool BakeBVH::triHit(uint32_t ti, const Ctx &ctx, float &tOut, float &uOut, float &vOut) const
        {
            const BakeTri &t = m_tris[ti];
            const Vector3 &dir = ctx.dir;

            const float px = dir.y * t.e2.z - dir.z * t.e2.y;
            const float py = dir.z * t.e2.x - dir.x * t.e2.z;
            const float pz = dir.x * t.e2.y - dir.y * t.e2.x;
            const float det = t.e1.x * px + t.e1.y * py + t.e1.z * pz;
            if (std::fabs(det) < 1e-12f)
                return false;
            const float invDet = 1.0f / det;

            const float sx = ctx.orig.x - t.v0.x;
            const float sy = ctx.orig.y - t.v0.y;
            const float sz = ctx.orig.z - t.v0.z;
            const float u = (sx * px + sy * py + sz * pz) * invDet;
            if (u < 0.0f || u > 1.0f)
                return false;

            const float qx = sy * t.e1.z - sz * t.e1.y;
            const float qy = sz * t.e1.x - sx * t.e1.z;
            const float qz = sx * t.e1.y - sy * t.e1.x;
            const float v = (dir.x * qx + dir.y * qy + dir.z * qz) * invDet;
            if (v < 0.0f || u + v > 1.0f)
                return false;

            const float tt = (t.e2.x * qx + t.e2.y * qy + t.e2.z * qz) * invDet;
            if (tt <= 1e-4f || tt >= ctx.maxDist)
                return false;

            tOut = tt;
            uOut = u;
            vOut = v;
            return true;
        }
    }
}
