#ifndef PIP3D_PHYSICS_BODY_H
#define PIP3D_PHYSICS_BODY_H

#include "../Math/Collision.h"

namespace pip3D
{

    enum BodyShape
    {
        BODY_SHAPE_BOX = 0,
        BODY_SHAPE_SPHERE = 1
    };

    struct PhysicsMaterial
    {
        float friction;
        float restitution;

        PhysicsMaterial()
            : friction(0.5f), restitution(0.5f) {}

        PhysicsMaterial(float f, float r)
            : friction(f), restitution(r) {}
    };

    struct __attribute__((aligned(16))) RigidBody
    {
        Vector3 position;
        Vector3 previousPosition;
        Vector3 velocity;
        Vector3 acceleration;
        Vector3 angularVelocity;
        Quaternion orientation;
        Vector3 size;
        float mass;
        float invMass;
        float restitution;
        float friction;
        bool isStatic;
        bool isKinematic;
        bool isTrigger;
        BodyShape shape;
        float radius;
        Vector3 invInertia;
        AABB bounds;
        bool canSleep;
        bool isSleeping;
        float sleepTimer;

        RigidBody()
            : position(0, 0, 0), previousPosition(0, 0, 0), velocity(0, 0, 0), acceleration(0, 0, 0),
              angularVelocity(0, 0, 0), orientation(), size(1, 1, 1),
              mass(1.0f), invMass(1.0f), restitution(0.5f), friction(0.5f),
              isStatic(false), isKinematic(false), isTrigger(false), shape(BODY_SHAPE_BOX), radius(0.5f),
              invInertia(0, 0, 0), bounds(AABB::fromCenterSize(Vector3(0, 0, 0), Vector3(1, 1, 1))),
              canSleep(true), isSleeping(false), sleepTimer(0.0f)
        {
            computeInertia();
        }

        RigidBody(const Vector3 &pos, const Vector3 &size_, float m = 1.0f)
            : position(pos), previousPosition(pos), velocity(0, 0, 0), acceleration(0, 0, 0),
              angularVelocity(0, 0, 0), orientation(), size(size_),
              mass(m), invMass(m > 0.0f ? 1.0f / m : 0.0f), restitution(0.5f), friction(0.5f),
              isStatic(false), isKinematic(false), isTrigger(false), shape(BODY_SHAPE_BOX), radius(size_.x * 0.5f),
              invInertia(0, 0, 0), bounds(AABB::fromCenterSize(pos, size_)),
              canSleep(true), isSleeping(false), sleepTimer(0.0f)
        {
            computeInertia();
        }

        __attribute__((always_inline)) inline void setBox(const Vector3 &newSize)
        {
            size = newSize;
            shape = BODY_SHAPE_BOX;
            radius = newSize.x * 0.5f;
            updateBoundsFromTransform();
            computeInertia();
        }

        __attribute__((always_inline)) inline void setSphere(float r)
        {
            shape = BODY_SHAPE_SPHERE;
            radius = r;
            size = Vector3(r * 2.0f, r * 2.0f, r * 2.0f);
            updateBoundsFromTransform();
            computeInertia();
        }

        __attribute__((always_inline)) inline void applyForce(const Vector3 &force)
        {
            if (!isStatic && !isKinematic && mass > 0.0f)
            {
                acceleration += force * invMass;
                isSleeping = false;
                sleepTimer = 0.0f;
            }
        }

        __attribute__((always_inline)) inline void applyGravity(float gravityValue = -9.81f)
        {
            if (!isStatic && !isKinematic)
            {
                acceleration.y += gravityValue;
                isSleeping = false;
                sleepTimer = 0.0f;
            }
        }

        __attribute__((always_inline)) inline void update(float deltaTime)
        {
            if (isStatic || isSleeping)
                return;

            velocity += acceleration * deltaTime;
            const float linearDamping = 0.3f;
            const float angularDamping = 2.0f;
            float linFactor = 1.0f - linearDamping * deltaTime;
            float angFactor = 1.0f - angularDamping * deltaTime;
            if (linFactor < 0.0f)
                linFactor = 0.0f;
            if (angFactor < 0.0f)
                angFactor = 0.0f;
            velocity *= linFactor;
            angularVelocity *= angFactor;
            float vLenSq = velocity.lengthSquared();
            if (vLenSq < 1e-5f)
            {
                velocity = Vector3(0, 0, 0);
                vLenSq = 0.0f;
            }
            if (vLenSq > 0.0f)
            {
                const float maxLinVel = 40.0f;
                float maxLinVelSq = maxLinVel * maxLinVel;
                if (vLenSq > maxLinVelSq)
                {
                    float invLen = FastMath::fastInvSqrt(vLenSq);
                    float scale = maxLinVel * invLen;
                    velocity *= scale;
                }
            }

            float angLenSq = angularVelocity.lengthSquared();
            if (angLenSq < 1e-5f)
            {
                angularVelocity = Vector3(0, 0, 0);
                angLenSq = 0.0f;
            }

            position += velocity * deltaTime;

            if (angLenSq > 1e-8f)
            {
                const float maxAngVel = 10.0f;
                float maxAngVelSq = maxAngVel * maxAngVel;
                if (angLenSq > maxAngVelSq)
                {
                    float invLen = FastMath::fastInvSqrt(angLenSq);
                    float scale = maxAngVel * invLen;
                    angularVelocity *= scale;
                    angLenSq = angularVelocity.lengthSquared();
                }
                float halfDt = 0.5f * deltaTime;
                Quaternion omega(
                    angularVelocity.x * halfDt,
                    angularVelocity.y * halfDt,
                    angularVelocity.z * halfDt,
                    0.0f);
                Quaternion deltaQ = omega * orientation;
                orientation.x += deltaQ.x;
                orientation.y += deltaQ.y;
                orientation.z += deltaQ.z;
                orientation.w += deltaQ.w;
                orientation.normalize();
            }

            updateBoundsFromTransform();
            acceleration = Vector3(0, 0, 0);
        }

