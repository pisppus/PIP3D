#ifndef MESHRENDERER_H
#define MESHRENDERER_H

#include "../../Core/Core.h"
#include "../../Core/Camera.h"
#include "../../Core/Frustum.h"
#include "../../Math/Math.h"
#include "../../Geometry/Mesh.h"
#include "../Display/ZBuffer.h"
#include "../Display/FrameBuffer.h"
#include "../Lighting/Lighting.h"
#include "../Rasterizer/Rasterizer.h"
#include "../Rasterizer/Shading.h"
#include "CameraController.h"

namespace pip3D
{

    class MeshRenderer
    {
    private:
        static void decodeColorToFloat(uint16_t color,
                                       float &baseR,
                                       float &baseG,
                                       float &baseB)
        {
            baseR = ((color >> 11) & 0x1F) / 31.0f;
            baseG = ((color >> 5) & 0x3F) / 63.0f;
            baseB = (color & 0x1F) / 31.0f;
        }

        static void drawTriangle3D_Color_Preprojected(const Vector3 &v0, const Vector3 &v1, const Vector3 &v2,
                                                      const Vector3 &p0, const Vector3 &p1, const Vector3 &p2,
                                                      float baseR,
                                                      float baseG,
                                                      float baseB,
                                                      const Camera &camera,
                                                      const Viewport &viewport,
                                                      const Matrix4x4 &viewProjMatrix,
                                                      FrameBuffer &framebuffer,
                                                      ZBuffer<320, 240> *zBuffer,
                                                      const Light *lights,
                                                      int activeLightCount,
                                                      bool backfaceCullingEnabled,
                                                      uint32_t &statsTrianglesTotal,
                                                      uint32_t &statsTrianglesBackfaceCulled)
        {
            Vector3 edge1 = v1 - v0;
            Vector3 edge2 = v2 - v0;
            Vector3 normal = edge1.cross(edge2);
            Vector3 viewDir = camera.position - v0;

            if (camera.projectionType == PERSPECTIVE)
            {
                if (p0.z <= 0.0f && p1.z <= 0.0f && p2.z <= 0.0f)
                    return;
            }
            normal.normalize();
            viewDir.normalize();
            Vector3 fragPos = (v0 + v1 + v2) * (1.0f / 3.0f);

            float finalR, finalG, finalB;
            Shading::calculateLighting(fragPos, normal, viewDir,
                                       lights, activeLightCount,
                                       baseR, baseG, baseB,
                                       finalR, finalG, finalB);

            uint16_t shadedColor = Shading::applyDithering(finalR, finalG, finalB, (int16_t)p0.x, (int16_t)p0.y);

            Rasterizer::fillTriangle((int16_t)p0.x, (int16_t)p0.y, p0.z,
                                     (int16_t)p1.x, (int16_t)p1.y, p1.z,
                                     (int16_t)p2.x, (int16_t)p2.y, p2.z,
                                     shadedColor,
                                     framebuffer.getBuffer(),
                                     zBuffer,
                                     framebuffer.getConfig());
        }

        static void drawTriangle3D_Color(const Vector3 &v0, const Vector3 &v1, const Vector3 &v2,
                                         float baseR,
                                         float baseG,
                                         float baseB,
                                         const Camera &camera,
                                         const Viewport &viewport,
                                         const Matrix4x4 &viewProjMatrix,
                                         FrameBuffer &framebuffer,
                                         ZBuffer<320, 240> *zBuffer,
                                         const Light *lights,
                                         int activeLightCount,
                                         bool backfaceCullingEnabled,
                                         uint32_t &statsTrianglesTotal,
                                         uint32_t &statsTrianglesBackfaceCulled)
        {
            Vector3 p0 = CameraController::project(v0, viewProjMatrix, viewport);
            Vector3 p1 = CameraController::project(v1, viewProjMatrix, viewport);
            Vector3 p2 = CameraController::project(v2, viewProjMatrix, viewport);

            drawTriangle3D_Color_Preprojected(v0, v1, v2,
                                              p0, p1, p2,
                                              baseR, baseG, baseB,
                                              camera, viewport, viewProjMatrix,
                                              framebuffer, zBuffer,
                                              lights, activeLightCount,
                                              backfaceCullingEnabled,
                                              statsTrianglesTotal,
                                              statsTrianglesBackfaceCulled);
        }

