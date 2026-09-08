#include <array>
#include <cmath>
#include <map>
#include <unordered_set>
#include <vector>

#include "Scene/Collect.hpp"

namespace pip3D
{
    namespace Bake
    {
        namespace
        {
            void gatherLights(Renderer &r, const BakeConfig &cfg,
                              const ShadingParams &sp, SceneLighting &out)
            {
                out.wrapTerm = sp.diffuseWrap;
                out.diffScale = sp.diffuseStrength / (1.0f + sp.diffuseWrap);
                out.smoothEps = 0.10f;
                out.tone.exposure = sp.exposureCoeff * r.getExposureScale();
                out.tone.knee = sp.toneKnee;
                out.tone.saturation = (cfg.saturation > 0.0f) ? cfg.saturation : 1.0f;
                out.skyLevel = 0.55f * r.getAmbientScale();
                out.skyNeut = std::fmin(1.0f, std::fmax(0.0f, cfg.skyNeutralize));

                const Light *lights = r.getLights();
                const int n = r.getActiveLightCount();

                out.hasSun = r.isSunEnabled();
                for (int i = 0; i < n; ++i)
                {
                    if (lights[i].type != LIGHT_DIRECTIONAL)
                        continue;
                    const Vector3 d = lights[i].direction;
                    const float len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
                    if (len > 1e-6f)
                        out.sunDirTo = d * (-1.0f / len);
                    float cr, cg, cb;
                    lights[i].color.toFloat(cr, cg, cb);
                    const float inten = lights[i].intensity;
                    out.sunCol = Vector3(cr * inten, cg * inten, cb * inten);
                    break;
                }
                if (out.sunCol.x + out.sunCol.y + out.sunCol.z <= 0.0f)
                    out.hasSun = false;

                const Light *pl = r.getPointLights();
                const int np = r.getPointLightCount();
                out.staticLights.reserve(static_cast<size_t>(np));
                for (int i = 0; i < np; ++i)
                {
                    StaticLightSrc s;
                    s.pos = pl[i].position;
                    float cr, cg, cb;
                    pl[i].color.toFloat(cr, cg, cb);
                    s.color = Vector3(cr * pl[i].intensity, cg * pl[i].intensity, cb * pl[i].intensity);
                    s.rangeSq = pl[i].rangeSq;
                    s.invRangeSq = pl[i].invRangeSq;
                    out.staticLights.push_back(s);
                }

                const Sky &sky = r.getSkybox();
                out.skyTop = sky.top;
                out.skyHor = sky.horizon;
                out.skyGnd = sky.ground;

                const Vector3 up(0.0f, 1.0f, 0.0f);
                Vector3 st1, st2;
                tangentBasis(up, st1, st2);
                uint32_t srng = 20260826u;
                constexpr uint32_t kSkyAvgN = 512;
                for (uint32_t i = 0; i < kSkyAvgN; ++i)
                {
                    const Vector3 d = cosineHemiDir(up, st1, st2, i, kSkyAvgN,
                                                    rand01(srng) * 6.2831853f);
                    out.skyAvgCol = out.skyAvgCol + skyColorForDir(d, out.skyTop,
                                                                   out.skyHor, out.skyGnd,
                                                                   out.skyNeut);
                }
                out.skyAvgCol = out.skyAvgCol * (1.0f / static_cast<float>(kSkyAvgN));
            }
        }

        SceneLighting collectLighting(Renderer &r, const BakeConfig &cfg)
        {
            SceneLighting out;
            gatherLights(r, cfg, Shading::getParams(), out);
            return out;
        }

