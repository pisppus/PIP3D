#ifndef PIP3D_PHYSICS_CONTACTS_H
#define PIP3D_PHYSICS_CONTACTS_H

#include "../Math/Collision.h"
#include "Body.h"

namespace pip3D
{

    struct Contact
    {
        Vector3 pos;
        float penetration;
        float accumulatedImpulse;
        float normalMass;
        float bias;

        Contact()
            : pos(0.0f, 0.0f, 0.0f),
              penetration(0.0f),
              accumulatedImpulse(0.0f),
              normalMass(0.0f),
              bias(0.0f) {}
    };

    struct CollisionInfo
    {
        bool hasCollision;
        Vector3 normal;
        Contact contacts[4];
        int contactCount;
        RigidBody *bodyA;
        RigidBody *bodyB;

        CollisionInfo()
            : hasCollision(false),
              normal(0.0f, 1.0f, 0.0f),
              contactCount(0),
              bodyA(nullptr),
              bodyB(nullptr) {}
    };

    struct RaycastHit
    {
        bool hit;
        Vector3 point;
        Vector3 normal;
        float distance;
        RigidBody *body;

        RaycastHit()
            : hit(false), point(0.0f, 0.0f, 0.0f), normal(0.0f, 1.0f, 0.0f), distance(0.0f), body(nullptr) {}
    };

}

#endif
