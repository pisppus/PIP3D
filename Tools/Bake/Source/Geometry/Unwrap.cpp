#include "Geometry/Unwrap.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <vector>

#include <Pip3D.hpp>
#include "Core/Math.hpp"

namespace pip3D
{
    namespace Bake
    {
        bool shelfPackCharts(ChartPlan &plan, float S, bool useMul)
        {
            const uint32_t chartCount = static_cast<uint32_t>(plan.chartFaces.size());
            if (chartCount == 0)
            {
                plan.usedW = plan.usedH = 1.0f;
                return true;
            }
            const float gutter = plan.gutter;

            std::vector<uint32_t> order(chartCount);
            for (uint32_t i = 0; i < chartCount; ++i)
                order[i] = i;
            std::sort(order.begin(), order.end(), [&plan, S, useMul](uint32_t a, uint32_t b)
                      {
                          const float ma = (useMul && a < plan.chartMul.size()) ? plan.chartMul[a] : 1.0f;
                          const float mb = (useMul && b < plan.chartMul.size()) ? plan.chartMul[b] : 1.0f;
                          const float ha = plan.baseH[a] * S * ma;
                          const float hb = plan.baseH[b] * S * mb;
                          if (std::fabs(ha - hb) > 1e-6f)
                              return ha > hb;
                          const float wa = plan.baseW[a] * S * ma;
                          const float wb = plan.baseW[b] * S * mb;
                          return wa > wb; });

            float curX = gutter * 0.5f;
            float curY = gutter * 0.5f;
            float rowH = 0.0f;
            float maxX = 0.0f, maxY = 0.0f;

            plan.chartX.assign(chartCount, 0.0f);
            plan.chartY.assign(chartCount, 0.0f);

            for (const uint32_t i : order)
            {
                const float mul = (useMul && i < plan.chartMul.size()) ? plan.chartMul[i] : 1.0f;
                const float bw = plan.baseW[i] * S * mul + gutter;
                const float bh = plan.baseH[i] * S * mul + gutter;

                if (curX + bw > 1.0f + 1e-4f)
                {
                    curY += rowH;
                    curX = gutter * 0.5f;
                    rowH = 0.0f;
                }
                if (curY + bh > 1.0f + 1e-4f)
                    return false;

                plan.chartX[i] = curX;
                plan.chartY[i] = curY;
                maxX = std::fmax(maxX, curX + bw - gutter);
                maxY = std::fmax(maxY, curY + bh - gutter);

                curX += bw;
                rowH = std::fmax(rowH, bh);
            }
            plan.usedW = std::fmin(1.0f, maxX);
            plan.usedH = std::fmin(1.0f, maxY);
            return true;
        }

        void repackChartsWithMul(ChartPlan &plan)
        {
            if (shelfPackCharts(plan, plan.scaleS, true))
                return;
            for (int it = 0; it < 32; ++it)
            {
                bool changed = false;
                for (float &m : plan.chartMul)
                {
                    const float nm = std::fmax(0.25f, m * 0.9f);
                    if (nm != m)
                    {
                        m = nm;
                        changed = true;
                    }
                }
                if (!changed)
                    break;
                if (shelfPackCharts(plan, plan.scaleS, true))
                    return;
            }
            shelfPackCharts(plan, plan.scaleS, false);
        }

