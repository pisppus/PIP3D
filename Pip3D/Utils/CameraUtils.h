#ifndef CAMERAUTILS_H
#define CAMERAUTILS_H

#include "../Core/Camera.h"
#include "../Rendering/Renderer.h"

namespace pip3D
{

    class CameraHelper
    {
    public:
        static void quickSetup(Camera &cam, float fov, float nearPlane, float farPlane)
        {
            cam.setPerspective(fov, nearPlane, farPlane);
        }
    };

    class MultiCameraHelper
    {
    public:
        static int createIsometricCamera(Renderer &renderer, float distance)
        {
            int camIdx = renderer.createCamera();
            if (camIdx < 0)
                return -1;

            Camera &cam = renderer.getCamera(camIdx);
            cam.setOrtho(distance, distance, 0.1f, 100.0f);

            float angle = 0.785398f;
            float dist = distance * 1.5f;
            cam.position = Vector3(dist * cosf(angle), dist * 0.7f, dist * sinf(angle));
            cam.target = Vector3(0, 0, 0);
            cam.markDirty();

            return camIdx;
        }
    };

}

#endif
