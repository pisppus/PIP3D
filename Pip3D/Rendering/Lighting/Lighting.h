#ifndef LIGHTING_H
#define LIGHTING_H

#include "../../Core/Core.h"

namespace pip3D
{

    enum LightType
    {
        LIGHT_DIRECTIONAL,
        LIGHT_POINT
    };

    struct Light
    {
        LightType type;
        float intensity;
        Vector3 direction;
        Vector3 position;
        Color color;
        float range;
        float rangeSq;
        float invRangeSq;

        mutable float cachedR, cachedG, cachedB;
        mutable bool colorCacheDirty;

        Light() : type(LIGHT_DIRECTIONAL),
                  intensity(1.0f),
                  direction(0, -1, 0),
                  position(0, 10, 0),
                  color(Color::WHITE),
                  range(0.0f),
                  rangeSq(0.0f),
                  invRangeSq(0.0f),
                  cachedR(0.0f),
                  cachedG(0.0f),
                  cachedB(0.0f),
                  colorCacheDirty(true)
        {
            direction.normalize();
        }

        __attribute__((always_inline)) inline void getCachedRGB(float &r, float &g, float &b) const
        {
            if (unlikely(colorCacheDirty))
            {
                const uint16_t lightRGB = color.rgb565;
                cachedR = ((lightRGB >> 11) & 0x1F) * (1.0f / 31.0f);
                cachedG = ((lightRGB >> 5) & 0x3F) * (1.0f / 63.0f);
                cachedB = (lightRGB & 0x1F) * (1.0f / 31.0f);
                colorCacheDirty = false;
            }
            r = cachedR;
            g = cachedG;
            b = cachedB;
        }

        __attribute__((always_inline)) inline void setRange(float r)
        {
            range = r;
            if (r > 0.0f)
            {
                float rsq = r * r;
                rangeSq = rsq;
                invRangeSq = 1.0f / rsq;
            }
            else
            {
                rangeSq = 0.0f;
                invRangeSq = 0.0f;
            }
        }
    };

}

#endif