        static void drawTriangle3D_Clipped_Preprojected(const Vector3 &v0, const Vector3 &v1, const Vector3 &v2,
                                                        const Vector3 &p0, const Vector3 &p1, const Vector3 &p2,
                                                        float baseR,
                                                        float baseG,
                                                        float baseB,
                                                        const Camera &camera,
                                                        const Viewport &viewport,
                                                        const Matrix4x4 &viewProjMatrix,
                                                        FrameBuffer &framebuffer,
                                                        ZBuffer<320, 240> *zBuffer,
                                                        const Light *lights,
                                                        int activeLightCount,
                                                        bool backfaceCullingEnabled,
                                                        uint32_t &statsTrianglesTotal,
                                                        uint32_t &statsTrianglesBackfaceCulled)
        {
            if (camera.projectionType == PERSPECTIVE)
            {
                const Vector3 camPos = camera.position;
                const Vector3 camFwd = camera.forward();
                const float nearD = camera.nearPlane;

                float d0 = (v0 - camPos).dot(camFwd);
                float d1 = (v1 - camPos).dot(camFwd);
                float d2 = (v2 - camPos).dot(camFwd);

                if (d0 < nearD && d1 < nearD && d2 < nearD)
                {
                    return;
                }

                if (d0 >= nearD && d1 >= nearD && d2 >= nearD)
                {
                    drawTriangle3D_Color_Preprojected(v0, v1, v2,
                                                      p0, p1, p2,
                                                      baseR, baseG, baseB,
                                                      camera, viewport, viewProjMatrix,
                                                      framebuffer, zBuffer,
                                                      lights, activeLightCount,
                                                      backfaceCullingEnabled,
                                                      statsTrianglesTotal,
                                                      statsTrianglesBackfaceCulled);
                    return;
                }

                drawTriangle3D_Clipped(v0, v1, v2,
                                       baseR, baseG, baseB,
                                       camera, viewport, viewProjMatrix,
                                       framebuffer, zBuffer,
                                       lights, activeLightCount,
                                       backfaceCullingEnabled,
                                       statsTrianglesTotal,
                                       statsTrianglesBackfaceCulled);
                return;
            }

            drawTriangle3D_Color_Preprojected(v0, v1, v2,
                                              p0, p1, p2,
                                              baseR, baseG, baseB,
                                              camera, viewport, viewProjMatrix,
                                              framebuffer, zBuffer,
                                              lights, activeLightCount,
                                              backfaceCullingEnabled,
                                              statsTrianglesTotal,
                                              statsTrianglesBackfaceCulled);
        }