        void collectInstances(std::vector<MeshInstance *> &instances,
                              const BakeConfig &cfg,
                              std::map<Mesh *, MeshCacheEntry> &meshCache,
                              std::vector<InstanceBakeData> &bakes,
                              std::vector<BakeTri> &worldTris,
                              std::vector<EmissiveTriLight> &emissives,
                              float &emissiveTotalArea)
        {
            uint32_t nextMeshKey = 0;
            for (MeshInstance *inst : instances)
            {
                if (!inst->isVisible())
                    continue;
                Mesh *mesh = inst->getMesh();
                if (!mesh)
                    continue;

                MeshCacheEntry *mc = nullptr;
                auto found = meshCache.find(mesh);
                if (found != meshCache.end())
                {
                    mc = &found->second;
                }
                else
                {
                    MeshCacheEntry &entry = meshCache[mesh];
                    entry.meshKey = nextMeshKey++;
                    const uint32_t vn = mesh->numVertices();
                    const uint32_t fn = mesh->numFaces();
                    entry.localPos.resize(vn);
                    mesh->decodePositions(entry.localPos.data(), vn);
                    entry.localNormals.resize(vn);
                    const Vertex *vd = mesh->vertexData();
                    for (uint32_t i = 0; i < vn; ++i)
                        entry.localNormals[i] = vd[i].normal.get();

                    entry.indices.resize(static_cast<size_t>(fn) * 3);
                    if (mesh->isIndex32())
                    {
                        const Face32 *f = mesh->faceData32();
                        for (uint32_t i = 0; i < fn; ++i)
                        {
                            entry.indices[i * 3 + 0] = f[i].v0;
                            entry.indices[i * 3 + 1] = f[i].v1;
                            entry.indices[i * 3 + 2] = f[i].v2;
                        }
                    }
                    else
                    {
                        const Face16 *f = mesh->faceData16();
                        for (uint32_t i = 0; i < fn; ++i)
                        {
                            entry.indices[i * 3 + 0] = f[i].v0;
                            entry.indices[i * 3 + 1] = f[i].v1;
                            entry.indices[i * 3 + 2] = f[i].v2;
                        }
                    }

                    {
                        struct TriHash
                        {
                            size_t operator()(const std::array<uint64_t, 3> &a) const noexcept
                            {
                                return static_cast<size_t>(a[0] ^ (a[1] * 0x9E3779B97F4A7C15ull) ^ (a[2] * 0xBF58476D1CE4E5B9ull));
                            }
                        };
                        std::unordered_set<std::array<uint64_t, 3>, TriHash> seen;
                        seen.reserve(fn * 2);
                        float ua = 0.0f;
                        for (uint32_t i = 0; i < fn; ++i)
                        {
                            std::array<uint64_t, 3> tri;
                            hashTri(entry.localPos[entry.indices[i * 3 + 0]],
                                    entry.localPos[entry.indices[i * 3 + 1]],
                                    entry.localPos[entry.indices[i * 3 + 2]], tri);
                            if (seen.find(tri) != seen.end())
                                continue;
                            seen.insert(tri);
                            const Vector3 &a = entry.localPos[entry.indices[i * 3 + 0]];
                            const Vector3 &b = entry.localPos[entry.indices[i * 3 + 1]];
                            const Vector3 &c = entry.localPos[entry.indices[i * 3 + 2]];
                            const Vector3 cr = cross3(b - a, c - a);
                            ua += std::sqrt(cr.x * cr.x + cr.y * cr.y + cr.z * cr.z) * 0.5f;
                        }
                        entry.uniqueArea = ua * inst->getScale().x * ((inst->getScale().y + inst->getScale().z) * 0.5f);
                    }
                    mc = &entry;
                }

                InstanceBakeData ib;
                ib.inst = inst;
                ib.meshCache = mc;
                ib.meshKey = mc->meshKey;

                const uint32_t fn = mesh->numFaces();
                const Matrix4x4 &wt = inst->transform();
                float ir, ig, ibc;
                inst->color().toFloat(ir, ig, ibc);
                const bool hasSub = mesh->hasSubMeshes();
                const uint32_t subN = mesh->numSubMeshes();
                const Texture *meshTex = mesh->isTextured() ? mesh->getTexture() : nullptr;
                const Vertex *vd = mesh->vertexData();
                uint32_t subCursor = 0;

                ib.worldPos.resize(static_cast<size_t>(fn) * 3);
                ib.worldNrm.resize(static_cast<size_t>(fn) * 3);
                for (uint32_t i = 0; i < fn; ++i)
                {
                    if (hasSub)
                    {
                        while (subCursor < subN && i >= mesh->subMeshFaceEnd(subCursor))
                            ++subCursor;
                    }
                    float sr = 1.0f, sg = 1.0f, sb = 1.0f;
                    if (hasSub && subCursor < subN)
                        mesh->subMeshColor(subCursor).toFloat(sr, sg, sb);
                    const Vector3 baseCol(ir * sr, ig * sg, ibc * sb);

                    Vector3 p[3];
                    for (int k = 0; k < 3; ++k)
                    {
                        const uint32_t vi = mc->indices[i * 3 + k];
                        p[k] = wt.transformNoDiv(mc->localPos[vi]);
                        ib.worldPos[i * 3 + k] = p[k];
                        ib.worldNrm[i * 3 + k] = transformDir(wt, mc->localNormals[vi]);
                    }

                    Vector3 gn = cross3(p[1] - p[0], p[2] - p[0]);
                    const float gl = std::sqrt(gn.x * gn.x + gn.y * gn.y + gn.z * gn.z);
                    if (gl < 1e-12f)
                        continue;
                    gn = gn * (1.0f / gl);

                    if (inst->isEmissive())
                    {
                        EmissiveTriLight e;
                        e.v0 = p[0];
                        e.e1 = p[1] - p[0];
                        e.e2 = p[2] - p[0];
                        e.n = gn;
                        e.area = gl * 0.5f;
                        float er, eg, eb;
                        inst->emissiveColor().toFloat(er, eg, eb);
                        const float k = inst->emissiveIntensity() * cfg.emissiveGain;
                        e.color = Vector3(er * k, eg * k, eb * k);
                        emissives.push_back(e);
                        emissiveTotalArea += e.area;
                        continue;
                    }

                    BakeTri t;
                    t.v0 = p[0];
                    t.e1 = p[1] - p[0];
                    t.e2 = p[2] - p[0];
                    t.centroid = (p[0] + p[1] + p[2]) * (1.0f / 3.0f);
                    t.gn = gn;
                    t.albedo = baseCol;
                    t.tex = meshTex;
                    t.tu0 = vd[mc->indices[i * 3 + 0]].tu;
                    t.tv0 = vd[mc->indices[i * 3 + 0]].tv;
                    t.tu1 = vd[mc->indices[i * 3 + 1]].tu;
                    t.tv1 = vd[mc->indices[i * 3 + 1]].tv;
                    t.tu2 = vd[mc->indices[i * 3 + 2]].tu;
                    t.tv2 = vd[mc->indices[i * 3 + 2]].tv;
                    t.area = gl * 0.5f;
                    worldTris.push_back(t);
                }

                if (!inst->isEmissive())
                    bakes.push_back(std::move(ib));
            }
        }
    }
}
