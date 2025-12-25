#ifndef LIGHTMANAGER_H
#define LIGHTMANAGER_H

#include <vector>
#include "Lighting.h"

namespace pip3D
{
    class LightManager
    {
    public:
        static void setLight(std::vector<Light> &lights, int &activeLightCount, int index, const Light &light);
        static int addLight(std::vector<Light> &lights, int &activeLightCount, const Light &light);
        static void removeLight(std::vector<Light> &lights, int &activeLightCount, int index);
        static Light *getLight(std::vector<Light> &lights, int activeLightCount, int index);
        static void clearLights(int &activeLightCount);
        static int getLightCount(int activeLightCount);

        static void setMainDirectionalLight(std::vector<Light> &lights, int &activeLightCount,
                                            const Vector3 &direction, const Color &color, float intensity = 1.0f);

        static void setMainPointLight(std::vector<Light> &lights, int &activeLightCount,
                                      const Vector3 &position, const Color &color,
                                      float intensity = 1.0f, float range = 10.0f);

        static void setLightColor(std::vector<Light> &lights, int activeLightCount, const Color &color);
        static void setLightPosition(std::vector<Light> &lights, int activeLightCount, const Vector3 &pos);
        static void setLightDirection(std::vector<Light> &lights, int activeLightCount, const Vector3 &dir);
        static void setLightTemperature(std::vector<Light> &lights, int activeLightCount, float kelvin);
        static Color getLightColor(const std::vector<Light> &lights, int activeLightCount);
        static void setLightType(std::vector<Light> &lights, int activeLightCount, LightType type);
    };
}

namespace pip3D
{
    inline void LightManager::setLight(std::vector<Light> &lights, int &activeLightCount, int index, const Light &light)
    {
        if (index < 0)
        {
            LOGW(::pip3D::Debug::LOG_MODULE_RENDER,
                 "LightManager::setLight: negative index %d (activeLightCount=%d)",
                 index,
                 activeLightCount);
            return;
        }

        if (index >= static_cast<int>(lights.size()))
        {
            lights.resize(index + 1);
        }

        lights[index] = light;
        lights[index].colorCacheDirty = true;

        if (index + 1 > activeLightCount)
            activeLightCount = index + 1;
    }

    inline int LightManager::addLight(std::vector<Light> &lights, int &activeLightCount, const Light &light)
    {
        if (activeLightCount < static_cast<int>(lights.size()))
        {
            lights[activeLightCount] = light;
        }
        else
        {
            lights.push_back(light);
        }

        lights[activeLightCount].colorCacheDirty = true;
        return activeLightCount++;
    }

    inline void LightManager::removeLight(std::vector<Light> &lights, int &activeLightCount, int index)
    {
        if (index < 0 || index >= activeLightCount)
        {
            LOGW(::pip3D::Debug::LOG_MODULE_RENDER,
                 "LightManager::removeLight: index %d out of range (activeLightCount=%d)",
                 index,
                 activeLightCount);
            return;
        }

        if (index == activeLightCount - 1)
        {
            activeLightCount--;
            return;
        }

        for (int i = index; i < activeLightCount - 1; i++)
        {
            lights[i] = lights[i + 1];
        }

        activeLightCount--;
    }

    inline Light *LightManager::getLight(std::vector<Light> &lights, int activeLightCount, int index)
    {
        if (index < 0 || index >= activeLightCount)
        {
            LOGW(::pip3D::Debug::LOG_MODULE_RENDER,
                 "LightManager::getLight: index %d out of range (activeLightCount=%d)",
                 index,
                 activeLightCount);
            return nullptr;
        }
        return &lights[index];
    }

    inline void LightManager::clearLights(int &activeLightCount)
    {
        activeLightCount = 0;
    }

    inline int LightManager::getLightCount(int activeLightCount)
    {
        return activeLightCount;
    }

