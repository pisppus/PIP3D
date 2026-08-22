#include <vector>

#include "Core/Platform.hpp"
#include "Math/Algebra.hpp"
#include "Camera/Camera.hpp"
#include "Rendering/Buffers/FrameBuffer.hpp"
#include "Rendering/Buffers/ZBuffer.hpp"
#include "Rendering/Lighting/Fog.hpp"
#include "Rendering/Lighting/Lighting.hpp"
#include "Rendering/Pipeline/Culling.hpp"
#include "Rendering/Pipeline/DrawCache.hpp"
#include "Rendering/Pipeline/MeshDraw.hpp"
#include "Rendering/Pipeline/Rasterizer/Smooth.hpp"
#include "Rendering/Pipeline/Rasterizer/Textured.hpp"
#include "Rendering/Pipeline/Shading.hpp"
#include "Rendering/Pipeline/Telemetry.hpp"
#include "Rendering/Resources/Texture.hpp"
#include "Rendering/Resources/Textures/Missing.hpp"
#include "Rendering/Renderer.hpp"

namespace pip3D
{

    PIP3D_HOT IRAM_ATTR static int collectActiveLightsForBounds(
        const Vector3 &center, float radius,
        const Light *allLights, int allLightCount,
        Light *outLights, int maxLights)
    {
        int count = 0;

        for (int i = 0; i < allLightCount && count < maxLights; ++i)
        {
            if (allLights[i].type == LIGHT_DIRECTIONAL)
                outLights[count++] = allLights[i];
        }

        if (count >= maxLights)
            return count;

        struct LightScore
        {
            int index;
            float score;
        };
        LightScore scores[16];
        int scoreCount = 0;

        for (int i = 0; i < allLightCount && scoreCount < 16; ++i)
        {
            const Light &l = allLights[i];
            if (l.type != LIGHT_POINT)
                continue;

            const float dx = l.position.x - center.x;
            const float dy = l.position.y - center.y;
            const float dz = l.position.z - center.z;
            const float distSq = dx * dx + dy * dy + dz * dz;

            const float maxDist = l.range + radius;
            if (l.range > 0.0f && distSq > maxDist * maxDist)
                continue;

            float atten = 1.0f;
            if (l.range > 0.0f)
                atten = FastMath::fastReciprocal(1.0f + distSq * l.invRangeSq);

            scores[scoreCount++] = {i, l.intensity * atten};
        }

        const int remaining = maxLights - count;
        if (scoreCount > remaining)
        {
            for (int i = 1; i < scoreCount; ++i)
            {
                const LightScore temp = scores[i];
                int j = i - 1;
                while (j >= 0 && scores[j].score < temp.score)
                {
                    scores[j + 1] = scores[j];
                    --j;
                }
                scores[j + 1] = temp;
            }
        }

        for (int i = 0; i < scoreCount && count < maxLights; ++i)
            outLights[count++] = allLights[scores[i].index];

        return count;
    }

    void Renderer::draw(MeshInstance *instance)
    {
        if (unlikely(!instance || !instance->isVisible()))
            return;

        Mesh *mesh = instance->getMesh();
        if (!mesh)
            return;

        if (shadowsEnabled)
        {
            if (instance->getBlobShadow())
                blobShadowQueue_.push_back(instance);
            else if (mesh->getCastShadows())
                shadowQueue_.push_back(instance);
        }

        if (instance->isEmissive())
            emissiveQueue_.push_back(instance);

        opaqueQueue_.push_back(instance);
    }

    void Renderer::flushQueue()
    {
        const size_t n = opaqueQueue_.size();

        if (opaqueSortEnabled_ && n > 1)
        {
            const Camera &cam = cameras[activeCameraIndex];
            const Vector3 camPos = cam.position;
            const Vector3 camFwd = cam.forward();

            float *PIP3D_RESTRICT eyeZ = static_cast<float *>(alloca(n * sizeof(float)));
            for (size_t i = 0; i < n; ++i)
                eyeZ[i] = Culling::computeEyeZ(opaqueQueue_[i]->center(), camPos, camFwd);

            for (size_t i = 1; i < n; ++i)
            {
                const float keyZ = eyeZ[i];
                MeshInstance *keyInst = opaqueQueue_[i];
                size_t j = i;
                while (j > 0 && eyeZ[j - 1] > keyZ)
                {
                    eyeZ[j] = eyeZ[j - 1];
                    opaqueQueue_[j] = opaqueQueue_[j - 1];
                    --j;
                }
                eyeZ[j] = keyZ;
                opaqueQueue_[j] = keyInst;
            }
        }

        for (size_t i = 0; i < opaqueQueue_.size(); ++i)
            drawMeshInstanceInternal(opaqueQueue_[i], false);

        for (size_t i = 0; i < shadowQueue_.size(); ++i)
            drawMeshInstanceShadow(shadowQueue_[i]);

        const float blobOpacity = shadowSettings.shadowOpacity;
        for (size_t i = 0; i < blobShadowQueue_.size(); ++i)
        {
            MeshInstance *inst = blobShadowQueue_[i];
            drawBlobShadow(inst->pos(), inst->radius(), blobOpacity);
        }
    }

