#ifndef SHADOW_H
#define SHADOW_H

#include "../../Core/Core.h"
#include "../../Math/Math.h"

namespace pip3D
{

    class ShadowProjector
    {
    public:
        struct ShadowPlane
        {
            Vector3 normal;
            float d;

            ShadowPlane() : normal(0, 1, 0), d(0) {}
            ShadowPlane(const Vector3 &n, float distance) : normal(n), d(distance)
            {
                normal.normalize();
            }

            static ShadowPlane fromPointAndNormal(const Vector3 &point, const Vector3 &normal)
            {
                Vector3 n = normal;
                n.normalize();
                float d = -(n.x * point.x + n.y * point.y + n.z * point.z);
                return ShadowPlane(n, d);
            }
        };
    };

    struct ShadowSettings
    {
        bool enabled;
        Color shadowColor;
        float shadowOpacity;
        float shadowOffset;
        bool softEdges;
        ShadowProjector::ShadowPlane plane;

        ShadowSettings()
            : enabled(true), shadowColor(Color::fromRGB888(20, 20, 30)),
              shadowOpacity(0.7f),
              shadowOffset(0.01f), softEdges(true), plane(Vector3(0, 1, 0), 0)
        {
        }
    };

}

#endif
