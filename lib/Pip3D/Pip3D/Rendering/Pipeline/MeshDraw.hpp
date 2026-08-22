#pragma once

#include "Core/Platform.hpp"
#include "Camera/Camera.hpp"
#include "Math/Algebra.hpp"

#include "Rendering/Buffers/ZBuffer.hpp"
#include "Rendering/Buffers/FrameBuffer.hpp"
#include "Rendering/Lighting/Lighting.hpp"
#include "Rendering/Lighting/Fog.hpp"
#include "Rendering/Pipeline/Rasterizer/Solid.hpp"
#include "Rendering/Pipeline/Rasterizer/Smooth.hpp"
#include "Rendering/Pipeline/Rasterizer/Textured.hpp"

#include "Shading.hpp"

namespace pip3D
{
    namespace MeshRenderer
    {

        struct ClipVert
        {
            Vector3 pos;
            float u, v;
            float d;
            float lr, lg, lb;
        };

        PIP3D_FORCE_INLINE static ClipVert lerpClipVert(const ClipVert &a,
                                                        const ClipVert &b,
                                                        float t) noexcept
        {
            const float it = 1.0f - t;
            ClipVert r;
            r.pos.x = a.pos.x * it + b.pos.x * t;
            r.pos.y = a.pos.y * it + b.pos.y * t;
            r.pos.z = a.pos.z * it + b.pos.z * t;
            r.u = a.u * it + b.u * t;
            r.v = a.v * it + b.v * t;
            r.d = a.d * it + b.d * t;
            r.lr = a.lr * it + b.lr * t;
            r.lg = a.lg * it + b.lg * t;
            r.lb = a.lb * it + b.lb * t;
            return r;
        }

        struct ClipVertSmooth
        {
            Vector3 pos;
            float d;
            float lr, lg, lb;
        };

        PIP3D_FORCE_INLINE static ClipVertSmooth lerpClipVertSmooth(const ClipVertSmooth &a,
                                                                    const ClipVertSmooth &b,
                                                                    float t) noexcept
        {
            const float it = 1.0f - t;
            ClipVertSmooth r;
            r.pos = a.pos * it + b.pos * t;
            r.d = a.d * it + b.d * t;
            r.lr = a.lr * it + b.lr * t;
            r.lg = a.lg * it + b.lg * t;
            r.lb = a.lb * it + b.lb * t;
            return r;
        }

        struct ClipVertPhong
        {
            Vector3 pos;
            Vector3 normal;
            float d;
        };

        PIP3D_FORCE_INLINE static ClipVertPhong lerpClipVertPhong(const ClipVertPhong &a,
                                                                  const ClipVertPhong &b,
                                                                  float t) noexcept
        {
            const float it = 1.0f - t;
            ClipVertPhong r;
            r.pos = a.pos * it + b.pos * t;
            r.normal = a.normal * it + b.normal * t;
            r.d = a.d * it + b.d * t;
            return r;
        }

        PIP3D_FORCE_INLINE static void decodeColorToFloat(uint16_t color,
                                                          float &baseR,
                                                          float &baseG,
                                                          float &baseB) noexcept
        {
            Color(color).toFloat(baseR, baseG, baseB);
        }

        template <typename Pos, typename Lerp>
        PIP3D_HOT static int clipTriangleNear(
            const Pos &v0, const Pos &v1, const Pos &v2,
            float d0, float d1, float d2,
            float nearD,
            Pos *PIP3D_RESTRICT out,
            Lerp lerpFn) noexcept
        {
            int outCount = 0;

            const float edges[3][2] = {{d0, d1}, {d1, d2}, {d2, d0}};
            const Pos *verts[3] = {&v0, &v1, &v2};

            for (int e = 0; e < 3; ++e)
            {
                const Pos &A = *verts[e];
                const Pos &B = *verts[(e + 1) % 3];
                const float da = edges[e][0];
                const float db = edges[e][1];
                const bool inA = da >= nearD;
                const bool inB = db >= nearD;

                if (inA && inB)
                {
                    if (outCount < 4)
                        out[outCount++] = B;
                }
                else if (inA != inB)
                {
                    const float denom = db - da;
                    float t = (fabsf(denom) < 1e-6f) ? 0.0f
                                                     : (nearD - da) / denom;
                    t = clamp(t, 0.0f, 1.0f);
                    const Pos ip = lerpFn(A, B, t);
                    if (outCount < 4)
                        out[outCount++] = ip;
                    if (!inA && outCount < 4)
                        out[outCount++] = B;
                }
            }

            return outCount;
        }