    bool Renderer::clipAndDrawNearTextured(const DrawTelemetryClipVert inVerts[3],
                                           float nearD,
                                           const Camera &camera,
                                           const Viewport &viewport,
                                           const Matrix4x4 &viewProjMatrix,
                                           FrameBuffer &framebuffer,
                                           ZBuffer *zBuffer,
                                           const Texture &tex)
    {
        DrawTelemetryClipVert clipped[4];
        int outCount = 0;

#define PIP3D_CLIP_EDGE_T(IDX_A, IDX_B)                               \
    {                                                                 \
        const DrawTelemetryClipVert &P0 = inVerts[IDX_A];             \
        const DrawTelemetryClipVert &P1 = inVerts[IDX_B];             \
        const float d0 = P0.d;                                        \
        const float d1 = P1.d;                                        \
        const bool in0 = d0 >= nearD;                                 \
        const bool in1 = d1 >= nearD;                                 \
        if (in0 && in1)                                               \
            clipped[outCount++] = P1;                                 \
        else if (in0 != in1)                                          \
        {                                                             \
            const float denom = d1 - d0;                              \
            float t = (fabsf(denom) < 1e-6f) ? 0.0f                   \
                                             : (nearD - d0) / denom;  \
            t = clamp(t, 0.0f, 1.0f);                                 \
            const DrawTelemetryClipVert ip = lerpClipVert(P0, P1, t); \
            if (in0)                                                  \
                clipped[outCount++] = ip;                             \
            else                                                      \
            {                                                         \
                clipped[outCount++] = ip;                             \
                clipped[outCount++] = P1;                             \
            }                                                         \
        }                                                             \
    }
        PIP3D_CLIP_EDGE_T(0, 1);
        PIP3D_CLIP_EDGE_T(1, 2);
        PIP3D_CLIP_EDGE_T(2, 0);
#undef PIP3D_CLIP_EDGE_T

        if (outCount < 3)
            return false;

        const float viewportHalfWidth = static_cast<float>(viewport.width) * 0.5f;
        const float viewportHalfHeight = static_cast<float>(viewport.height) * 0.5f;
        const int16_t bandTop = g_bandOffsetY;
        const int16_t bandBottom = static_cast<int16_t>(bandTop + g_bandHeight);
        const DisplayConfig &framebufferConfig = framebuffer.getConfig();
        const float viewportWidth = static_cast<float>(viewport.width);
        const float bandTopF = static_cast<float>(bandTop);
        uint16_t *const frameBuffer = framebuffer.getBuffer();

        Vector3 proj[4];
        for (int i = 0; i < outCount; ++i)
            proj[i] = CameraController::project(clipped[i].pos, viewProjMatrix,
                                                viewportHalfWidth, viewportHalfHeight, 0, 0);

        auto drawTri = [&](int a, int b, int c) -> bool
        {
            const Vector3 &p0 = proj[a];
            const Vector3 &p1 = proj[b];
            const Vector3 &p2 = proj[c];

            const float minY = (p0.y < p1.y) ? ((p0.y < p2.y) ? p0.y : p2.y) : ((p1.y < p2.y) ? p1.y : p2.y);
            const float maxY = (p0.y > p1.y) ? ((p0.y > p2.y) ? p0.y : p2.y) : ((p1.y > p2.y) ? p1.y : p2.y);
            if (maxY < bandTop || minY >= bandBottom)
                return false;

            const float minX = (p0.x < p1.x) ? ((p0.x < p2.x) ? p0.x : p2.x) : ((p1.x < p2.x) ? p1.x : p2.x);
            const float maxX = (p0.x > p1.x) ? ((p0.x > p2.x) ? p0.x : p2.x) : ((p1.x > p2.x) ? p1.x : p2.x);
            if (maxX < 0.0f || minX >= viewportWidth)
                return false;

            Rasterizer::fillTriangleTextured(
                p0.x, p0.y - bandTopF, p0.z,
                p1.x, p1.y - bandTopF, p1.z,
                p2.x, p2.y - bandTopF, p2.z,
                clipped[a].u, clipped[a].v,
                clipped[b].u, clipped[b].v,
                clipped[c].u, clipped[c].v,
                clipped[a].d, clipped[b].d, clipped[c].d,
                clipped[a].lr, clipped[a].lg, clipped[a].lb,
                clipped[b].lr, clipped[b].lg, clipped[b].lb,
                clipped[c].lr, clipped[c].lg, clipped[c].lb,
                tex, frameBuffer, zBuffer, framebufferConfig);
            return true;
        };

        bool drew = drawTri(0, 1, 2);
        if (outCount == 4)
            drew |= drawTri(0, 2, 3);
        return drew;
    }

    IRAM_ATTR void Renderer::drawMeshInstanceInternal(MeshInstance *instance, bool performFrustumCull)
    {
        if (!instance || !instance->isVisible())
            return;

        Mesh *mesh = instance->getMesh();
        if (!mesh)
            return;

        const Vector3 center = instance->center();
        const float radius = instance->radius();

        if (performFrustumCull)
        {
            if (!frustum.testSphere(center, radius))
            {
                statsInstancesFrustumCulled++;
                return;
            }
        }

        const Camera &cam = cameras[activeCameraIndex];
        const Vector3 &camFwd = cam.forward();

        const float zEye = Culling::computeEyeZ(center, cam.position, camFwd);

        float radiusPixels = 0.0f;
        if (zEye > cam.nearPlane)
        {
            const float projScale = Culling::ensureProjScale(cam, viewport);
            radiusPixels = Culling::computeScreenRadius(radius, zEye, projScale);

            if (radiusPixels < 1.0f)
            {
                statsInstancesTotal++;
                return;
            }
        }

        drawMeshInstanceBanded(instance, zEye, radiusPixels);
    }