        __attribute__((always_inline)) inline void setPosition(const Vector3 &pos)
        {
            position = pos;
            previousPosition = pos;
            updateBoundsFromTransform();
            isSleeping = false;
            sleepTimer = 0.0f;
        }

        __attribute__((always_inline)) inline void setStatic(bool s)
        {
            isStatic = s;
            if (isStatic)
            {
                isKinematic = false;
                isTrigger = false;
            }
            if (isStatic)
            {
                velocity = Vector3(0, 0, 0);
                acceleration = Vector3(0, 0, 0);
                angularVelocity = Vector3(0, 0, 0);
                isSleeping = false;
                sleepTimer = 0.0f;
            }
            computeInertia();
        }

        __attribute__((always_inline)) inline void setKinematic(bool k)
        {
            isKinematic = k;
            if (isKinematic)
            {
                isStatic = false;
                canSleep = false;
                isSleeping = false;
                sleepTimer = 0.0f;
                velocity = Vector3(0, 0, 0);
                acceleration = Vector3(0, 0, 0);
                angularVelocity = Vector3(0, 0, 0);
            }
            computeInertia();
        }

        __attribute__((always_inline)) inline void setTrigger(bool t)
        {
            isTrigger = t;
        }

        __attribute__((always_inline)) inline void wakeUp()
        {
            if (isSleeping)
            {
                isSleeping = false;
                sleepTimer = 0.0f;
            }
        }

        __attribute__((always_inline)) inline void setCanSleep(bool value)
        {
            canSleep = value;
            if (!canSleep)
            {
                isSleeping = false;
                sleepTimer = 0.0f;
            }
        }

        __attribute__((always_inline)) inline void setMaterial(const PhysicsMaterial &m)
        {
            friction = m.friction;
            restitution = m.restitution;
        }

        __attribute__((always_inline)) inline void updateBoundsFromTransform()
        {
            if (shape == BODY_SHAPE_SPHERE)
            {
                bounds = AABB::fromCenterSize(position, size);
                return;
            }

            Vector3 half = size * 0.5f;

            Vector3 ex = orientation.rotate(Vector3(1.0f, 0.0f, 0.0f));
            Vector3 ey = orientation.rotate(Vector3(0.0f, 1.0f, 0.0f));
            Vector3 ez = orientation.rotate(Vector3(0.0f, 0.0f, 1.0f));

            ex.x = fabsf(ex.x);
            ex.y = fabsf(ex.y);
            ex.z = fabsf(ex.z);
            ey.x = fabsf(ey.x);
            ey.y = fabsf(ey.y);
            ey.z = fabsf(ey.z);
            ez.x = fabsf(ez.x);
            ez.y = fabsf(ez.y);
            ez.z = fabsf(ez.z);

            Vector3 r;
            r.x = ex.x * half.x + ey.x * half.y + ez.x * half.z;
            r.y = ex.y * half.x + ey.y * half.y + ez.y * half.z;
            r.z = ex.z * half.x + ey.z * half.y + ez.z * half.z;

            bounds.min = position - r;
            bounds.max = position + r;
        }

    private:
        __attribute__((always_inline)) inline void computeInertia()
        {
            if (isStatic || isKinematic || mass <= 0.0f)
            {
                invMass = 0.0f;
                invInertia = Vector3(0, 0, 0);
                return;
            }
            invMass = 1.0f / mass;
            if (shape == BODY_SHAPE_BOX)
            {
                float hx = size.x * 0.5f;
                float hy = size.y * 0.5f;
                float hz = size.z * 0.5f;
                float ix = (mass / 12.0f) * (hy * hy + hz * hz);
                float iy = (mass / 12.0f) * (hx * hx + hz * hz);
                float iz = (mass / 12.0f) * (hx * hx + hy * hy);
                invInertia.x = ix > 0.0f ? 1.0f / ix : 0.0f;
                invInertia.y = iy > 0.0f ? 1.0f / iy : 0.0f;
                invInertia.z = iz > 0.0f ? 1.0f / iz : 0.0f;
            }
            else
            {
                float r = radius;
                float i = 0.4f * mass * r * r;
                float invI = i > 0.0f ? 1.0f / i : 0.0f;
                invInertia = Vector3(invI, invI, invI);
            }
        }
    };

}

#endif