        PIP3D_FORCE_INLINE static bool bboxCull(
            const Vector3 &p0, const Vector3 &p1, const Vector3 &p2,
            int16_t bandTop, int16_t bandBottom,
            float viewportWidth) noexcept
        {
            const float minY = (p0.y < p1.y) ? ((p0.y < p2.y) ? p0.y : p2.y)
                                             : ((p1.y < p2.y) ? p1.y : p2.y);
            const float maxY = (p0.y > p1.y) ? ((p0.y > p2.y) ? p0.y : p2.y)
                                             : ((p1.y > p2.y) ? p1.y : p2.y);
            if (maxY < bandTop || minY >= bandBottom)
                return true;

            const float minX = (p0.x < p1.x) ? ((p0.x < p2.x) ? p0.x : p2.x)
                                             : ((p1.x < p2.x) ? p1.x : p2.x);
            const float maxX = (p0.x > p1.x) ? ((p0.x > p2.x) ? p0.x : p2.x)
                                             : ((p1.x > p2.x) ? p1.x : p2.x);
            if (maxX < 0.0f || minX >= viewportWidth)
                return true;

            return false;
        }

        PIP3D_HOT static void drawTriangle3D_Color_Preprojected(
            const Vector3 &v0, const Vector3 &v1, const Vector3 &v2,
            const Vector3 &p0, const Vector3 &p1, const Vector3 &p2,
            float baseR, float baseG, float baseB,
            const Vector3 &camPos,
            float viewportWidth,
            int16_t bandTop, int16_t bandBottom, float bandTopF,
            FrameBuffer &framebuffer,
            ZBuffer *zBuffer,
            const Light *lights,
            int activeLightCount,
            bool useUniformColor,
            uint16_t uniformColor)
        {

            if (bboxCull(p0, p1, p2, bandTop, bandBottom, viewportWidth))
                return;

            uint16_t shadedColor = uniformColor;

            if (!useUniformColor)
            {

                float finalR, finalG, finalB;
                Shading::calculateFaceLighting(
                    v0, v1, v2, camPos,
                    lights, activeLightCount,
                    baseR, baseG, baseB,
                    finalR, finalG, finalB);
                shadedColor = Color::fromFloat(finalR, finalG, finalB).rgb565;
            }

            Rasterizer::fillTriangle(p0.x, p0.y - bandTopF, p0.z,
                                     p1.x, p1.y - bandTopF, p1.z,
                                     p2.x, p2.y - bandTopF, p2.z,
                                     shadedColor,
                                     framebuffer.getBuffer(),
                                     zBuffer,
                                     framebuffer.getConfig());
        }