        static void drawTriangle3D_Clipped(const Vector3 &v0, const Vector3 &v1, const Vector3 &v2,
                                           float baseR,
                                           float baseG,
                                           float baseB,
                                           const Camera &camera,
                                           const Viewport &viewport,
                                           const Matrix4x4 &viewProjMatrix,
                                           FrameBuffer &framebuffer,
                                           ZBuffer<320, 240> *zBuffer,
                                           const Light *lights,
                                           int activeLightCount,
                                           bool backfaceCullingEnabled,
                                           uint32_t &statsTrianglesTotal,
                                           uint32_t &statsTrianglesBackfaceCulled)
        {
            if (camera.projectionType == PERSPECTIVE)
            {
                const Vector3 camPos = camera.position;
                const Vector3 camFwd = camera.forward();
                const float nearD = camera.nearPlane;

                Vector3 inVerts[3] = {v0, v1, v2};
                float dist[3];
                for (int i = 0; i < 3; ++i)
                {
                    dist[i] = (inVerts[i] - camPos).dot(camFwd);
                }

                auto isInside = [&](int i) -> bool
                {
                    return dist[i] >= nearD;
                };

                auto intersect = [&](const Vector3 &a, const Vector3 &b, float da, float db) -> Vector3
                {
                    float denom = (db - da);
                    if (fabsf(denom) < 1e-6f)
                        return a;
                    float t = (nearD - da) / denom;
                    if (t < 0.0f)
                        t = 0.0f;
                    if (t > 1.0f)
                        t = 1.0f;
                    return a + (b - a) * t;
                };

                Vector3 clipped[4];
                int outCount = 0;

                for (int i = 0; i < 3; ++i)
                {
                    int j = (i + 1) % 3;
                    bool in0 = isInside(i);
                    bool in1 = isInside(j);
                    const Vector3 &P0 = inVerts[i];
                    const Vector3 &P1 = inVerts[j];
                    float d0 = dist[i];
                    float d1 = dist[j];

                    if (in0 && in1)
                    {
                        clipped[outCount++] = P1;
                    }
                    else if (in0 && !in1)
                    {
                        clipped[outCount++] = intersect(P0, P1, d0, d1);
                    }
                    else if (!in0 && in1)
                    {
                        clipped[outCount++] = intersect(P0, P1, d0, d1);
                        clipped[outCount++] = P1;
                    }
                }

                if (outCount < 3)
                {
                    return;
                }

                if (outCount == 3)
                {
                    drawTriangle3D_Color(clipped[0], clipped[1], clipped[2],
                                         baseR, baseG, baseB,
                                         camera, viewport, viewProjMatrix,
                                         framebuffer, zBuffer,
                                         lights, activeLightCount,
                                         backfaceCullingEnabled,
                                         statsTrianglesTotal,
                                         statsTrianglesBackfaceCulled);
                    return;
                }

                if (outCount == 4)
                {
                    drawTriangle3D_Color(clipped[0], clipped[1], clipped[2],
                                         baseR, baseG, baseB,
                                         camera, viewport, viewProjMatrix,
                                         framebuffer, zBuffer,
                                         lights, activeLightCount,
                                         backfaceCullingEnabled,
                                         statsTrianglesTotal,
                                         statsTrianglesBackfaceCulled);

                    drawTriangle3D_Color(clipped[0], clipped[2], clipped[3],
                                         baseR, baseG, baseB,
                                         camera, viewport, viewProjMatrix,
                                         framebuffer, zBuffer,
                                         lights, activeLightCount,
                                         backfaceCullingEnabled,
                                         statsTrianglesTotal,
                                         statsTrianglesBackfaceCulled);
                    return;
                }
            }

            drawTriangle3D_Color(v0, v1, v2,
                                 baseR, baseG, baseB,
                                 camera, viewport, viewProjMatrix,
                                 framebuffer, zBuffer,
                                 lights, activeLightCount,
                                 backfaceCullingEnabled,
                                 statsTrianglesTotal,
                                 statsTrianglesBackfaceCulled);
        }

    public:
        static void drawTriangle3D(const Vector3 &v0, const Vector3 &v1, const Vector3 &v2,
                                   uint16_t color,
                                   const Camera &camera,
                                   const Viewport &viewport,
                                   const Matrix4x4 &viewProjMatrix,
                                   FrameBuffer &framebuffer,
                                   ZBuffer<320, 240> *zBuffer,
                                   const Light *lights,
                                   int activeLightCount,
                                   bool backfaceCullingEnabled,
                                   uint32_t &statsTrianglesTotal,
                                   uint32_t &statsTrianglesBackfaceCulled)
        {
            float baseR;
            float baseG;
            float baseB;
            decodeColorToFloat(color, baseR, baseG, baseB);

            drawTriangle3D_Clipped(v0, v1, v2,
                                   baseR, baseG, baseB,
                                   camera, viewport, viewProjMatrix,
                                   framebuffer, zBuffer,
                                   lights, activeLightCount,
                                   backfaceCullingEnabled,
                                   statsTrianglesTotal,
                                   statsTrianglesBackfaceCulled);
        }

