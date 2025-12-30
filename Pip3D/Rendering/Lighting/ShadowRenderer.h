#ifndef SHADOWRENDERER_H
#define SHADOWRENDERER_H

#include <stdint.h>

#include "Shadow.h"
#include "Lighting.h"
#include "../Display/FrameBuffer.h"
#include "../Display/ZBuffer.h"
#include "../Rasterizer/Rasterizer.h"
#include "../SceneRendering/CameraController.h"
#include "../../Math/Math.h"
#include "../../Geometry/Mesh.h"
#include "../../Core/Instance.h"

namespace pip3D
{
    class ShadowRenderer
    {
    private:
        __attribute__((always_inline)) static inline void computeShadowColorAndAlpha(
            const ShadowSettings &shadowSettings,
            uint16_t &shadowColorOut,
            uint8_t &baseAlphaOut)
        {
            float opacity = clamp(shadowSettings.shadowOpacity, 0.0f, 1.0f);
            uint16_t srcColor = shadowSettings.shadowColor.rgb565;

            uint16_t r = (uint16_t)(((srcColor >> 11) & 0x1F) * opacity);
            uint16_t g = (uint16_t)(((srcColor >> 5) & 0x3F) * opacity);
            uint16_t b = (uint16_t)((srcColor & 0x1F) * opacity);
            shadowColorOut = (uint16_t)((r << 11) | (g << 5) | b);
            baseAlphaOut = (uint8_t)(opacity * 255.0f);
        }

    public:
        static void drawMeshShadow(Mesh *mesh,
                                   bool shadowsEnabled,
                                   const ShadowSettings &shadowSettings,
                                   const Camera &camera,
                                   const Light *lights,
                                   int activeLightCount,
                                   const Matrix4x4 &viewProjMatrix,
                                   const Viewport &viewport,
                                   FrameBuffer &framebuffer,
                                   ZBuffer<SCREEN_WIDTH, SCREEN_BAND_HEIGHT> *zBuffer,
                                   bool &backfaceCullingEnabled)
        {
            if (!mesh || !mesh->isVisible() || !shadowsEnabled || !shadowSettings.enabled)
                return;

            mesh->updateTransform();

            if (activeLightCount == 0)
                return;
            const Light &light = lights[0];
            if (light.type != LIGHT_DIRECTIONAL && light.type != LIGHT_POINT)
                return;

            uint16_t shadowColor;
            uint8_t baseAlpha;
            computeShadowColorAndAlpha(shadowSettings, shadowColor, baseAlpha);

            bool oldCulling = backfaceCullingEnabled;
            backfaceCullingEnabled = false;

            const ShadowProjector::ShadowPlane &plane = shadowSettings.plane;

            Vector3 meshCenter = mesh->center();
            float meshRadius = mesh->radius();
            float centerDist = plane.normal.x * meshCenter.x + plane.normal.y * meshCenter.y + plane.normal.z * meshCenter.z + plane.d;
            if (centerDist + meshRadius <= 0.0f)
            {
                backfaceCullingEnabled = oldCulling;
                return;
            }

            const float planeY = -plane.d / plane.normal.y;
            const float offsetY = shadowSettings.shadowOffset;
            const float depthBias = 0.005f;
            Vector3 dirNorm;
            if (light.type == LIGHT_DIRECTIONAL)
            {
                dirNorm = light.direction;
                dirNorm.normalize();
            }
            const Vector3 lightPos = light.position;

            for (uint16_t i = 0; i < mesh->numFaces(); i++)
            {
                const Face &face = mesh->face(i);

                Vector3 v0 = mesh->vertex(face.v0);
                Vector3 v1 = mesh->vertex(face.v1);
                Vector3 v2 = mesh->vertex(face.v2);

                float d0 = plane.normal.x * v0.x + plane.normal.y * v0.y + plane.normal.z * v0.z + plane.d;
                float d1 = plane.normal.x * v1.x + plane.normal.y * v1.y + plane.normal.z * v1.z + plane.d;
                float d2 = plane.normal.x * v2.x + plane.normal.y * v2.y + plane.normal.z * v2.z + plane.d;
                if (d0 <= 0.0f && d1 <= 0.0f && d2 <= 0.0f)
                    continue;

                Vector3 n = (v1 - v0).cross(v2 - v0);
                Vector3 L;
                if (light.type == LIGHT_DIRECTIONAL)
                {
                    L = Vector3(-dirNorm.x, -dirNorm.y, -dirNorm.z);
                }
                else
                {
                    L = light.position - v0;
                }
                float nl = n.dot(L);
                if (nl <= 0.0f)
                    continue;

                Vector3 sv0, sv1, sv2;

                if (light.type == LIGHT_DIRECTIONAL)
                {
                    const Vector3 &L = dirNorm;
                    float ly = L.y;
                    float signLy = (ly >= 0.0f) ? 1.0f : -1.0f;
                    float absLy = fabsf(ly);
                    float safeLy = (absLy < 0.15f) ? signLy * 0.15f : ly;

                    float t0 = (planeY - v0.y) / safeLy;
                    float t1 = (planeY - v1.y) / safeLy;
                    float t2 = (planeY - v2.y) / safeLy;

                    sv0 = Vector3(v0.x + t0 * L.x, planeY, v0.z + t0 * L.z);
                    sv1 = Vector3(v1.x + t1 * L.x, planeY, v1.z + t1 * L.z);
                    sv2 = Vector3(v2.x + t2 * L.x, planeY, v2.z + t2 * L.z);
                }
                else
                {
                    Vector3 L0 = v0 - lightPos;
                    Vector3 L1 = v1 - lightPos;
                    Vector3 L2 = v2 - lightPos;

                    if (fabsf(L0.y) > 0.001f)
                    {
                        float t0 = (planeY - lightPos.y) / L0.y;
                        sv0 = lightPos + L0 * t0;
                    }
                    else
                    {
                        sv0 = Vector3(v0.x, planeY, v0.z);
                    }

                    if (fabsf(L1.y) > 0.001f)
                    {
                        float t1 = (planeY - lightPos.y) / L1.y;
                        sv1 = lightPos + L1 * t1;
                    }
                    else
                    {
                        sv1 = Vector3(v1.x, planeY, v1.z);
                    }

                    if (fabsf(L2.y) > 0.001f)
                    {
                        float t2 = (planeY - lightPos.y) / L2.y;
                        sv2 = lightPos + L2 * t2;
                    }
                    else
                    {
                        sv2 = Vector3(v2.x, planeY, v2.z);
                    }
                }

                sv0.y += offsetY;
                sv1.y += offsetY;
                sv2.y += offsetY;

                Vector3 p0 = CameraController::project(sv0, viewProjMatrix, viewport);
                Vector3 p1 = CameraController::project(sv1, viewProjMatrix, viewport);
                Vector3 p2 = CameraController::project(sv2, viewProjMatrix, viewport);

                if (camera.projectionType == PERSPECTIVE)
                {
                    if (p0.z <= 0.0f && p1.z <= 0.0f && p2.z <= 0.0f)
                        continue;
                    p0.z -= depthBias;
                    p1.z -= depthBias;
                    p2.z -= depthBias;
                }

                int16_t bandTop = currentBandOffsetY();
                int16_t bandH = currentBandHeight();
                int16_t bandBottom = static_cast<int16_t>(bandTop + bandH);

                float minY = fminf(p0.y, fminf(p1.y, p2.y));
                float maxY = fmaxf(p0.y, fmaxf(p1.y, p2.y));
                if (maxY < bandTop || minY >= bandBottom)
                    continue;

                Vector3 lp0 = p0;
                Vector3 lp1 = p1;
                Vector3 lp2 = p2;
                lp0.y -= (float)bandTop;
                lp1.y -= (float)bandTop;
                lp2.y -= (float)bandTop;

                Rasterizer::fillShadowTriangle((int16_t)lp0.x, (int16_t)lp0.y, lp0.z,
                                               (int16_t)lp1.x, (int16_t)lp1.y, lp1.z,
                                               (int16_t)lp2.x, (int16_t)lp2.y, lp2.z,
                                               shadowColor,
                                               baseAlpha,
                                               framebuffer.getBuffer(),
                                               zBuffer,
                                               framebuffer.getConfig(),
                                               shadowSettings.softEdges);
            }

            backfaceCullingEnabled = oldCulling;
        }