        PIP3D_HOT inline void clipAndDrawNearTextured(const ClipVert inVerts[3],
                                                      float nearD,
                                                      const Viewport &viewport,
                                                      const Matrix4x4 &viewProjMatrix,
                                                      FrameBuffer &framebuffer,
                                                      ZBuffer *zBuffer,
                                                      const Texture &tex)
        {
            ClipVert clipped[4];
            const int outCount = clipTriangleNear(
                inVerts[0], inVerts[1], inVerts[2],
                inVerts[0].d, inVerts[1].d, inVerts[2].d,
                nearD, clipped,
                [](const ClipVert &a, const ClipVert &b, float t) noexcept
                {
                    return lerpClipVert(a, b, t);
                });

            if (outCount < 3)
                return;

            const float viewportHalfWidth = static_cast<float>(viewport.width) * 0.5f;
            const float viewportHalfHeight = static_cast<float>(viewport.height) * 0.5f;
            const int16_t bandTop = g_bandOffsetY;
            const int16_t bandBottom = static_cast<int16_t>(bandTop + g_bandHeight);
            const float viewportWidth = static_cast<float>(viewport.width);
            const float bandTopF = static_cast<float>(bandTop);
            const DisplayConfig &framebufferConfig = framebuffer.getConfig();

            Vector3 proj[4];
            for (int i = 0; i < outCount; ++i)
                proj[i] = CameraController::project(clipped[i].pos, viewProjMatrix,
                                                    viewportHalfWidth, viewportHalfHeight,
                                                    viewport.x, viewport.y);

            auto drawTri = [&](int a, int b, int c)
            {
                const Vector3 &p0 = proj[a];
                const Vector3 &p1 = proj[b];
                const Vector3 &p2 = proj[c];

                if (bboxCull(p0, p1, p2, bandTop, bandBottom, viewportWidth))
                    return;

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
                    tex,
                    framebuffer.getBuffer(),
                    zBuffer,
                    framebufferConfig);
            };

            drawTri(0, 1, 2);
            if (outCount == 4)
                drawTri(0, 2, 3);
        }

        PIP3D_HOT static void clipAndDrawNear(
            const Vector3 &v0, const Vector3 &v1, const Vector3 &v2,
            float d0, float d1, float d2,
            float nearD,
            float baseR, float baseG, float baseB,
            const Vector3 &camPos,
            float viewportHalfWidth, float viewportHalfHeight,
            float viewportWidth,
            int16_t bandTop, int16_t bandBottom, float bandTopF,
            const Matrix4x4 &viewProjMatrix,
            const Viewport &viewport,
            FrameBuffer &framebuffer,
            ZBuffer *zBuffer,
            const Light *lights,
            int activeLightCount,
            bool useUniformColor,
            uint16_t uniformColor)
        {
            Vector3 clipped[4];
            const int outCount = clipTriangleNear(
                v0, v1, v2, d0, d1, d2, nearD, clipped,
                [](const Vector3 &a, const Vector3 &b, float t) noexcept
                {
                    return a + (b - a) * t;
                });

            if (outCount < 3)
                return;

            Vector3 proj[4];
            for (int i = 0; i < outCount; ++i)
                proj[i] = CameraController::project(clipped[i], viewProjMatrix,
                                                    viewportHalfWidth, viewportHalfHeight,
                                                    viewport.x, viewport.y);

            drawTriangle3D_Color_Preprojected(clipped[0], clipped[1], clipped[2],
                                              proj[0], proj[1], proj[2],
                                              baseR, baseG, baseB,
                                              camPos,
                                              viewportWidth,
                                              bandTop, bandBottom, bandTopF,
                                              framebuffer, zBuffer,
                                              lights, activeLightCount,
                                              useUniformColor,
                                              uniformColor);

            if (outCount == 4)
            {
                drawTriangle3D_Color_Preprojected(clipped[0], clipped[2], clipped[3],
                                                  proj[0], proj[2], proj[3],
                                                  baseR, baseG, baseB,
                                                  camPos,
                                                  viewportWidth,
                                                  bandTop, bandBottom, bandTopF,
                                                  framebuffer, zBuffer,
                                                  lights, activeLightCount,
                                                  useUniformColor,
                                                  uniformColor);
            }
        }