        static void drawMesh(Mesh *mesh,
                             const Camera &camera,
                             const Viewport &viewport,
                             const Frustum &frustum,
                             const Matrix4x4 &viewProjMatrix,
                             FrameBuffer &framebuffer,
                             ZBuffer<320, 240> *zBuffer,
                             const Light *lights,
                             int activeLightCount,
                             bool backfaceCullingEnabled,
                             uint32_t &statsTrianglesTotal,
                             uint32_t &statsTrianglesBackfaceCulled)
        {
            if (!mesh || !mesh->isVisible())
                return;

            mesh->updateTransform();

            Vector3 center = mesh->center();
            float radius = mesh->radius();
            if (!frustum.sphere(center, radius))
                return;

            const uint16_t meshColor565 = mesh->color().rgb565;
            float baseR;
            float baseG;
            float baseB;
            decodeColorToFloat(meshColor565, baseR, baseG, baseB);

            const uint16_t faceCount = mesh->numFaces();
            if (faceCount == 0)
            {
                return;
            }

            uint16_t maxIndex = 0;
            for (uint16_t i = 0; i < faceCount; ++i)
            {
                const Face &face = mesh->face(i);
                if (face.v0 > maxIndex)
                    maxIndex = face.v0;
                if (face.v1 > maxIndex)
                    maxIndex = face.v1;
                if (face.v2 > maxIndex)
                    maxIndex = face.v2;
            }

            const uint16_t vertexCountUsed = static_cast<uint16_t>(maxIndex + 1);
            const size_t vertexBufferSize = static_cast<size_t>(vertexCountUsed) * sizeof(Vector3);

            Vector3 *worldVerts = (Vector3 *)MemUtils::allocAligned(vertexBufferSize, 16);
            if (!worldVerts)
            {
                return;
            }

            Vector3 *screenVerts = (Vector3 *)MemUtils::allocAligned(vertexBufferSize, 16);
            if (!screenVerts)
            {
                MemUtils::freeAligned(worldVerts);
                return;
            }

            const Matrix4x4 &meshTransform = mesh->getTransform();

            for (uint16_t i = 0; i < vertexCountUsed; ++i)
            {
                const Vertex &v = mesh->vert(i);
                Vector3 localPos = mesh->decodePosition(v);
                Vector3 worldPos = meshTransform.transformNoDiv(localPos);
                worldVerts[i] = worldPos;
                screenVerts[i] = CameraController::project(worldPos, viewProjMatrix, viewport);
            }

            for (uint16_t i = 0; i < faceCount; ++i)
            {
                const Face &face = mesh->face(i);
                uint16_t i0 = face.v0;
                uint16_t i1 = face.v1;
                uint16_t i2 = face.v2;

                const Vector3 &v0 = worldVerts[i0];
                const Vector3 &v1 = worldVerts[i1];
                const Vector3 &v2 = worldVerts[i2];

                statsTrianglesTotal++;
                if (backfaceCullingEnabled)
                {
                    Vector3 edge1 = v1 - v0;
                    Vector3 edge2 = v2 - v0;
                    Vector3 normal = edge1.cross(edge2);
                    Vector3 viewDir = camera.position - v0;
                    if (normal.dot(viewDir) <= 0.0f)
                    {
                        statsTrianglesBackfaceCulled++;
                        continue;
                    }
                }

                const Vector3 &p0 = screenVerts[i0];
                const Vector3 &p1 = screenVerts[i1];
                const Vector3 &p2 = screenVerts[i2];

                drawTriangle3D_Clipped_Preprojected(v0, v1, v2,
                                                    p0, p1, p2,
                                                    baseR, baseG, baseB,
                                                    camera, viewport, viewProjMatrix,
                                                    framebuffer, zBuffer,
                                                    lights, activeLightCount,
                                                    backfaceCullingEnabled,
                                                    statsTrianglesTotal,
                                                    statsTrianglesBackfaceCulled);
            }

            MemUtils::freeAligned(screenVerts);
            MemUtils::freeAligned(worldVerts);
        }
    };
}

#endif