    PIP3D_HOT IRAM_ATTR static void buildChunkBandCache(
        Renderer &r,
        MeshInstance *instance,
        ChunkBandCache &cache,
        uint32_t frameStamp)
    {
        Mesh *mesh = instance->getMesh();
        const uint32_t chunkCount = mesh->numChunks();
        const MeshChunk *PIP3D_RESTRICT chunks = mesh->chunkData();
        const float qs = mesh->getQScale();

        const Camera &cam = r.getCamera();
        const Vector3 camPos = cam.position;
        const Vector3 camFwd = cam.forward();
        const float nearPlane = cam.nearPlane;
        const float projScale = Culling::ensureProjScale(cam, r.getViewport());
        const Frustum &frustum = r.getFrustum();
        const Matrix4x4 &viewProjMatrix = r.getViewProjMatrix();
        const Matrix4x4 &worldTransform = instance->transform();

        const float viewportHalfWidth = static_cast<float>(r.getViewport().width) * 0.5f;
        const float viewportHalfHeight = static_cast<float>(r.getViewport().height) * 0.5f;
        const float viewportWidthF = static_cast<float>(r.getViewport().width);

        const float r00 = fabsf(worldTransform.m[0]), r01 = fabsf(worldTransform.m[4]), r02 = fabsf(worldTransform.m[8]);
        const float r10 = fabsf(worldTransform.m[1]), r11 = fabsf(worldTransform.m[5]), r12 = fabsf(worldTransform.m[9]);
        const float r20 = fabsf(worldTransform.m[2]), r21 = fabsf(worldTransform.m[6]), r22 = fabsf(worldTransform.m[10]);

        const int32_t bandCountInt = static_cast<int32_t>(SCREEN_BAND_COUNT);
        const int32_t bandHeightInt = static_cast<int32_t>(SCREEN_BAND_HEIGHT);

        cache.reset(frameStamp);
        cache.totalChunkCount = chunkCount;

        for (uint32_t i = 0; i < chunkCount; ++i)
        {
            if (cache.visibleCount >= ChunkBandCache::MAX_RECORDS)
                break;

            const MeshChunk &chunk = chunks[i];

            const Vector3 localMin(static_cast<float>(chunk.minX) * qs,
                                   static_cast<float>(chunk.minY) * qs,
                                   static_cast<float>(chunk.minZ) * qs);
            const Vector3 localMax(static_cast<float>(chunk.maxX) * qs,
                                   static_cast<float>(chunk.maxY) * qs,
                                   static_cast<float>(chunk.maxZ) * qs);
            const Vector3 localCenter = (localMin + localMax) * 0.5f;
            const Vector3 localHalf = (localMax - localMin) * 0.5f;

            const Vector3 worldCenter = worldTransform.transformNoDiv(localCenter);
            const float rX = r00 * localHalf.x + r01 * localHalf.y + r02 * localHalf.z;
            const float rY = r10 * localHalf.x + r11 * localHalf.y + r12 * localHalf.z;
            const float rZ = r20 * localHalf.x + r21 * localHalf.y + r22 * localHalf.z;

            const Vector3 worldMin = worldCenter - Vector3(rX, rY, rZ);
            const Vector3 worldMax = worldCenter + Vector3(rX, rY, rZ);

            if (!frustum.testAABB(worldMin, worldMax))
            {
                ++cache.frustumCulledCount;
                continue;
            }

            const float chunkEyeZ = (worldCenter.x - camPos.x) * camFwd.x +
                                    (worldCenter.y - camPos.y) * camFwd.y +
                                    (worldCenter.z - camPos.z) * camFwd.z;

            uint8_t minBand = 0;
            uint8_t maxBand = static_cast<uint8_t>(SCREEN_BAND_COUNT - 1);

            if (chunkEyeZ > nearPlane)
            {
                const float chunkRadius = sqrtf(rX * rX + rY * rY + rZ * rZ);
                const float chunkRadiusPx = chunkRadius * projScale * FastMath::fastReciprocal(chunkEyeZ);
                const Vector3 scrCenter = CameraController::project(worldCenter, viewProjMatrix,
                                                                    viewportHalfWidth, viewportHalfHeight, 0, 0);

                const float chunkMinX = scrCenter.x - chunkRadiusPx;
                const float chunkMaxX = scrCenter.x + chunkRadiusPx;
                if (chunkMaxX < 0.0f || chunkMinX >= viewportWidthF)
                {
                    ++cache.bandCulledCount;
                    continue;
                }

                const float chunkMinY = scrCenter.y - chunkRadiusPx;
                const float chunkMaxY = scrCenter.y + chunkRadiusPx;

                const int32_t minBand32 = static_cast<int32_t>(chunkMinY) / bandHeightInt;
                const int32_t maxBand32 = static_cast<int32_t>(chunkMaxY) / bandHeightInt;

                if (maxBand32 < 0 || minBand32 >= bandCountInt)
                {
                    ++cache.bandCulledCount;
                    continue;
                }

                minBand = static_cast<uint8_t>(clamp(minBand32, 0, bandCountInt - 1));
                maxBand = static_cast<uint8_t>(clamp(maxBand32, 0, bandCountInt - 1));
            }

            cache.records[cache.visibleCount++] = {
                static_cast<uint16_t>(i),
                minBand,
                maxBand};
        }

        const uint8_t nb = static_cast<uint8_t>(
            (SCREEN_BAND_COUNT <= ChunkBandCache::MAX_BUCKETS) ? SCREEN_BAND_COUNT
                                                               : ChunkBandCache::MAX_BUCKETS);

        uint16_t bucketSize[ChunkBandCache::MAX_BUCKETS] = {};
        for (uint16_t i = 0; i < cache.visibleCount; ++i)
        {
            const uint8_t mb = cache.records[i].minBand;
            if (mb < nb)
                ++bucketSize[mb];
        }

        uint16_t acc = 0;
        for (uint8_t b = 0; b < nb; ++b)
        {
            cache.bucketStart[b] = acc;
            cache.bucketCount[b] = bucketSize[b];
            acc += bucketSize[b];
        }

        uint16_t cursor[ChunkBandCache::MAX_BUCKETS];
        for (uint8_t b = 0; b < nb; ++b)
            cursor[b] = cache.bucketStart[b];

        for (uint16_t i = 0; i < cache.visibleCount; ++i)
        {
            const uint8_t mb = cache.records[i].minBand;
            if (mb < nb)
                cache.sortedIndices[cursor[mb]++] = i;
        }
    }