        PIP3D_HOT inline void drawTriangle3D_Preprojected(
            const Vector3 &v0, const Vector3 &v1, const Vector3 &v2,
            const Vector3 &p0, const Vector3 &p1, const Vector3 &p2,
            float d0, float d1, float d2,
            bool partiallyClipped,
            float nearD,
            const Vector3 &camPos,
            uint16_t color,
            const Matrix4x4 &viewProjMatrix,
            const Viewport &viewport,
            float viewportHalfWidth, float viewportHalfHeight, float viewportWidth,
            int16_t bandTop, int16_t bandBottom, float bandTopF,
            FrameBuffer &framebuffer,
            ZBuffer *zBuffer,
            const Light *lights,
            int activeLightCount,
            bool useUniformColor = false,
            uint16_t uniformColor = 0)
        {
            float baseR = 0.0f, baseG = 0.0f, baseB = 0.0f;
            if (!useUniformColor)
                Color(color).toFloat(baseR, baseG, baseB);

            if (likely(!partiallyClipped))
            {
                drawTriangle3D_Color_Preprojected(v0, v1, v2,
                                                  p0, p1, p2,
                                                  baseR, baseG, baseB,
                                                  camPos,
                                                  viewportWidth,
                                                  bandTop, bandBottom, bandTopF,
                                                  framebuffer, zBuffer,
                                                  lights, activeLightCount,
                                                  useUniformColor,
                                                  uniformColor);
                return;
            }

            clipAndDrawNear(v0, v1, v2, d0, d1, d2, nearD,
                            baseR, baseG, baseB,
                            camPos,
                            viewportHalfWidth, viewportHalfHeight,
                            viewportWidth,
                            bandTop, bandBottom, bandTopF,
                            viewProjMatrix,
                            viewport,
                            framebuffer, zBuffer,
                            lights, activeLightCount,
                            useUniformColor,
                            uniformColor);
        }

        PIP3D_HOT inline void drawTriangle3D(
            const Vector3 &v0, const Vector3 &v1, const Vector3 &v2,
            uint16_t color,
            const Camera &camera,
            const Viewport &viewport,
            const Matrix4x4 &viewProjMatrix,
            FrameBuffer &framebuffer,
            ZBuffer *zBuffer,
            const Light *lights,
            int activeLightCount,
            bool useUniformColor = false,
            uint16_t uniformColor = 0)
        {
            float baseR = 0.0f, baseG = 0.0f, baseB = 0.0f;
            if (!useUniformColor)
                Color(color).toFloat(baseR, baseG, baseB);

            const Vector3 &camPos = camera.position;
            const Vector3 &camFwd = camera.forward();
            const float nearD = camera.nearPlane;

            const float d0 = Culling::computeEyeZ(v0, camPos, camFwd);
            const float d1 = Culling::computeEyeZ(v1, camPos, camFwd);
            const float d2 = Culling::computeEyeZ(v2, camPos, camFwd);

            if (d0 < nearD && d1 < nearD && d2 < nearD)
                return;

            const float viewportHalfWidth = static_cast<float>(viewport.width) * 0.5f;
            const float viewportHalfHeight = static_cast<float>(viewport.height) * 0.5f;
            const float viewportWidth = static_cast<float>(viewport.width);
            const int16_t bandTop = g_bandOffsetY;
            const int16_t bandBottom = static_cast<int16_t>(bandTop + g_bandHeight);
            const float bandTopF = static_cast<float>(bandTop);

            const bool partiallyClipped = (d0 < nearD || d1 < nearD || d2 < nearD);

            if (likely(!partiallyClipped))
            {
                const Vector3 p0 = CameraController::project(v0, viewProjMatrix,
                                                             viewportHalfWidth, viewportHalfHeight,
                                                             viewport.x, viewport.y);
                const Vector3 p1 = CameraController::project(v1, viewProjMatrix,
                                                             viewportHalfWidth, viewportHalfHeight,
                                                             viewport.x, viewport.y);
                const Vector3 p2 = CameraController::project(v2, viewProjMatrix,
                                                             viewportHalfWidth, viewportHalfHeight,
                                                             viewport.x, viewport.y);
                drawTriangle3D_Color_Preprojected(v0, v1, v2,
                                                  p0, p1, p2,
                                                  baseR, baseG, baseB,
                                                  camPos,
                                                  viewportWidth,
                                                  bandTop, bandBottom, bandTopF,
                                                  framebuffer, zBuffer,
                                                  lights, activeLightCount,
                                                  useUniformColor,
                                                  uniformColor);
                return;
            }

            clipAndDrawNear(v0, v1, v2, d0, d1, d2, nearD,
                            baseR, baseG, baseB,
                            camPos,
                            viewportHalfWidth, viewportHalfHeight,
                            viewportWidth,
                            bandTop, bandBottom, bandTopF,
                            viewProjMatrix,
                            viewport,
                            framebuffer, zBuffer,
                            lights, activeLightCount,
                            useUniformColor,
                            uniformColor);
        }

