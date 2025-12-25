#ifndef PIP3D_PHYSICS_CONSTRAINTS_H
#define PIP3D_PHYSICS_CONSTRAINTS_H

#include <math.h>

#include "Body.h"

namespace pip3D
{

    struct Constraint
    {
        RigidBody *a;
        RigidBody *b;
        bool enabled;

        Constraint(RigidBody *a_, RigidBody *b_)
            : a(a_), b(b_), enabled(true) {}

        virtual ~Constraint() {}

        virtual void preStep(float deltaTime) = 0;
        virtual void solve(float deltaTime) = 0;
    };

    struct DistanceConstraint : public Constraint
    {
        Vector3 localAnchorA;
        Vector3 localAnchorB;
        float restLength;
        float effectiveMass;
        float bias;
        Vector3 rA;
        Vector3 rB;
        Vector3 n;

        DistanceConstraint(RigidBody *a_,
                           RigidBody *b_,
                           const Vector3 &localA,
                           const Vector3 &localB,
                           float restLength_)
            : Constraint(a_, b_),
              localAnchorA(localA),
              localAnchorB(localB),
              restLength(restLength_),
              effectiveMass(0.0f),
              bias(0.0f),
              rA(0.0f, 0.0f, 0.0f),
              rB(0.0f, 0.0f, 0.0f),
              n(0.0f, 1.0f, 0.0f)
        {
        }

        virtual void preStep(float deltaTime) override
        {
            if (!enabled)
                return;

            RigidBody *aBody = a;
            RigidBody *bBody = b;
            if (!aBody || !bBody)
                return;

            float invMassA = aBody->invMass;
            float invMassB = bBody->invMass;
            float invMassSum = invMassA + invMassB;
            if (invMassSum <= 0.0f)
            {
                effectiveMass = 0.0f;
                bias = 0.0f;
                return;
            }

            Vector3 worldAnchorA = aBody->position + aBody->orientation.rotate(localAnchorA);
            Vector3 worldAnchorB = bBody->position + bBody->orientation.rotate(localAnchorB);

            rA = worldAnchorA - aBody->position;
            rB = worldAnchorB - bBody->position;

            Vector3 delta = worldAnchorB - worldAnchorA;
            float distSq = delta.lengthSquared();
            float C = 0.0f;

            if (distSq > 1e-8f)
            {
                float dist = sqrtf(distSq);
                float invLen = 1.0f / dist;
                n = delta * invLen;
                C = dist - restLength;
            }
            else
            {
                n = Vector3(0.0f, 1.0f, 0.0f);
                C = -restLength;
            }

            Vector3 rAxn = rA.cross(n);
            Vector3 rBxn = rB.cross(n);
            Vector3 invInertiaA(aBody->invInertia.x * rAxn.x,
                                aBody->invInertia.y * rAxn.y,
                                aBody->invInertia.z * rAxn.z);
            Vector3 invInertiaB(bBody->invInertia.x * rBxn.x,
                                bBody->invInertia.y * rBxn.y,
                                bBody->invInertia.z * rBxn.z);
            Vector3 crossA = invInertiaA.cross(rA);
            Vector3 crossB = invInertiaB.cross(rB);
            float angularTerm = crossA.dot(n) + crossB.dot(n);
            float denom = invMassSum + angularTerm;

            if (denom > 0.0f)
                effectiveMass = 1.0f / denom;
            else
                effectiveMass = 0.0f;

            const float baumgarte = 0.2f;
            if (deltaTime > 0.0f)
                bias = -baumgarte * C / deltaTime;
            else
                bias = 0.0f;
        }

        virtual void solve(float /*deltaTime*/) override
        {
            if (!enabled)
                return;

            RigidBody *aBody = a;
            RigidBody *bBody = b;
            if (!aBody || !bBody)
                return;
            if (effectiveMass <= 0.0f)
                return;

            Vector3 vA = aBody->velocity + aBody->angularVelocity.cross(rA);
            Vector3 vB = bBody->velocity + bBody->angularVelocity.cross(rB);
            Vector3 relativeVelocity = vB - vA;
            float relVelAlongN = relativeVelocity.dot(n);

            float lambda = -(relVelAlongN + bias) * effectiveMass;

            Vector3 impulse = n * lambda;

            if (!aBody->isStatic && aBody->invMass > 0.0f)
            {
                aBody->velocity -= impulse * aBody->invMass;
                Vector3 angImpulse = rA.cross(impulse);
                aBody->angularVelocity.x -= angImpulse.x * aBody->invInertia.x;
                aBody->angularVelocity.y -= angImpulse.y * aBody->invInertia.y;
                aBody->angularVelocity.z -= angImpulse.z * aBody->invInertia.z;
            }

            if (!bBody->isStatic && bBody->invMass > 0.0f)
            {
                bBody->velocity += impulse * bBody->invMass;
                Vector3 angImpulse = rB.cross(impulse);
                bBody->angularVelocity.x += angImpulse.x * bBody->invInertia.x;
                bBody->angularVelocity.y += angImpulse.y * bBody->invInertia.y;
                bBody->angularVelocity.z += angImpulse.z * bBody->invInertia.z;
            }
        }
    };

    struct PointConstraint : public DistanceConstraint
    {
        PointConstraint(RigidBody *a_,
                        RigidBody *b_,
                        const Vector3 &localA,
                        const Vector3 &localB)
            : DistanceConstraint(a_, b_, localA, localB, 0.0f)
        {
        }
    };

}

#endif