    IRAM_ATTR void Renderer::drawMeshInstanceBanded(MeshInstance *instance,
                                                    float zEye, float radiusPixels)
    {
        if (!instance || !instance->isVisible())
            return;

        Mesh *mesh = instance->getMesh();
        if (!mesh)
            return;

        const Camera &cam = cameras[activeCameraIndex];
        if (zEye > cam.nearPlane && radiusPixels < 1.0f)
            return;

        const ShadingMode effectiveMode = instance->getEffectiveShadingMode(shadingMode);

        const Vector3 center = instance->center();
        const float radius = instance->radius();
        const DisplayConfig &framebufferConfig = framebuffer.getConfig();

        const uint16_t instColor565 = instance->color().rgb565;
        float instR, instG, instB;
        MeshRenderer::decodeColorToFloat(instColor565, instR, instG, instB);

        const uint32_t subMeshCount = mesh->numSubMeshes();
        const bool hasSubMeshes = mesh->hasSubMeshes();

        const bool isEmissiveInst = instance->isEmissive();

        bool useUniformColor = (mesh->getSingleColorLighting() || isEmissiveInst) && effectiveMode == SHADING_FLAT;
        uint16_t uniformColor = 0;

        Light localLights[4];
        const int localLightCount = collectActiveLightsForBounds(
            center, radius, lights.data(), activeLightCount, localLights, 4);

        const Matrix4x4 &worldTransform = instance->transform();
        const Vertex *PIP3D_RESTRICT vbase = mesh->vertexData();

        if (useUniformColor)
        {
            if (isEmissiveInst)
            {
                uniformColor = instance->color().rgb565;
            }
            else
            {
                NormalMatrix nmUniform(worldTransform);
                const Vector3 localNormal = (mesh->numVertices() > 0) ? vbase[0].normal.get() : Vector3(0.0f, 1.0f, 0.0f);
                const Vector3 worldNormal = nmUniform.transform(localNormal);

                float litR, litG, litB;
                Shading::calculateLambert(worldNormal, localLights, localLightCount,
                                          instR, instG, instB,
                                          litR, litG, litB);

                const float vx = cam.position.x - center.x;
                const float vy = cam.position.y - center.y;
                const float vz = cam.position.z - center.z;
                const float viewDistSq = vx * vx + vy * vy + vz * vz;
                const float invLen = (viewDistSq > 1e-8f) ? FastMath::fastInvSqrt(viewDistSq) : 0.0f;
                const float dist = viewDistSq * invLen;
                Shading::applyFog(dist, litR, litG, litB, litR, litG, litB);

                uniformColor = Color::fromFloat(litR, litG, litB).rgb565;
            }
        }

        NormalMatrix nmWorld(worldTransform);
        const bool needsWorldNormals = (effectiveMode != SHADING_FLAT);

        const uint32_t vertexCountUsed = mesh->numVertices();
        const uint32_t faceCount = mesh->numFaces();

        DrawCache *const cache = &instance->drawCache();

        Vector3 *PIP3D_RESTRICT worldVerts = nullptr;
        Vector3 *PIP3D_RESTRICT worldNormals = nullptr;
        Vector3 *PIP3D_RESTRICT screenVerts = nullptr;

        const uint32_t frameStamp = g_frameStamp;
        const uint32_t instanceVersion = instance->version();

        const int16_t bandTop = g_bandOffsetY;
        const int16_t bandBottom = static_cast<int16_t>(bandTop + g_bandHeight);
        const float bandTopF = static_cast<float>(bandTop);
        const float viewportWidth = static_cast<float>(viewport.width);
        const float viewportHalfWidth = viewportWidth * 0.5f;
        const float viewportHalfHeight = static_cast<float>(viewport.height) * 0.5f;
        const Vector3 camPos = cam.position;
        const float nearPlane = cam.nearPlane;
        constexpr float kNearClipEps = 1e-4f;
        const float nearClip = nearPlane + kNearClipEps;
        const bool isTextured = mesh->isTextured();
        const bool doBackfaceCull = backfaceCullingEnabled;

        const Texture *meshTexture = isTextured ? mesh->getTexture() : nullptr;
        if (!meshTexture && mesh->wantsTexture())
        {
            meshTexture = &g_missingTexture;
        }
        const bool effectiveTextured = (meshTexture != nullptr);
        const Vector3 &camFwd = cam.forward();

        const uint32_t chunkCount = mesh->numChunks();
        const MeshChunk *PIP3D_RESTRICT chunks = mesh->chunkData();
        const bool hasChunks = (chunkCount > 0 && chunks != nullptr);

        if (!hasChunks)
        {
            if (likely(cache->ensureCapacity(static_cast<uint16_t>(
                                                 vertexCountUsed > 65535 ? 65535 : vertexCountUsed),
                                             needsWorldNormals)))
            {
                worldVerts = cache->worldVerts();
                worldNormals = cache->worldNormals();
                screenVerts = cache->screenVerts();
            }

            if (likely(worldVerts))
            {
                const DrawCache::ProjState projState = cache->beginProjection(frameStamp, instanceVersion);

                if (projState == DrawCache::ProjState::NeedsTransformAndProject)
                {
                    for (uint32_t i = 0; i < vertexCountUsed; ++i)
                    {
                        const Vector3 local = mesh->decodePosition(vbase[i]);
                        const Vector3 world = worldTransform.transformNoDiv(local);
                        worldVerts[i] = world;
                        screenVerts[i] = CameraController::project(world, viewProjMatrix,
                                                                   viewportHalfWidth, viewportHalfHeight, 0, 0);
                        if (needsWorldNormals)
                            worldNormals[i] = nmWorld.transform(vbase[i].normal.get());
                    }
                    cache->commitProjection(frameStamp, instanceVersion);
                }
                else if (projState == DrawCache::ProjState::NeedsReproject)
                {
                    for (uint32_t i = 0; i < vertexCountUsed; ++i)
                        screenVerts[i] = CameraController::project(worldVerts[i], viewProjMatrix,
                                                                   viewportHalfWidth, viewportHalfHeight, 0, 0);
                    cache->commitProjection(frameStamp, instanceVersion);
                }
            }

            const bool is32 = mesh->isIndex32();
            const Face16 *PIP3D_RESTRICT fbase16 = is32 ? nullptr : mesh->faceData16();
            const Face32 *PIP3D_RESTRICT fbase32 = is32 ? mesh->faceData32() : nullptr;

            uint32_t currentSubMesh = 0;

            for (uint32_t i = 0; i < faceCount; ++i)
            {
                uint32_t vIdx0, vIdx1, vIdx2;
                if (likely(!is32))
                {
                    vIdx0 = fbase16[i].v0;
                    vIdx1 = fbase16[i].v1;
                    vIdx2 = fbase16[i].v2;
                }
                else
                {
                    vIdx0 = fbase32[i].v0;
                    vIdx1 = fbase32[i].v1;
                    vIdx2 = fbase32[i].v2;
                }

                Vector3 v0, v1, v2;
                if (likely(worldVerts))
                {
                    v0 = worldVerts[vIdx0];
                    v1 = worldVerts[vIdx1];
                    v2 = worldVerts[vIdx2];
                }
                else
                {
                    v0 = worldTransform.transformNoDiv(mesh->decodePosition(vbase[vIdx0]));
                    v1 = worldTransform.transformNoDiv(mesh->decodePosition(vbase[vIdx1]));
                    v2 = worldTransform.transformNoDiv(mesh->decodePosition(vbase[vIdx2]));
                }

                const float d0 = (v0.x - camPos.x) * camFwd.x + (v0.y - camPos.y) * camFwd.y + (v0.z - camPos.z) * camFwd.z;
                const float d1 = (v1.x - camPos.x) * camFwd.x + (v1.y - camPos.y) * camFwd.y + (v1.z - camPos.z) * camFwd.z;
                const float d2 = (v2.x - camPos.x) * camFwd.x + (v2.y - camPos.y) * camFwd.y + (v2.z - camPos.z) * camFwd.z;

                if (unlikely(d0 < nearClip && d1 < nearClip && d2 < nearClip))
                {
                    statsTrianglesBackfaceCulled++;
                    continue;
                }

                const bool partiallyClipped = unlikely(d0 < nearClip || d1 < nearClip || d2 < nearClip);

                if (doBackfaceCull)
                {
                    const float e1x = v1.x - v0.x, e1y = v1.y - v0.y, e1z = v1.z - v0.z;
                    const float e2x = v2.x - v0.x, e2y = v2.y - v0.y, e2z = v2.z - v0.z;
                    const float nx = e1y * e2z - e1z * e2y;
                    const float ny = e1z * e2x - e1x * e2z;
                    const float nz = e1x * e2y - e1y * e2x;
                    const float vx = v0.x - camPos.x;
                    const float vy = v0.y - camPos.y;
                    const float vz = v0.z - camPos.z;
                    if (nx * vx + ny * vy + nz * vz >= 0.0f)
                    {
                        statsTrianglesBackfaceCulled++;
                        continue;
                    }
                }

                Vector3 p0, p1, p2;
                if (likely(screenVerts))
                {
                    p0 = screenVerts[vIdx0];
                    p1 = screenVerts[vIdx1];
                    p2 = screenVerts[vIdx2];
                }
                else
                {
                    p0 = CameraController::project(v0, viewProjMatrix, viewportHalfWidth, viewportHalfHeight, 0, 0);
                    p1 = CameraController::project(v1, viewProjMatrix, viewportHalfWidth, viewportHalfHeight, 0, 0);
                    p2 = CameraController::project(v2, viewProjMatrix, viewportHalfWidth, viewportHalfHeight, 0, 0);
                }

                if (!partiallyClipped)
                {
                    const float minY = (p0.y < p1.y) ? ((p0.y < p2.y) ? p0.y : p2.y) : ((p1.y < p2.y) ? p1.y : p2.y);
                    const float maxY = (p0.y > p1.y) ? ((p0.y > p2.y) ? p0.y : p2.y) : ((p1.y > p2.y) ? p1.y : p2.y);
                    if (maxY < bandTop || minY >= bandBottom)
                        continue;
                    const float minX = (p0.x < p1.x) ? ((p0.x < p2.x) ? p0.x : p2.x) : ((p1.x < p2.x) ? p1.x : p2.x);
                    const float maxX = (p0.x > p1.x) ? ((p0.x > p2.x) ? p0.x : p2.x) : ((p1.x > p2.x) ? p1.x : p2.x);
                    if (maxX < 0.0f || minX >= viewportWidth)
                        continue;
                }

                statsTrianglesTotal++;

                if (!partiallyClipped)
                {
                    const float area = (p1.x - p0.x) * (p2.y - p0.y) - (p2.x - p0.x) * (p1.y - p0.y);
                    if (fabsf(area) <= 1.0f)
                    {
                        statsTrianglesBackfaceCulled++;
                        continue;
                    }
                }

                float faceR = instR, faceG = instG, faceB = instB;
                if (hasSubMeshes)
                {
                    while (currentSubMesh < subMeshCount &&
                           i >= mesh->subMeshFaceEnd(currentSubMesh))
                        ++currentSubMesh;
                    if (currentSubMesh < subMeshCount)
                    {
                        float sr, sg, sb;
                        mesh->subMeshColor(currentSubMesh).toFloat(sr, sg, sb);
                        faceR = instR * sr;
                        faceG = instG * sg;
                        faceB = instB * sb;
                    }
                }

                if (effectiveTextured)
                {
                    const Vertex &vert0 = vbase[vIdx0];
                    const Vertex &vert1 = vbase[vIdx1];
                    const Vertex &vert2 = vbase[vIdx2];

                    float lr0, lg0, lb0, lr1, lg1, lb1, lr2, lg2, lb2;

                    if (effectiveMode == SHADING_GOURAUD)
                    {
                        Vector3 n0 = worldNormals ? worldNormals[vIdx0] : vbase[vIdx0].normal.get();
                        Vector3 n1 = worldNormals ? worldNormals[vIdx1] : vbase[vIdx1].normal.get();
                        Vector3 n2 = worldNormals ? worldNormals[vIdx2] : vbase[vIdx2].normal.get();
                        Shading::calculateVertexLightingGouraud(v0, n0, camPos,
                                                                localLights, localLightCount, faceR, faceG, faceB, lr0, lg0, lb0);
                        Shading::calculateVertexLightingGouraud(v1, n1, camPos,
                                                                localLights, localLightCount, faceR, faceG, faceB, lr1, lg1, lb1);
                        Shading::calculateVertexLightingGouraud(v2, n2, camPos,
                                                                localLights, localLightCount, faceR, faceG, faceB, lr2, lg2, lb2);
                    }
                    else
                    {
                        Shading::calculateFaceLighting(
                            v0, v1, v2, camPos,
                            localLights, localLightCount,
                            faceR, faceG, faceB,
                            lr0, lg0, lb0);
                        lr1 = lr0;
                        lg1 = lg0;
                        lb1 = lb0;
                        lr2 = lr0;
                        lg2 = lg0;
                        lb2 = lb0;
                    }

                    if (!partiallyClipped)
                    {
                        Rasterizer::fillTriangleTextured(
                            p0.x, p0.y - bandTopF, p0.z,
                            p1.x, p1.y - bandTopF, p1.z,
                            p2.x, p2.y - bandTopF, p2.z,
                            vert0.tu, vert0.tv,
                            vert1.tu, vert1.tv,
                            vert2.tu, vert2.tv,
                            d0, d1, d2,
                            lr0, lg0, lb0,
                            lr1, lg1, lb1,
                            lr2, lg2, lb2,
                            *meshTexture,
                            framebuffer.getBuffer(),
                            &zBuffer,
                            framebufferConfig);
                    }
                    else
                    {
                        const DrawTelemetryClipVert cv[3] = {
                            {v0, vert0.tu, vert0.tv, d0, lr0, lg0, lb0},
                            {v1, vert1.tu, vert1.tv, d1, lr1, lg1, lb1},
                            {v2, vert2.tu, vert2.tv, d2, lr2, lg2, lb2}};
                        clipAndDrawNearTextured(
                            cv, nearClip,
                            cam, viewport, viewProjMatrix,
                            framebuffer, &zBuffer,
                            *meshTexture);
                    }
                    continue;
                }

                switch (effectiveMode)
                {
                case SHADING_FLAT:
                    MeshRenderer::drawTriangle3D_Preprojected(
                        v0, v1, v2, p0, p1, p2,
                        d0, d1, d2,
                        partiallyClipped,
                        nearClip,
                        camPos,
                        Color::fromFloat(faceR, faceG, faceB).rgb565,
                        viewProjMatrix,
                        viewport,
                        viewportHalfWidth, viewportHalfHeight, viewportWidth,
                        bandTop, bandBottom, bandTopF,
                        framebuffer, &zBuffer,
                        localLights, localLightCount,
                        useUniformColor, uniformColor);
                    break;

                case SHADING_GOURAUD:
                {
                    Vector3 n0 = worldNormals ? worldNormals[vIdx0] : vbase[vIdx0].normal.get();
                    Vector3 n1 = worldNormals ? worldNormals[vIdx1] : vbase[vIdx1].normal.get();
                    Vector3 n2 = worldNormals ? worldNormals[vIdx2] : vbase[vIdx2].normal.get();

                    float lr0, lg0, lb0, lr1, lg1, lb1, lr2, lg2, lb2;
                    Shading::calculateVertexLightingGouraud(v0, n0, camPos,
                                                            localLights, localLightCount, faceR, faceG, faceB, lr0, lg0, lb0);
                    Shading::calculateVertexLightingGouraud(v1, n1, camPos,
                                                            localLights, localLightCount, faceR, faceG, faceB, lr1, lg1, lb1);
                    Shading::calculateVertexLightingGouraud(v2, n2, camPos,
                                                            localLights, localLightCount, faceR, faceG, faceB, lr2, lg2, lb2);

                    if (likely(!partiallyClipped))
                    {
                        MeshRenderer::drawTriangle3D_Smooth_Preprojected(
                            p0, p1, p2,
                            lr0, lg0, lb0, lr1, lg1, lb1, lr2, lg2, lb2,
                            viewportWidth, bandTop, bandBottom, bandTopF,
                            framebuffer, &zBuffer);
                    }
                    else
                    {
                        const MeshRenderer::ClipVertSmooth cv[3] = {
                            {v0, d0, lr0, lg0, lb0},
                            {v1, d1, lr1, lg1, lb1},
                            {v2, d2, lr2, lg2, lb2}};
                        MeshRenderer::clipAndDrawNearSmooth(
                            cv, nearClip, viewport, viewProjMatrix,
                            framebuffer, &zBuffer);
                    }
                    break;
                }

                case SHADING_PHONG:
                {
                    Vector3 n0 = worldNormals ? worldNormals[vIdx0] : vbase[vIdx0].normal.get();
                    Vector3 n1 = worldNormals ? worldNormals[vIdx1] : vbase[vIdx1].normal.get();
                    Vector3 n2 = worldNormals ? worldNormals[vIdx2] : vbase[vIdx2].normal.get();

                    if (likely(!partiallyClipped))
                    {
                        MeshRenderer::drawTriangle3D_Phong_Preprojected(
                            v0, v1, v2, p0, p1, p2,
                            n0, n1, n2, d0, d1, d2,
                            faceR, faceG, faceB, camPos,
                            viewportWidth, bandTop, bandBottom, bandTopF,
                            framebuffer, &zBuffer,
                            localLights, localLightCount);
                    }
                    else
                    {
                        const MeshRenderer::ClipVertPhong cv[3] = {
                            {v0, n0, d0},
                            {v1, n1, d1},
                            {v2, n2, d2}};
                        MeshRenderer::clipAndDrawNearPhong(
                            cv, nearClip,
                            faceR, faceG, faceB, camPos,
                            viewport, viewProjMatrix,
                            framebuffer, &zBuffer,
                            localLights, localLightCount);
                    }
                    break;
                }
                }
            }
            return;
        }

        const uint16_t chunkCacheVerts = mesh->maxChunkVertexCount();
        if (likely(cache->ensureCapacity(chunkCacheVerts, needsWorldNormals)))
        {
            worldVerts = cache->worldVerts();
            worldNormals = cache->worldNormals();
            screenVerts = cache->screenVerts();
        }

        const bool is32 = mesh->isIndex32();
        const Face16 *PIP3D_RESTRICT fbase16 = is32 ? nullptr : mesh->faceData16();
        const Face32 *PIP3D_RESTRICT fbase32 = is32 ? mesh->faceData32() : nullptr;

        const int32_t bandIndex32 = static_cast<int32_t>(g_bandOffsetY) / static_cast<int32_t>(SCREEN_BAND_HEIGHT);
        const uint8_t bandIndex = static_cast<uint8_t>(
            (bandIndex32 < 0) ? 0
                              : ((bandIndex32 >= static_cast<int32_t>(SCREEN_BAND_COUNT))
                                     ? static_cast<int32_t>(SCREEN_BAND_COUNT - 1)
                                     : bandIndex32));

        ChunkBandCache &chunkCache = instance->chunkBandCache();
        if (chunkCache.frameStamp != frameStamp || chunkCache.instanceVersion != instanceVersion)
        {
            buildChunkBandCache(*this, instance, chunkCache, frameStamp);
            chunkCache.instanceVersion = instanceVersion;
        }

        const uint8_t nb = static_cast<uint8_t>(
            (SCREEN_BAND_COUNT <= ChunkBandCache::MAX_BUCKETS) ? SCREEN_BAND_COUNT
                                                               : ChunkBandCache::MAX_BUCKETS);

        uint32_t currentSubMesh = 0;

        for (uint8_t b = 0; b <= bandIndex && b < nb; ++b)
        {
            const uint16_t bStart = chunkCache.bucketStart[b];
            const uint16_t bCount = chunkCache.bucketCount[b];
            if (bCount == 0)
                continue;

            for (uint16_t k = 0; k < bCount; ++k)
            {
                const uint16_t recIdx = chunkCache.sortedIndices[bStart + k];
                const ChunkBandRecord &rec = chunkCache.records[recIdx];

                if (bandIndex > rec.maxBand)
                    continue;

                const MeshChunk &chunk = chunks[rec.chunkIdx];
                const uint16_t chunkVCount = chunk.vCount;
                const uint32_t chunkVOffset = chunk.vOffset;
                const uint32_t chunkFOffset = chunk.faceOffset;
                const uint16_t chunkFCount = chunk.faceCount;

                if (likely(worldVerts && screenVerts))
                {
                    if (chunkCache.currentChunkIdx != rec.chunkIdx)
                    {
                        const Vertex *PIP3D_RESTRICT chunkVBase = vbase + chunkVOffset;
                        for (uint16_t i = 0; i < chunkVCount; ++i)
                        {
                            const Vector3 local = mesh->decodePosition(chunkVBase[i]);
                            const Vector3 world = worldTransform.transformNoDiv(local);
                            worldVerts[i] = world;
                            screenVerts[i] = CameraController::project(world, viewProjMatrix,
                                                                       viewportHalfWidth, viewportHalfHeight, 0, 0);
                            if (needsWorldNormals)
                                worldNormals[i] = nmWorld.transform(chunkVBase[i].normal.get());
                        }
                        chunkCache.currentChunkIdx = rec.chunkIdx;
                        currentSubMesh = 0;
                    }
                }

                for (uint16_t fi = 0; fi < chunkFCount; ++fi)
                {
                    const uint32_t faceIdx = chunkFOffset + fi;
                    uint32_t vIdx0, vIdx1, vIdx2;
                    if (likely(!is32))
                    {
                        vIdx0 = fbase16[faceIdx].v0;
                        vIdx1 = fbase16[faceIdx].v1;
                        vIdx2 = fbase16[faceIdx].v2;
                    }
                    else
                    {
                        vIdx0 = fbase32[faceIdx].v0;
                        vIdx1 = fbase32[faceIdx].v1;
                        vIdx2 = fbase32[faceIdx].v2;
                    }

                    Vector3 v0, v1, v2;
                    if (likely(worldVerts))
                    {
                        v0 = worldVerts[vIdx0];
                        v1 = worldVerts[vIdx1];
                        v2 = worldVerts[vIdx2];
                    }
                    else
                    {
                        const Vertex *chunkVBase = vbase + chunkVOffset;
                        v0 = worldTransform.transformNoDiv(mesh->decodePosition(chunkVBase[vIdx0]));
                        v1 = worldTransform.transformNoDiv(mesh->decodePosition(chunkVBase[vIdx1]));
                        v2 = worldTransform.transformNoDiv(mesh->decodePosition(chunkVBase[vIdx2]));
                    }

                    const float d0 = (v0.x - camPos.x) * camFwd.x + (v0.y - camPos.y) * camFwd.y + (v0.z - camPos.z) * camFwd.z;
                    const float d1 = (v1.x - camPos.x) * camFwd.x + (v1.y - camPos.y) * camFwd.y + (v1.z - camPos.z) * camFwd.z;
                    const float d2 = (v2.x - camPos.x) * camFwd.x + (v2.y - camPos.y) * camFwd.y + (v2.z - camPos.z) * camFwd.z;

                    if (unlikely(d0 < nearClip && d1 < nearClip && d2 < nearClip))
                    {
                        statsTrianglesBackfaceCulled++;
                        continue;
                    }

                    const bool partiallyClipped = unlikely(d0 < nearClip || d1 < nearClip || d2 < nearClip);

                    if (doBackfaceCull)
                    {
                        const float e1x = v1.x - v0.x, e1y = v1.y - v0.y, e1z = v1.z - v0.z;
                        const float e2x = v2.x - v0.x, e2y = v2.y - v0.y, e2z = v2.z - v0.z;
                        const float nx = e1y * e2z - e1z * e2y;
                        const float ny = e1z * e2x - e1x * e2z;
                        const float nz = e1x * e2y - e1y * e2x;
                        const float vx = v0.x - camPos.x;
                        const float vy = v0.y - camPos.y;
                        const float vz = v0.z - camPos.z;
                        if (nx * vx + ny * vy + nz * vz >= 0.0f)
                        {
                            statsTrianglesBackfaceCulled++;
                            continue;
                        }
                    }

                    Vector3 p0, p1, p2;
                    if (likely(screenVerts))
                    {
                        p0 = screenVerts[vIdx0];
                        p1 = screenVerts[vIdx1];
                        p2 = screenVerts[vIdx2];
                    }
                    else
                    {
                        p0 = CameraController::project(v0, viewProjMatrix, viewportHalfWidth, viewportHalfHeight, 0, 0);
                        p1 = CameraController::project(v1, viewProjMatrix, viewportHalfWidth, viewportHalfHeight, 0, 0);
                        p2 = CameraController::project(v2, viewProjMatrix, viewportHalfWidth, viewportHalfHeight, 0, 0);
                    }

                    if (!partiallyClipped)
                    {
                        const float minY = (p0.y < p1.y) ? ((p0.y < p2.y) ? p0.y : p2.y) : ((p1.y < p2.y) ? p1.y : p2.y);
                        const float maxY = (p0.y > p1.y) ? ((p0.y > p2.y) ? p0.y : p2.y) : ((p1.y > p2.y) ? p1.y : p2.y);
                        if (maxY < bandTop || minY >= bandBottom)
                            continue;
                        const float minX = (p0.x < p1.x) ? ((p0.x < p2.x) ? p0.x : p2.x) : ((p1.x < p2.x) ? p1.x : p2.x);
                        const float maxX = (p0.x > p1.x) ? ((p0.x > p2.x) ? p0.x : p2.x) : ((p1.x > p2.x) ? p1.x : p2.x);
                        if (maxX < 0.0f || minX >= viewportWidth)
                            continue;
                    }

                    statsTrianglesTotal++;

                    if (!partiallyClipped)
                    {
                        const float area = (p1.x - p0.x) * (p2.y - p0.y) - (p2.x - p0.x) * (p1.y - p0.y);
                        if (fabsf(area) <= 1.0f)
                        {
                            statsTrianglesBackfaceCulled++;
                            continue;
                        }
                    }

                    float faceR = instR, faceG = instG, faceB = instB;
                    if (hasSubMeshes)
                    {
                        while (currentSubMesh < subMeshCount &&
                               faceIdx >= mesh->subMeshFaceEnd(currentSubMesh))
                            ++currentSubMesh;
                        if (currentSubMesh < subMeshCount)
                        {
                            float sr, sg, sb;
                            mesh->subMeshColor(currentSubMesh).toFloat(sr, sg, sb);
                            faceR = instR * sr;
                            faceG = instG * sg;
                            faceB = instB * sb;
                        }
                    }

                    if (effectiveTextured)
                    {
                        const Vertex *chunkVBase = vbase + chunkVOffset;
                        const Vertex &vert0 = chunkVBase[vIdx0];
                        const Vertex &vert1 = chunkVBase[vIdx1];
                        const Vertex &vert2 = chunkVBase[vIdx2];

                        float lr0, lg0, lb0, lr1, lg1, lb1, lr2, lg2, lb2;

                        if (effectiveMode == SHADING_GOURAUD)
                        {
                            Vector3 n0 = worldNormals ? worldNormals[vIdx0] : chunkVBase[vIdx0].normal.get();
                            Vector3 n1 = worldNormals ? worldNormals[vIdx1] : chunkVBase[vIdx1].normal.get();
                            Vector3 n2 = worldNormals ? worldNormals[vIdx2] : chunkVBase[vIdx2].normal.get();
                            Shading::calculateVertexLightingGouraud(v0, n0, camPos,
                                                                    localLights, localLightCount, faceR, faceG, faceB, lr0, lg0, lb0);
                            Shading::calculateVertexLightingGouraud(v1, n1, camPos,
                                                                    localLights, localLightCount, faceR, faceG, faceB, lr1, lg1, lb1);
                            Shading::calculateVertexLightingGouraud(v2, n2, camPos,
                                                                    localLights, localLightCount, faceR, faceG, faceB, lr2, lg2, lb2);
                        }
                        else
                        {
                            Shading::calculateFaceLighting(v0, v1, v2, camPos,
                                                           localLights, localLightCount, faceR, faceG, faceB,
                                                           lr0, lg0, lb0);
                            lr1 = lr0;
                            lg1 = lg0;
                            lb1 = lb0;
                            lr2 = lr0;
                            lg2 = lg0;
                            lb2 = lb0;
                        }

                        if (!partiallyClipped)
                        {
                            Rasterizer::fillTriangleTextured(
                                p0.x, p0.y - bandTopF, p0.z,
                                p1.x, p1.y - bandTopF, p1.z,
                                p2.x, p2.y - bandTopF, p2.z,
                                vert0.tu, vert0.tv,
                                vert1.tu, vert1.tv,
                                vert2.tu, vert2.tv,
                                d0, d1, d2,
                                lr0, lg0, lb0, lr1, lg1, lb1, lr2, lg2, lb2,
                                *meshTexture,
                                framebuffer.getBuffer(), &zBuffer, framebufferConfig);
                        }
                        else
                        {
                            const DrawTelemetryClipVert cv[3] = {
                                {v0, vert0.tu, vert0.tv, d0, lr0, lg0, lb0},
                                {v1, vert1.tu, vert1.tv, d1, lr1, lg1, lb1},
                                {v2, vert2.tu, vert2.tv, d2, lr2, lg2, lb2}};
                            clipAndDrawNearTextured(
                                cv, nearClip, cam, viewport, viewProjMatrix,
                                framebuffer, &zBuffer, *meshTexture);
                        }
                        continue;
                    }

                    switch (effectiveMode)
                    {
                    case SHADING_FLAT:
                        MeshRenderer::drawTriangle3D_Preprojected(
                            v0, v1, v2, p0, p1, p2,
                            d0, d1, d2,
                            partiallyClipped, nearClip, camPos,
                            Color::fromFloat(faceR, faceG, faceB).rgb565,
                            viewProjMatrix, viewport,
                            viewportHalfWidth, viewportHalfHeight, viewportWidth,
                            bandTop, bandBottom, bandTopF,
                            framebuffer, &zBuffer,
                            localLights, localLightCount,
                            useUniformColor, uniformColor);
                        break;

                    case SHADING_GOURAUD:
                    {
                        const Vertex *chunkVBase = vbase + chunkVOffset;
                        Vector3 n0 = worldNormals ? worldNormals[vIdx0] : chunkVBase[vIdx0].normal.get();
                        Vector3 n1 = worldNormals ? worldNormals[vIdx1] : chunkVBase[vIdx1].normal.get();
                        Vector3 n2 = worldNormals ? worldNormals[vIdx2] : chunkVBase[vIdx2].normal.get();

                        float lr0, lg0, lb0, lr1, lg1, lb1, lr2, lg2, lb2;
                        Shading::calculateVertexLightingGouraud(v0, n0, camPos,
                                                                localLights, localLightCount, faceR, faceG, faceB, lr0, lg0, lb0);
                        Shading::calculateVertexLightingGouraud(v1, n1, camPos,
                                                                localLights, localLightCount, faceR, faceG, faceB, lr1, lg1, lb1);
                        Shading::calculateVertexLightingGouraud(v2, n2, camPos,
                                                                localLights, localLightCount, faceR, faceG, faceB, lr2, lg2, lb2);

                        if (likely(!partiallyClipped))
                        {
                            MeshRenderer::drawTriangle3D_Smooth_Preprojected(
                                p0, p1, p2,
                                lr0, lg0, lb0, lr1, lg1, lb1, lr2, lg2, lb2,
                                viewportWidth, bandTop, bandBottom, bandTopF,
                                framebuffer, &zBuffer);
                        }
                        else
                        {
                            const MeshRenderer::ClipVertSmooth cv[3] = {
                                {v0, d0, lr0, lg0, lb0},
                                {v1, d1, lr1, lg1, lb1},
                                {v2, d2, lr2, lg2, lb2}};
                            MeshRenderer::clipAndDrawNearSmooth(
                                cv, nearClip, viewport, viewProjMatrix,
                                framebuffer, &zBuffer);
                        }
                        break;
                    }

                    case SHADING_PHONG:
                    {
                        const Vertex *chunkVBase = vbase + chunkVOffset;
                        Vector3 n0 = worldNormals ? worldNormals[vIdx0] : chunkVBase[vIdx0].normal.get();
                        Vector3 n1 = worldNormals ? worldNormals[vIdx1] : chunkVBase[vIdx1].normal.get();
                        Vector3 n2 = worldNormals ? worldNormals[vIdx2] : chunkVBase[vIdx2].normal.get();

                        if (likely(!partiallyClipped))
                        {
                            MeshRenderer::drawTriangle3D_Phong_Preprojected(
                                v0, v1, v2, p0, p1, p2,
                                n0, n1, n2, d0, d1, d2,
                                faceR, faceG, faceB, camPos,
                                viewportWidth, bandTop, bandBottom, bandTopF,
                                framebuffer, &zBuffer,
                                localLights, localLightCount);
                        }
                        else
                        {
                            const MeshRenderer::ClipVertPhong cv[3] = {
                                {v0, n0, d0},
                                {v1, n1, d1},
                                {v2, n2, d2}};
                            MeshRenderer::clipAndDrawNearPhong(
                                cv, nearClip, faceR, faceG, faceB, camPos,
                                viewport, viewProjMatrix,
                                framebuffer, &zBuffer,
                                localLights, localLightCount);
                        }
                        break;
                    }
                    }
                }
            }
        }
    }

    void Renderer::drawTriangle3D(const Vector3 &v0, const Vector3 &v1, const Vector3 &v2, uint16_t color)
    {
        MeshRenderer::drawTriangle3D(v0, v1, v2, color, cameras[activeCameraIndex], viewport,
                                     viewProjMatrix, framebuffer, &zBuffer, lights.data(), activeLightCount,
                                     false, 0);
    }
}