        PIP3D_HOT inline void drawTriangle3D_Smooth_Preprojected(
            const Vector3 &p0, const Vector3 &p1, const Vector3 &p2,
            float lr0, float lg0, float lb0,
            float lr1, float lg1, float lb1,
            float lr2, float lg2, float lb2,
            float viewportWidth,
            int16_t bandTop, int16_t bandBottom, float bandTopF,
            FrameBuffer &framebuffer,
            ZBuffer *zBuffer)
        {
            if (bboxCull(p0, p1, p2, bandTop, bandBottom, viewportWidth))
                return;

            Rasterizer::fillTriangleSmooth(
                static_cast<int16_t>(p0.x), static_cast<int16_t>(p0.y - bandTopF), p0.z,
                static_cast<int16_t>(p1.x), static_cast<int16_t>(p1.y - bandTopF), p1.z,
                static_cast<int16_t>(p2.x), static_cast<int16_t>(p2.y - bandTopF), p2.z,
                lr0, lg0, lb0,
                lr1, lg1, lb1,
                lr2, lg2, lb2,
                framebuffer.getBuffer(), zBuffer, framebuffer.getConfig());
        }

        PIP3D_HOT inline void clipAndDrawNearSmooth(
            const ClipVertSmooth inVerts[3],
            float nearD,
            const Viewport &viewport,
            const Matrix4x4 &viewProjMatrix,
            FrameBuffer &framebuffer,
            ZBuffer *zBuffer)
        {
            ClipVertSmooth clipped[4];
            const int outCount = clipTriangleNear(
                inVerts[0], inVerts[1], inVerts[2],
                inVerts[0].d, inVerts[1].d, inVerts[2].d,
                nearD, clipped,
                [](const ClipVertSmooth &a, const ClipVertSmooth &b, float t) noexcept
                {
                    return lerpClipVertSmooth(a, b, t);
                });

            if (outCount < 3)
                return;

            const float viewportHalfWidth = static_cast<float>(viewport.width) * 0.5f;
            const float viewportHalfHeight = static_cast<float>(viewport.height) * 0.5f;
            const int16_t bandTop = g_bandOffsetY;
            const int16_t bandBottom = static_cast<int16_t>(bandTop + g_bandHeight);
            const float viewportWidth = static_cast<float>(viewport.width);
            const float bandTopF = static_cast<float>(bandTop);

            Vector3 proj[4];
            for (int i = 0; i < outCount; ++i)
                proj[i] = CameraController::project(clipped[i].pos, viewProjMatrix,
                                                    viewportHalfWidth, viewportHalfHeight,
                                                    viewport.x, viewport.y);

            auto drawTri = [&](int a, int b, int c)
            {
                drawTriangle3D_Smooth_Preprojected(
                    proj[a], proj[b], proj[c],
                    clipped[a].lr, clipped[a].lg, clipped[a].lb,
                    clipped[b].lr, clipped[b].lg, clipped[b].lb,
                    clipped[c].lr, clipped[c].lg, clipped[c].lb,
                    viewportWidth, bandTop, bandBottom, bandTopF,
                    framebuffer, zBuffer);
            };

            drawTri(0, 1, 2);
            if (outCount == 4)
                drawTri(0, 2, 3);
        }

