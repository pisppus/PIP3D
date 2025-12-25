#ifndef CAMERACONTROLLER_H
#define CAMERACONTROLLER_H

#include "../../Core/Core.h"
#include "../../Core/Camera.h"
#include "../../Core/Frustum.h"
#include "../../Math/Math.h"

namespace pip3D
{
    class CameraController
    {
    public:
        static void updateViewProjectionIfNeeded(Camera &camera,
                                                 const Viewport &viewport,
                                                 Matrix4x4 &viewMatrix,
                                                 Matrix4x4 &projMatrix,
                                                 Matrix4x4 &viewProjMatrix,
                                                 Frustum &frustum,
                                                 bool &viewProjMatrixDirty,
                                                 bool &cameraChangedThisFrame)
        {
            if (viewProjMatrixDirty || camera.cache.flags.viewDirty || camera.cache.flags.projDirty)
            {
                cameraChangedThisFrame = true;
                float aspect = (float)viewport.width / viewport.height;
                viewMatrix = camera.getViewMatrix();
                projMatrix = camera.getProjectionMatrix(aspect);
                viewProjMatrix = projMatrix * viewMatrix;
                viewProjMatrixDirty = false;
                frustum.extractFromViewProjection(viewProjMatrix);
            }
        }

        static Vector3 project(const Vector3 &v,
                               const Matrix4x4 &viewProjMatrix,
                               const Viewport &viewport)
        {
            Vector3 projected = viewProjMatrix.transform(v);
            projected.x = (projected.x + 1.0f) * viewport.width * 0.5f + viewport.x;
            projected.y = (1.0f - projected.y) * viewport.height * 0.5f + viewport.y;
            return projected;
        }
    };
}

#endif