    inline void LightManager::setMainDirectionalLight(std::vector<Light> &lights, int &activeLightCount,
                                                      const Vector3 &direction, const Color &color, float intensity)
    {
        if (activeLightCount == 0)
            activeLightCount = 1;
        if (lights.empty())
            lights.resize(1);
        lights[0].type = LIGHT_DIRECTIONAL;
        lights[0].direction = direction;
        lights[0].direction.normalize();
        lights[0].color = color;
        lights[0].intensity = intensity;
        lights[0].colorCacheDirty = true;
    }

    inline void LightManager::setMainPointLight(std::vector<Light> &lights, int &activeLightCount,
                                                const Vector3 &position, const Color &color,
                                                float intensity, float range)
    {
        if (activeLightCount == 0)
            activeLightCount = 1;
        if (lights.empty())
            lights.resize(1);
        lights[0].type = LIGHT_POINT;
        lights[0].position = position;
        lights[0].color = color;
        lights[0].intensity = intensity;
        lights[0].setRange(range);
        lights[0].colorCacheDirty = true;
    }

    inline void LightManager::setLightColor(std::vector<Light> &lights, int activeLightCount, const Color &color)
    {
        if (activeLightCount == 0 || lights.empty())
        {
            LOGW(::pip3D::Debug::LOG_MODULE_RENDER,
                 "LightManager::setLightColor called with no active lights (activeLightCount=%d, lightsEmpty=%d)",
                 activeLightCount,
                 lights.empty() ? 1 : 0);
            return;
        }
        lights[0].color = color;
        lights[0].colorCacheDirty = true;
    }

    inline void LightManager::setLightPosition(std::vector<Light> &lights, int activeLightCount, const Vector3 &pos)
    {
        if (activeLightCount == 0 || lights.empty())
        {
            LOGW(::pip3D::Debug::LOG_MODULE_RENDER,
                 "LightManager::setLightPosition called with no active lights (activeLightCount=%d, lightsEmpty=%d)",
                 activeLightCount,
                 lights.empty() ? 1 : 0);
            return;
        }
        lights[0].position = pos;
    }

    inline void LightManager::setLightDirection(std::vector<Light> &lights, int activeLightCount, const Vector3 &dir)
    {
        if (activeLightCount == 0 || lights.empty())
        {
            LOGW(::pip3D::Debug::LOG_MODULE_RENDER,
                 "LightManager::setLightDirection called with no active lights (activeLightCount=%d, lightsEmpty=%d)",
                 activeLightCount,
                 lights.empty() ? 1 : 0);
            return;
        }
        lights[0].direction = dir;
        lights[0].direction.normalize();
    }

    inline void LightManager::setLightTemperature(std::vector<Light> &lights, int activeLightCount, float kelvin)
    {
        if (activeLightCount == 0 || lights.empty())
        {
            LOGW(::pip3D::Debug::LOG_MODULE_RENDER,
                 "LightManager::setLightTemperature called with no active lights (activeLightCount=%d, lightsEmpty=%d)",
                 activeLightCount,
                 lights.empty() ? 1 : 0);
            return;
        }
        Color color = Color::fromTemperature(kelvin);
        lights[0].color = color;
        lights[0].colorCacheDirty = true;
    }

    inline Color LightManager::getLightColor(const std::vector<Light> &lights, int activeLightCount)
    {
        if (activeLightCount == 0 || lights.empty())
        {
            LOGW(::pip3D::Debug::LOG_MODULE_RENDER,
                 "LightManager::getLightColor called with no active lights (activeLightCount=%d, lightsEmpty=%d)",
                 activeLightCount,
                 lights.empty() ? 1 : 0);
            return Color::WHITE;
        }
        return lights[0].color;
    }

    inline void LightManager::setLightType(std::vector<Light> &lights, int activeLightCount, LightType type)
    {
        if (activeLightCount == 0 || lights.empty())
        {
            LOGW(::pip3D::Debug::LOG_MODULE_RENDER,
                 "LightManager::setLightType called with no active lights (activeLightCount=%d, lightsEmpty=%d)",
                 activeLightCount,
                 lights.empty() ? 1 : 0);
            return;
        }
        lights[0].type = type;
    }
}

#endif