        PIP3D_HOT inline void drawTriangle3D_Phong_Preprojected(
            const Vector3 &v0, const Vector3 &v1, const Vector3 &v2,
            const Vector3 &p0, const Vector3 &p1, const Vector3 &p2,
            const Vector3 &n0, const Vector3 &n1, const Vector3 &n2,
            float d0, float d1, float d2,
            float baseR, float baseG, float baseB,
            const Vector3 &camPos,
            float viewportWidth,
            int16_t bandTop, int16_t bandBottom, float bandTopF,
            FrameBuffer &framebuffer,
            ZBuffer *zBuffer,
            const Light *lights, int activeLightCount)
        {
            if (bboxCull(p0, p1, p2, bandTop, bandBottom, viewportWidth))
                return;

            Rasterizer::fillTrianglePhong(
                static_cast<int16_t>(p0.x), static_cast<int16_t>(p0.y - bandTopF), p0.z,
                static_cast<int16_t>(p1.x), static_cast<int16_t>(p1.y - bandTopF), p1.z,
                static_cast<int16_t>(p2.x), static_cast<int16_t>(p2.y - bandTopF), p2.z,
                v0.x, v0.y, v0.z,
                v1.x, v1.y, v1.z,
                v2.x, v2.y, v2.z,
                n0.x, n0.y, n0.z,
                n1.x, n1.y, n1.z,
                n2.x, n2.y, n2.z,
                d0, d1, d2,
                baseR, baseG, baseB,
                camPos, lights, activeLightCount,
                framebuffer.getBuffer(), zBuffer, framebuffer.getConfig());
        }

        PIP3D_HOT inline void clipAndDrawNearPhong(
            const ClipVertPhong inVerts[3],
            float nearD,
            float baseR, float baseG, float baseB,
            const Vector3 &camPos,
            const Viewport &viewport,
            const Matrix4x4 &viewProjMatrix,
            FrameBuffer &framebuffer,
            ZBuffer *zBuffer,
            const Light *lights, int activeLightCount)
        {
            ClipVertPhong clipped[4];
            const int outCount = clipTriangleNear(
                inVerts[0], inVerts[1], inVerts[2],
                inVerts[0].d, inVerts[1].d, inVerts[2].d,
                nearD, clipped,
                [](const ClipVertPhong &a, const ClipVertPhong &b, float t) noexcept
                {
                    return lerpClipVertPhong(a, b, t);
                });

            if (outCount < 3)
                return;

            const float viewportHalfWidth = static_cast<float>(viewport.width) * 0.5f;
            const float viewportHalfHeight = static_cast<float>(viewport.height) * 0.5f;
            const int16_t bandTop = g_bandOffsetY;
            const int16_t bandBottom = static_cast<int16_t>(bandTop + g_bandHeight);
            const float viewportWidth = static_cast<float>(viewport.width);
            const float bandTopF = static_cast<float>(bandTop);

            Vector3 proj[4];
            for (int i = 0; i < outCount; ++i)
                proj[i] = CameraController::project(clipped[i].pos, viewProjMatrix,
                                                    viewportHalfWidth, viewportHalfHeight,
                                                    viewport.x, viewport.y);

            auto drawTri = [&](int a, int b, int c)
            {
                drawTriangle3D_Phong_Preprojected(
                    clipped[a].pos, clipped[b].pos, clipped[c].pos,
                    proj[a], proj[b], proj[c],
                    clipped[a].normal, clipped[b].normal, clipped[c].normal,
                    clipped[a].d, clipped[b].d, clipped[c].d,
                    baseR, baseG, baseB,
                    camPos,
                    viewportWidth, bandTop, bandBottom, bandTopF,
                    framebuffer, zBuffer,
                    lights, activeLightCount);
            };

            drawTri(0, 1, 2);
            if (outCount == 4)
                drawTri(0, 2, 3);
        }
    }
}