        ChartPlan planCharts(const std::vector<Vector3> &localPos,
                             const std::vector<uint32_t> &indices,
                             uint32_t faceCount,
                             uint32_t targetRes,
                             bool adaptiveTexels)
        {
            ChartPlan plan;
            plan.faceCount = faceCount;
            if (faceCount == 0)
                return plan;

            plan.dupOf.assign(faceCount, 0xFFFFFFFFu);
            {
                std::map<std::array<uint64_t, 3>, uint32_t> uniqueFaces;
                for (uint32_t f = 0; f < faceCount; ++f)
                {
                    std::array<uint64_t, 3> tri;
                    hashTri(localPos[indices[f * 3 + 0]], localPos[indices[f * 3 + 1]],
                            localPos[indices[f * 3 + 2]], tri);
                    auto it = uniqueFaces.find(tri);
                    if (it == uniqueFaces.end())
                        uniqueFaces.emplace(tri, f);
                    else
                        plan.dupOf[f] = it->second;
                }
            }

            std::vector<int> faceAxis(faceCount);
            std::vector<Vector3> faceN(faceCount);
            plan.fuv.resize(faceCount);
            for (uint32_t f = 0; f < faceCount; ++f)
            {
                const Vector3 &a = localPos[indices[f * 3 + 0]];
                const Vector3 &b = localPos[indices[f * 3 + 1]];
                const Vector3 &c = localPos[indices[f * 3 + 2]];
                Vector3 n = cross3(b - a, c - a);
                const float nl = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
                faceN[f] = (nl > 1e-9f) ? n * (1.0f / nl) : Vector3(0.0f, 1.0f, 0.0f);
                faceAxis[f] = dominantAxis(faceN[f]);
            }

            std::vector<uint32_t> parent(faceCount);
            for (uint32_t i = 0; i < faceCount; ++i)
                parent[i] = i;
            auto find = [&parent](uint32_t x)
            {
                while (parent[x] != x)
                {
                    parent[x] = parent[parent[x]];
                    x = parent[x];
                }
                return x;
            };

            struct EdgeKey
            {
                uint32_t a, b, face;
            };
            std::vector<EdgeKey> edges;
            edges.reserve(static_cast<size_t>(faceCount) * 3);
            for (uint32_t f = 0; f < faceCount; ++f)
            {
                if (plan.dupOf[f] != 0xFFFFFFFFu)
                    continue;
                for (int k = 0; k < 3; ++k)
                {
                    const uint32_t va = indices[f * 3 + k];
                    const uint32_t vb = indices[f * 3 + (k + 1) % 3];
                    edges.push_back(va < vb ? EdgeKey{va, vb, f} : EdgeKey{vb, va, f});
                }
            }
            std::sort(edges.begin(), edges.end(), [](const EdgeKey &x, const EdgeKey &y)
                      { return x.a != y.a ? x.a < y.a : x.b < y.b; });

            for (size_t i = 1; i < edges.size(); ++i)
            {
                if (edges[i].a == edges[i - 1].a && edges[i].b == edges[i - 1].b &&
                    faceAxis[edges[i].face] == faceAxis[edges[i - 1].face])
                {
                    const uint32_t fa = edges[i].face, fb = edges[i - 1].face;
                    if (faceN[fa].x * faceN[fb].x + faceN[fa].y * faceN[fb].y + faceN[fa].z * faceN[fb].z < 0.866f)
                        continue;
                    const uint32_t ra = find(fa);
                    const uint32_t rb = find(fb);
                    if (ra != rb)
                        parent[ra] = rb;
                }
            }

            std::vector<uint32_t> chartRoot(faceCount, 0xFFFFFFFFu);

            plan.chartOf.assign(faceCount, 0xFFFFFFFFu);
            for (uint32_t f = 0; f < faceCount; ++f)
            {
                if (plan.dupOf[f] != 0xFFFFFFFFu)
                    continue;
                const uint32_t root = find(f);
                if (chartRoot[root] == 0xFFFFFFFFu)
                {
                    chartRoot[root] = static_cast<uint32_t>(plan.chartFaces.size());
                    plan.chartFaces.emplace_back();
                }
                plan.chartOf[f] = chartRoot[root];
                plan.chartFaces[chartRoot[root]].push_back(f);
            }

            const uint32_t chartCount = static_cast<uint32_t>(plan.chartFaces.size());
            plan.baseW.assign(chartCount, 0.0f);
            plan.baseH.assign(chartCount, 0.0f);
            plan.chartMinU.assign(chartCount, 0.0f);
            plan.chartMinV.assign(chartCount, 0.0f);
            plan.chartMul.assign(chartCount, 1.0f);
            plan.chartDev.assign(chartCount, 0.0f);
            plan.chartN.assign(chartCount, Vector3(0.0f, 1.0f, 0.0f));

            {
                auto faceArea = [&](uint32_t f) -> float
                {
                    const Vector3 &a = localPos[indices[f * 3 + 0]];
                    const Vector3 &b = localPos[indices[f * 3 + 1]];
                    const Vector3 &c = localPos[indices[f * 3 + 2]];
                    Vector3 cr = cross3(b - a, c - a);
                    return std::sqrt(cr.x * cr.x + cr.y * cr.y + cr.z * cr.z) * 0.5f;
                };

                for (uint32_t c = 0; c < chartCount; ++c)
                {
                    Vector3 navg(0.0f, 0.0f, 0.0f);
                    float wsum = 0.0f;
                    for (const uint32_t f : plan.chartFaces[c])
                    {
                        const float w = std::fmax(faceArea(f), 1e-10f);
                        navg = navg + faceN[f] * w;
                        wsum += w;
                    }
                    float nl = std::sqrt(navg.x * navg.x + navg.y * navg.y + navg.z * navg.z);
                    if (nl < 1e-9f)
                        navg = Vector3(0.0f, 1.0f, 0.0f);
                    else
                        navg = navg * (1.0f / nl);
                    plan.chartN[c] = navg;

                    float dev = 0.0f;
                    for (const uint32_t f : plan.chartFaces[c])
                    {
                        const float w = std::fmax(faceArea(f), 1e-10f);
                        float d = faceN[f].x * navg.x + faceN[f].y * navg.y + faceN[f].z * navg.z;
                        d = std::fmax(-1.0f, std::fmin(1.0f, d));
                        dev += std::acos(d) * w;
                    }
                    dev /= std::fmax(wsum, 1e-10f);
                    plan.chartDev[c] = dev;
                    plan.chartMul[c] = adaptiveTexels ? ((dev < 0.08f) ? 1.0f : (dev < 0.25f) ? 0.75f
                                                                                              : 0.5f)
                                                      : 1.0f;
                }
            }

            for (uint32_t c = 0; c < chartCount; ++c)
            {
                Vector3 t1, t2;
                tangentBasis(plan.chartN[c], t1, t2);
                for (const uint32_t f : plan.chartFaces[c])
                    for (int k = 0; k < 3; ++k)
                    {
                        const Vector3 &p = localPos[indices[f * 3 + k]];
                        plan.fuv[f].u[k] = p.x * t1.x + p.y * t1.y + p.z * t1.z;
                        plan.fuv[f].v[k] = p.x * t2.x + p.y * t2.y + p.z * t2.z;
                    }
            }

            for (uint32_t c = 0; c < chartCount; ++c)
            {
                float umn = 1e30f, umx = -1e30f, vmn = 1e30f, vmx = -1e30f;
                for (const uint32_t f : plan.chartFaces[c])
                    for (int k = 0; k < 3; ++k)
                    {
                        umn = std::fmin(umn, plan.fuv[f].u[k]);
                        umx = std::fmax(umx, plan.fuv[f].u[k]);
                        vmn = std::fmin(vmn, plan.fuv[f].v[k]);
                        vmx = std::fmax(vmx, plan.fuv[f].v[k]);
                    }
                plan.chartMinU[c] = umn;
                plan.chartMinV[c] = vmn;
                plan.baseW[c] = std::fmax(umx - umn, 1e-4f);
                plan.baseH[c] = std::fmax(vmx - vmn, 1e-4f);
            }

            plan.gutter = 6.0f / std::fmax(4.0f, static_cast<float>(targetRes));

            float lowS = 1e-6f, highS = 1000.0f;
            float bestS = lowS;
            for (int iter = 0; iter < 28; ++iter)
            {
                const float midS = (lowS + highS) * 0.5f;
                if (shelfPackCharts(plan, midS, false))
                {
                    bestS = midS;
                    lowS = midS;
                }
                else
                {
                    highS = midS;
                }
            }

            plan.scaleS = bestS;
            shelfPackCharts(plan, bestS, adaptiveTexels);
            return plan;
        }

