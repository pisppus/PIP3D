#ifndef PIP3D_PHYSICS_BUOYANCY_H
#define PIP3D_PHYSICS_BUOYANCY_H

#include <float.h>

#include "../Math/Collision.h"

namespace pip3D
{

    struct BuoyancyZone : public AABB
    {
        float surfaceLevel;
        float density;
        float dragLinear;
        float dragAngular;

        BuoyancyZone()
            : AABB(Vector3(-FLT_MAX, -FLT_MAX, -FLT_MAX), Vector3(FLT_MAX, FLT_MAX, FLT_MAX)),
              surfaceLevel(0.0f),
              density(1.5f),
              dragLinear(2.0f),
              dragAngular(2.0f)
        {
        }

        BuoyancyZone(const AABB &bounds,
                     float surface,
                     float density_ = 1.5f,
                     float dragL = 2.0f,
                     float dragA = 2.0f)
            : AABB(bounds),
              surfaceLevel(surface),
              density(density_),
              dragLinear(dragL),
              dragAngular(dragA)
        {
        }
    };

}

#endif
