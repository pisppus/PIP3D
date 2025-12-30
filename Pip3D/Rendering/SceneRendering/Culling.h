#ifndef CULLING_H
#define CULLING_H

#include "../../Core/Core.h"
#include "../../Core/Camera.h"
#include "../../Math/Math.h"
#include "../Display/ZBuffer.h"
#include "CameraController.h"

#ifndef IRAM_ATTR
#define IRAM_ATTR
#endif

namespace pip3D
{
    class Culling
    {
    public:
        static bool IRAM_ATTR isInstanceOccluded(const Vector3 &center,
                                                 float radius,
                                                 const Camera &camera,
                                                 const Viewport &viewport,
                                                 const Matrix4x4 &viewProjMatrix,
                                                 ZBuffer<SCREEN_WIDTH, SCREEN_BAND_HEIGHT> *zBuffer,
                                                 const DisplayConfig &cfg)
        {
            if (!zBuffer || radius <= 0.0f)
                return false;

            Vector3 pc = CameraController::project(center, viewProjMatrix, viewport);

            if (camera.projectionType == PERSPECTIVE && pc.z <= 0.0f)
                return false;

            Vector3 toCenter = center - camera.position;
            float distSq = toCenter.dot(toCenter);
            if (distSq <= 1e-8f)
                return false;

            float distToCenter = sqrtf(distSq);
            Vector3 dirToCenter = toCenter * (1.0f / distToCenter);

            Vector3 frontWorld = center - dirToCenter * radius;
            Vector3 frontProj = viewProjMatrix.transform(frontWorld);
            float objDepth = frontProj.z;

            Vector3 py = CameraController::project(Vector3(center.x, center.y + radius, center.z),
                                                   viewProjMatrix, viewport);

            float rScr = fabsf(py.y - pc.y);
            if (rScr < 1.0f)
                rScr = 1.0f;

            int16_t cx = (int16_t)pc.x;
            int16_t cy = (int16_t)pc.y;

            const float eps = 1e-3f;

            const int16_t offsets[5][2] = {
                {0, 0},
                {(int16_t)rScr, 0},
                {(int16_t)-rScr, 0},
                {0, (int16_t)rScr},
                {0, (int16_t)-rScr}};

            int validSamples = 0;

            for (int i = 0; i < 5; ++i)
            {
                int16_t sx = cx + offsets[i][0];
                int16_t sy = cy + offsets[i][1];

                if (sx < 0 || sy < 0 || sx >= cfg.width || sy >= cfg.height)
                    continue;

                ++validSamples;

                float depth = zBuffer->getDepth01((uint16_t)sx, (uint16_t)sy);

                if (depth >= 1.0f)
                    return false;

                if (depth >= objDepth - eps)
                    return false;
            }

            if (validSamples == 0)
                return false;

            return true;
        }
    };
}

#endif
