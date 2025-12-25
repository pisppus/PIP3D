#ifndef SCENEHELPER_H
#define SCENEHELPER_H

#include "../Rendering/Renderer.h"
#include "../Geometry/PrimitiveShapes.h"
#include "../Core/Core.h"

namespace pip3D
{

    class SceneHelper
    {
    private:
        Renderer *renderer;
        float groundSize;
        float groundY;
        Color groundColor;
        bool hasSun;
        float sunIntensity;
        Plane *groundPlane;

    public:
        SceneHelper(Renderer *r) : renderer(r), groundSize(15.0f), groundY(-1.5f),
                                   groundColor(Color::fromRGB888(100, 100, 100)),
                                   hasSun(false), sunIntensity(0.6f), groundPlane(nullptr) {}

        void addGround(float size, float y, Color color)
        {
            groundSize = size;
            groundY = y;
            groundColor = color;

            if (!groundPlane)
            {
                groundPlane = new Plane(size, size, 1, color);
                groundPlane->setPosition(0, y, 0);
            }
            else
            {
                groundPlane->color(color);
                groundPlane->setPosition(0, y, 0);
            }
        }

        void addSun(float intensity, float temperature)
        {
            hasSun = true;
            sunIntensity = intensity;
        }

        void renderGround()
        {
            if (groundPlane && renderer)
            {
                renderer->drawMesh(groundPlane);
            }
        }

        ~SceneHelper()
        {
            if (groundPlane)
                delete groundPlane;
        }

        void renderSun(float glowIntensity, float temperature)
        {
            if (!renderer || !hasSun)
                return;

            Light *mainLight = renderer->getLight(0);
            if (!mainLight)
                return;

            Vector3 sunDir = mainLight->direction;
            Vector3 sunPos = sunDir * -20.0f;
            Color sunColor = Color::fromTemperature(temperature);

            renderer->drawSunSprite(sunPos, sunColor, glowIntensity);
        }

        void setSunPosition(float x, float y, float z)
        {
            if (!renderer)
                return;

            Vector3 sunPos(x, y, z);
            Vector3 sunDir = Vector3(0, 0, 0) - sunPos;
            sunDir.normalize();

            renderer->setLightDirection(sunDir);
            renderer->setLightPosition(sunPos);
        }

        void setSunPosition(float x, float y, float z, float temperature)
        {
            if (!renderer)
                return;

            Vector3 sunPos(x, y, z);
            Vector3 sunDir = Vector3(0, 0, 0) - sunPos;
            sunDir.normalize();

            Color sunColor = Color::fromTemperature(temperature);
            renderer->setMainDirectionalLight(sunDir, sunColor, 1.0f);
        }
    };

}

#endif