        UnwrapResult rasterizeCharts(const ChartPlan &plan, uint32_t rectW, uint32_t rectH)
        {
            UnwrapResult res;
            res.faceCount = plan.faceCount;
            res.rectW = rectW;
            res.rectH = rectH;
            res.cornerUV.assign(static_cast<size_t>(plan.faceCount) * 3, LMCoord());
            res.texels.assign(static_cast<size_t>(rectW) * rectH, TexelHit());
            if (plan.faceCount == 0 || plan.chartFaces.empty())
                return res;

            const float S = plan.scaleS;

            const float normW = (plan.usedW > 1e-6f) ? 1.0f / plan.usedW : 1.0f;
            const float normH = (plan.usedH > 1e-6f) ? 1.0f / plan.usedH : 1.0f;

            float mulMin = 1.0f;
            for (const float m : plan.chartMul)
                mulMin = std::fmin(mulMin, m);
            res.metersPerTexelU = (S > 1e-9f) ? plan.usedW / (S * mulMin * static_cast<float>(rectW)) : 1.0f;
            res.metersPerTexelV = (S > 1e-9f) ? plan.usedH / (S * mulMin * static_cast<float>(rectH)) : 1.0f;

            for (uint32_t f = 0; f < plan.faceCount; ++f)
            {
                if (plan.dupOf[f] != 0xFFFFFFFFu)
                    continue;
                const uint32_t c = plan.chartOf[f];
                const float mul = (c < plan.chartMul.size()) ? plan.chartMul[c] : 1.0f;
                for (int k = 0; k < 3; ++k)
                {
                    const float uu = (plan.fuv[f].u[k] - plan.chartMinU[c]) / plan.baseW[c];
                    const float vv = (plan.fuv[f].v[k] - plan.chartMinV[c]) / plan.baseH[c];
                    const float uN = (plan.chartX[c] + uu * plan.baseW[c] * S * mul) * normW;
                    const float vN = (plan.chartY[c] + vv * plan.baseH[c] * S * mul) * normH;
                    res.cornerUV[f * 3 + k].u = clamp01u(uN);
                    res.cornerUV[f * 3 + k].v = clamp01u(vN);
                }
            }
            for (uint32_t f = 0; f < plan.faceCount; ++f)
            {
                const uint32_t orig = plan.dupOf[f];
                if (orig == 0xFFFFFFFFu)
                    continue;
                for (int k = 0; k < 3; ++k)
                    res.cornerUV[f * 3 + k] = res.cornerUV[orig * 3 + k];
            }

            const float cw = static_cast<float>(rectW);
            const float chh = static_cast<float>(rectH);
            std::vector<float> bestScore(static_cast<size_t>(rectW) * rectH, -1e30f);

            for (uint32_t f = 0; f < plan.faceCount; ++f)
            {
                if (plan.dupOf[f] != 0xFFFFFFFFu)
                    continue;
                const LMCoord &A = res.cornerUV[f * 3 + 0];
                const LMCoord &B = res.cornerUV[f * 3 + 1];
                const LMCoord &C = res.cornerUV[f * 3 + 2];

                float umn = std::fmin(A.u, std::fmin(B.u, C.u));
                float umx = std::fmax(A.u, std::fmax(B.u, C.u));
                float vmn = std::fmin(A.v, std::fmin(B.v, C.v));
                float vmx = std::fmax(A.v, std::fmax(B.v, C.v));

                umn -= 1.0f / cw;
                vmn -= 1.0f / chh;
                umx += 1.0f / cw;
                vmx += 1.0f / chh;

                const int tx0 = std::max(0, static_cast<int>(std::floor(umn * cw)));
                const int ty0 = std::max(0, static_cast<int>(std::floor(vmn * chh)));
                const int tx1 = std::min(static_cast<int>(rectW) - 1, static_cast<int>(std::ceil(umx * cw)));
                const int ty1 = std::min(static_cast<int>(rectH) - 1, static_cast<int>(std::ceil(vmx * chh)));

                const float det = (B.u - A.u) * (C.v - A.v) - (C.u - A.u) * (B.v - A.v);
                if (std::fabs(det) < 1e-12f)
                    continue;
                const float invDet = 1.0f / det;

                for (int ty = ty0; ty <= ty1; ++ty)
                {
                    for (int tx = tx0; tx <= tx1; ++tx)
                    {
                        const float px = (static_cast<float>(tx) + 0.5f) / cw;
                        const float py = (static_cast<float>(ty) + 0.5f) / chh;

                        const float w1 = ((px - A.u) * (C.v - A.v) - (C.u - A.u) * (py - A.v)) * invDet;
                        const float w2 = ((B.u - A.u) * (py - A.v) - (px - A.u) * (B.v - A.v)) * invDet;
                        const float w0 = 1.0f - w1 - w2;

                        const float score = std::fmin(w0, std::fmin(w1, w2));
                        if (score >= -0.5f)
                        {
                            const size_t idx = static_cast<size_t>(ty) * rectW + tx;
                            if (score > bestScore[idx])
                            {
                                bestScore[idx] = score;

                                float b0 = w0, b1 = w1, b2 = w2;
                                if (score < 0.0f)
                                {
                                    b0 = std::max(0.0f, w0);
                                    b1 = std::max(0.0f, w1);
                                    b2 = std::max(0.0f, w2);
                                    const float sumB = b0 + b1 + b2;
                                    if (sumB > 1e-6f)
                                    {
                                        b1 /= sumB;
                                        b2 /= sumB;
                                    }
                                }

                                TexelHit &th = res.texels[idx];
                                th.face = f;
                                th.b1 = b1;
                                th.b2 = b2;
                            }
                        }
                    }
                }
            }

            return res;
        }
    }
}