        static void drawMeshInstanceShadow(MeshInstance *instance,
                                           bool shadowsEnabled,
                                           const ShadowSettings &shadowSettings,
                                           const Camera &camera,
                                           const Light *lights,
                                           int activeLightCount,
                                           const Matrix4x4 &viewProjMatrix,
                                           const Viewport &viewport,
                                           FrameBuffer &framebuffer,
                                           ZBuffer<SCREEN_WIDTH, SCREEN_BAND_HEIGHT> *zBuffer,
                                           bool &backfaceCullingEnabled)
        {
            if (!instance || !instance->isVisible() || !shadowsEnabled || !shadowSettings.enabled)
                return;

            Mesh *mesh = instance->getMesh();
            if (!mesh)
                return;

            if (activeLightCount == 0)
                return;
            const Light &light = lights[0];

            const Matrix4x4 &worldTransform = instance->transform();

            uint16_t shadowColor;
            uint8_t baseAlpha;
            computeShadowColorAndAlpha(shadowSettings, shadowColor, baseAlpha);

            bool oldCulling = backfaceCullingEnabled;
            backfaceCullingEnabled = false;

            const ShadowProjector::ShadowPlane &plane = shadowSettings.plane;

            Vector3 instCenter = instance->center();
            float instRadius = instance->radius();
            float centerDist = plane.normal.x * instCenter.x + plane.normal.y * instCenter.y + plane.normal.z * instCenter.z + plane.d;
            if (centerDist + instRadius <= 0.0f)
            {
                backfaceCullingEnabled = oldCulling;
                return;
            }

            const float planeY = -plane.d / plane.normal.y;
            const float offsetY = shadowSettings.shadowOffset;
            const float depthBias = 0.005f;
            Vector3 dirNorm;
            if (light.type == LIGHT_DIRECTIONAL)
            {
                dirNorm = light.direction;
                dirNorm.normalize();
            }

            const Vector3 lightPos = light.position;

            for (uint16_t i = 0; i < mesh->numFaces(); i++)
            {
                const Face &face = mesh->face(i);

                const Vertex &vert0 = mesh->vert(face.v0);
                const Vertex &vert1 = mesh->vert(face.v1);
                const Vertex &vert2 = mesh->vert(face.v2);

                Vector3 local0 = mesh->decodePosition(vert0);
                Vector3 local1 = mesh->decodePosition(vert1);
                Vector3 local2 = mesh->decodePosition(vert2);

                Vector3 v0 = worldTransform.transformNoDiv(local0);
                Vector3 v1 = worldTransform.transformNoDiv(local1);
                Vector3 v2 = worldTransform.transformNoDiv(local2);

                float d0 = plane.normal.x * v0.x + plane.normal.y * v0.y + plane.normal.z * v0.z + plane.d;
                float d1 = plane.normal.x * v1.x + plane.normal.y * v1.y + plane.normal.z * v1.z + plane.d;
                float d2 = plane.normal.x * v2.x + plane.normal.y * v2.y + plane.normal.z * v2.z + plane.d;
                if (d0 <= 0.0f && d1 <= 0.0f && d2 <= 0.0f)
                    continue;

                Vector3 n = (v1 - v0).cross(v2 - v0);
                Vector3 L;
                if (light.type == LIGHT_DIRECTIONAL)
                {
                    L = Vector3(-dirNorm.x, -dirNorm.y, -dirNorm.z);
                }
                else
                {
                    L = lightPos - v0;
                }
                float nl = n.dot(L);
                if (nl <= 0.0f)
                    continue;

                Vector3 sv0, sv1, sv2;

                if (light.type == LIGHT_DIRECTIONAL)
                {
                    const Vector3 &L = dirNorm;

                    float ly = L.y;
                    float signLy = (ly >= 0.0f) ? 1.0f : -1.0f;
                    float absLy = fabsf(ly);
                    float safeLy = (absLy < 0.15f) ? signLy * 0.15f : ly;

                    float t0 = (planeY - v0.y) / safeLy;
                    float t1 = (planeY - v1.y) / safeLy;
                    float t2 = (planeY - v2.y) / safeLy;

                    sv0 = Vector3(v0.x + t0 * L.x, planeY, v0.z + t0 * L.z);
                    sv1 = Vector3(v1.x + t1 * L.x, planeY, v1.z + t1 * L.z);
                    sv2 = Vector3(v2.x + t2 * L.x, planeY, v2.z + t2 * L.z);
                }
                else
                {
                    Vector3 L0 = v0 - lightPos;
                    Vector3 L1 = v1 - lightPos;
                    Vector3 L2 = v2 - lightPos;

                    if (fabsf(L0.y) > 0.001f)
                    {
                        float t0 = (planeY - lightPos.y) / L0.y;
                        sv0 = lightPos + L0 * t0;
                    }
                    else
                    {
                        sv0 = Vector3(v0.x, planeY, v0.z);
                    }

                    if (fabsf(L1.y) > 0.001f)
                    {
                        float t1 = (planeY - lightPos.y) / L1.y;
                        sv1 = lightPos + L1 * t1;
                    }
                    else
                    {
                        sv1 = Vector3(v1.x, planeY, v1.z);
                    }

                    if (fabsf(L2.y) > 0.001f)
                    {
                        float t2 = (planeY - lightPos.y) / L2.y;
                        sv2 = lightPos + L2 * t2;
                    }
                    else
                    {
                        sv2 = Vector3(v2.x, planeY, v2.z);
                    }
                }

                sv0.y += offsetY;
                sv1.y += offsetY;
                sv2.y += offsetY;

                Vector3 p0 = CameraController::project(sv0, viewProjMatrix, viewport);
                Vector3 p1 = CameraController::project(sv1, viewProjMatrix, viewport);
                Vector3 p2 = CameraController::project(sv2, viewProjMatrix, viewport);

                if (camera.projectionType == PERSPECTIVE)
                {
                    if (p0.z <= 0.0f && p1.z <= 0.0f && p2.z <= 0.0f)
                        continue;
                    p0.z -= depthBias;
                    p1.z -= depthBias;
                    p2.z -= depthBias;
                }

                Rasterizer::fillShadowTriangle(p0.x, p0.y, p0.z,
                                               p1.x, p1.y, p1.z,
                                               p2.x, p2.y, p2.z,
                                               shadowColor,
                                               baseAlpha,
                                               framebuffer.getBuffer(),
                                               zBuffer,
                                               framebuffer.getConfig(),
                                               shadowSettings.softEdges);
            }

            backfaceCullingEnabled = oldCulling;
        }
    };
}

#endif
