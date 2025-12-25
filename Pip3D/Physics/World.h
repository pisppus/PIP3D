#ifndef PIP3D_PHYSICS_WORLD_H
#define PIP3D_PHYSICS_WORLD_H

#include "../Math/Collision.h"
#include "../Core/Jobs.h"
#include "../Core/Debug/Logging.h"
#include "../Core/Debug/DebugDraw.h"
#include <vector>
#include <float.h>

#include "Body.h"
#include "Contacts.h"
#include "Constraints.h"

namespace pip3D
{

    class PhysicsWorld
    {
    private:
        static constexpr int SOLVER_ITERATIONS = 8;
        static constexpr int MAX_SUBSTEPS = 3;
        std::vector<RigidBody *> bodies;
        std::vector<Constraint *> constraints;
        Vector3 gravity;
        bool asyncEnabled;
        bool stepInProgress;
        float pendingDelta;
        float fixedTimeStep;
        float accumulator;
        float currentDeltaTime;
        std::vector<CollisionInfo> contactConstraints;
        std::vector<CollisionInfo> previousContactConstraints;

    public:
        PhysicsWorld()
            : gravity(0, -9.81f, 0), asyncEnabled(true), stepInProgress(false),
              pendingDelta(0.0f), fixedTimeStep(1.0f / 120.0f), accumulator(0.0f), currentDeltaTime(0.0f) {}

        bool addBody(RigidBody *body)
        {
            if (!body)
            {
                LOGE(::pip3D::Debug::LOG_MODULE_PHYSICS,
                     "PhysicsWorld::addBody called with null body");
                return false;
            }
            bodies.push_back(body);

            LOGI(::pip3D::Debug::LOG_MODULE_PHYSICS,
                 "PhysicsWorld::addBody: bodyCount=%u",
                 static_cast<unsigned int>(bodies.size()));
            return true;
        }

        bool addConstraint(Constraint *c)
        {
            if (!c)
            {
                LOGE(::pip3D::Debug::LOG_MODULE_PHYSICS,
                     "PhysicsWorld::addConstraint called with null constraint");
                return false;
            }
            constraints.push_back(c);
            return true;
        }

        void removeConstraint(Constraint *c)
        {
            for (size_t i = 0; i < constraints.size(); ++i)
            {
                if (constraints[i] == c)
                {
                    constraints[i] = constraints.back();
                    constraints.pop_back();
                    break;
                }
            }
        }

        void removeBody(RigidBody *body)
        {
            for (size_t i = 0; i < bodies.size(); i++)
            {
                if (bodies[i] == body)
                {
                    bodies[i] = bodies.back();
                    bodies.pop_back();
                    break;
                }
            }
        }

        void setGravity(const Vector3 &g)
        {
            gravity = g;
        }

        void setAsyncEnabled(bool enabled)
        {
            asyncEnabled = enabled;
        }

        bool isAsyncEnabled() const
        {
            return asyncEnabled && JobSystem::isEnabled();
        }

        bool isStepInProgress() const
        {
            return stepInProgress;
        }

        void setFixedTimeStep(float dt)
        {
            fixedTimeStep = dt;
        }

        float getFixedTimeStep() const
        {
            return fixedTimeStep;
        }

        void updateFixed(float frameDelta)
        {
            float dt = fixedTimeStep;
            if (dt <= 0.0f)
            {
                stepInternal(frameDelta);
                return;
            }

            accumulator += frameDelta;
            float maxAccum = dt * static_cast<float>(MAX_SUBSTEPS);
            if (accumulator > maxAccum)
                accumulator = maxAccum;

            if (isAsyncEnabled())
            {
                if (!stepInProgress && accumulator >= dt)
                {
                    stepAsync(dt);
                    accumulator -= dt;
                }
            }
            else
            {
                int steps = 0;
                while (accumulator >= dt && steps < MAX_SUBSTEPS)
                {
                    stepInternal(dt);
                    accumulator -= dt;
                    ++steps;
                }
            }
        }

        void stepAsync(float deltaTime)
        {
            if (!isAsyncEnabled())
            {
                stepInternal(deltaTime);
                return;
            }

            if (stepInProgress)
                return;

            pendingDelta = deltaTime;

            if (JobSystem::submit(&PhysicsWorld::stepJobFunc, this))
            {
                stepInProgress = true;
            }
            else
            {
                LOGW(::pip3D::Debug::LOG_MODULE_PHYSICS,
                     "PhysicsWorld::stepAsync: JobSystem::submit failed, physics step skipped");
            }
        }

        void debugDraw(Renderer &renderer)
        {
            size_t bodyCount = bodies.size();
            for (size_t i = 0; i < bodyCount; ++i)
            {
                RigidBody *b = bodies[i];
                if (!b)
                    continue;

                uint16_t color = Color::GREEN;
                if (b->isStatic)
                {
                    color = Color::RED;
                }
                else if (b->isSleeping)
                {
                    color = Color::GRAY;
                }

                DBG_AABB(renderer,
                         b->bounds,
                         color,
                         ::pip3D::Debug::DEBUG_CATEGORY_PHYSICS);

                if (b->shape == BODY_SHAPE_SPHERE)
                {
                    DBG_SPHERE(renderer,
                               b->position,
                               b->radius,
                               color,
                               ::pip3D::Debug::DEBUG_CATEGORY_PHYSICS);
                }
                else
                {
                    Vector3 half = b->size * 0.5f;

                    Vector3 local[8];
                    local[0] = Vector3(-half.x, -half.y, -half.z);
                    local[1] = Vector3(half.x, -half.y, -half.z);
                    local[2] = Vector3(half.x, half.y, -half.z);
                    local[3] = Vector3(-half.x, half.y, -half.z);
                    local[4] = Vector3(-half.x, -half.y, half.z);
                    local[5] = Vector3(half.x, -half.y, half.z);
                    local[6] = Vector3(half.x, half.y, half.z);
                    local[7] = Vector3(-half.x, half.y, half.z);

                    Vector3 corners[8];
                    for (int c = 0; c < 8; ++c)
                    {
                        corners[c] = b->orientation.rotate(local[c]) + b->position;
                    }

                    static const int edges[12][2] = {
                        {0, 1}, {1, 2}, {2, 3}, {3, 0},
                        {4, 5}, {5, 6}, {6, 7}, {7, 4},
                        {0, 4}, {1, 5}, {2, 6}, {3, 7}};

                    for (int e = 0; e < 12; ++e)
                    {
                        const Vector3 &a = corners[edges[e][0]];
                        const Vector3 &bpt = corners[edges[e][1]];
                        DBG_LINE(renderer,
                                 a,
                                 bpt,
                                 color,
                                 ::pip3D::Debug::DEBUG_CATEGORY_PHYSICS);
                    }
                }
            }

            size_t constraintCount = contactConstraints.size();
            for (size_t i = 0; i < constraintCount; ++i)
            {
                CollisionInfo &info = contactConstraints[i];
                if (!info.hasCollision || info.contactCount <= 0)
                    continue;

                Vector3 n = info.normal;
                float nLenSq = n.lengthSquared();
                if (nLenSq > 1e-8f)
                {
                    float invLen = FastMath::fastInvSqrt(nLenSq);
                    n *= invLen;
                }

                for (int j = 0; j < info.contactCount; ++j)
                {
                    Contact &c = info.contacts[j];
                    Vector3 pos = c.pos;

                    DBG_SPHERE(renderer,
                               pos,
                               0.08f,
                               Color::RED,
                               ::pip3D::Debug::DEBUG_CATEGORY_PHYSICS);

                    DBG_RAY(renderer,
                            pos,
                            n,
                            0.2f,
                            Color::YELLOW,
                            ::pip3D::Debug::DEBUG_CATEGORY_PHYSICS);
                }
            }
        }

    private:
        static void stepJobFunc(void *userData)
        {
            PhysicsWorld *self = static_cast<PhysicsWorld *>(userData);
            if (self)
                self->runStepJob();
        }

        void runStepJob()
        {
            float dt = pendingDelta;
            pendingDelta = 0.0f;

            if (dt <= 0.0f)
            {
                stepInProgress = false;
                return;
            }

            float baseStep = fixedTimeStep > 0.0f ? fixedTimeStep : dt;
            float remaining = dt;
            int steps = 0;

            while (remaining > 0.0f && steps < MAX_SUBSTEPS)
            {
                float curDt = (fixedTimeStep > 0.0f && remaining > baseStep) ? baseStep : remaining;
                stepInternal(curDt);
                remaining -= curDt;
                ++steps;
            }

            stepInProgress = false;
        }

        void stepInternal(float deltaTime)
        {
            currentDeltaTime = deltaTime;

            size_t bodyCount = bodies.size();

            for (size_t i = 0; i < bodyCount; i++)
            {
                RigidBody *b = bodies[i];
                b->previousPosition = b->position;
                if (!b->isStatic && !b->isSleeping && b->mass > 0.0f)
                {
                    b->applyForce(gravity * b->mass);
                }
            }

            for (size_t i = 0; i < bodyCount; i++)
            {
                bodies[i]->update(deltaTime);
            }

            contactConstraints.clear();

            for (size_t i = 0; i < bodyCount; i++)
            {
                RigidBody *a = bodies[i];
                for (size_t j = i + 1; j < bodyCount; j++)
                {
                    RigidBody *b = bodies[j];

                    bool aImmobile = (a->isStatic || a->isSleeping || a->isKinematic);
                    bool bImmobile = (b->isStatic || b->isSleeping || b->isKinematic);
                    if (aImmobile && bImmobile)
                        continue;

                    CollisionInfo info = detectCollision(a, b);
                    if (info.hasCollision && info.contactCount > 0)
                    {
                        if (a->isSleeping || b->isSleeping)
                        {
                            Vector3 vRel = b->velocity - a->velocity;
                            float vRelSq = vRel.lengthSquared();
                            const float wakeThresholdSq = 1e-4f;
                            if (vRelSq > wakeThresholdSq)
                            {
                                if (a->isSleeping)
                                    a->wakeUp();
                                if (b->isSleeping)
                                    b->wakeUp();
                            }
                        }

                        preStepConstraint(info, deltaTime);
                        contactConstraints.push_back(info);
                    }
                }
            }

            preStepJoints(deltaTime);

            warmStartConstraints();

            for (int iter = 0; iter < SOLVER_ITERATIONS; iter++)
            {
                size_t contactCount = contactConstraints.size();
                for (size_t c = 0; c < contactCount; c++)
                {
                    resolveCollision(contactConstraints[c]);
                }

                solveJoints(deltaTime);
            }

            positionalCorrection();

            previousContactConstraints = contactConstraints;

            const float sleepLinThresholdSq = 1e-4f;
            const float sleepAngThresholdSq = 1e-4f;
            const float sleepTime = 0.5f;

            for (size_t i = 0; i < bodyCount; i++)
            {
                RigidBody *b = bodies[i];
                if (b->isStatic || b->shape != BODY_SHAPE_BOX)
                    continue;
                float v2 = b->velocity.lengthSquared();
                if (v2 > 1e-3f)
                    continue;
                float w2 = b->angularVelocity.lengthSquared();
                if (w2 > 1e-3f)
                    continue;
                if (!b->canSleep)
                    continue;
                if (v2 < sleepLinThresholdSq && w2 < sleepAngThresholdSq)
                {
                    b->sleepTimer += deltaTime;
                    if (b->sleepTimer > sleepTime)
                    {
                        b->isSleeping = true;
                        b->velocity = Vector3(0, 0, 0);
                        b->angularVelocity = Vector3(0, 0, 0);
                        b->acceleration = Vector3(0, 0, 0);
                    }
                }
                else
                {
                    b->sleepTimer = 0.0f;
                    b->isSleeping = false;
                }

                float halfY = b->size.y * 0.5f;
                float targetY = halfY;
                float dy = b->position.y - targetY;
                if (fabsf(dy) < 0.1f)
                {
                    Vector3 up = b->orientation.rotate(Vector3(0, 1, 0));
                    if (up.y > 0.995f)
                    {
                        b->position.y = targetY;
                        b->orientation = Quaternion();
                        b->angularVelocity = Vector3(0, 0, 0);
                        b->updateBoundsFromTransform();
                    }
                }
            }
        }

        void preStepConstraint(CollisionInfo &info, float deltaTime);

        void warmStartConstraints();

        void positionalCorrection();

        void preStepJoints(float deltaTime)
        {
            size_t count = constraints.size();
            for (size_t i = 0; i < count; ++i)
            {
                Constraint *c = constraints[i];
                if (c)
                {
                    c->preStep(deltaTime);
                }
            }
        }

        void solveJoints(float deltaTime)
        {
            size_t count = constraints.size();
            for (size_t i = 0; i < count; ++i)
            {
                Constraint *c = constraints[i];
                if (c)
                {
                    c->solve(deltaTime);
                }
            }
        }

        bool raycast(const Ray &ray, RaycastHit &outHit, float maxDistance = FLT_MAX)
        {
            outHit.hit = false;
            outHit.body = nullptr;
            outHit.distance = maxDistance;

            size_t bodyCount = bodies.size();
            for (size_t i = 0; i < bodyCount; ++i)
            {
                RigidBody *b = bodies[i];
                if (!b)
                    continue;

                float tMinAABB, tMaxAABB;
                if (!ray.intersects(b->bounds, tMinAABB, tMaxAABB))
                    continue;

                if (tMaxAABB < 0.0f)
                    continue;

                float bestT = FLT_MAX;
                Vector3 hitNormal(0.0f, 1.0f, 0.0f);
                bool hitFound = false;

                if (b->shape == BODY_SHAPE_SPHERE)
                {
                    CollisionSphere s(b->position, b->radius);
                    float tSphere;
                    if (ray.intersects(s, tSphere) && tSphere >= 0.0f && tSphere <= maxDistance)
                    {
                        bestT = tSphere;
                        Vector3 hitPoint = ray.at(tSphere);
                        Vector3 n = hitPoint - b->position;
                        float nLenSq = n.lengthSquared();
                        if (nLenSq > 1e-8f)
                        {
                            float invLen = FastMath::fastInvSqrt(nLenSq);
                            n *= invLen;
                        }
                        else
                        {
                            n = Vector3(0.0f, 1.0f, 0.0f);
                        }
                        hitNormal = n;
                        hitFound = true;
                    }
                }
                else
                {
                    Vector3 half = b->size * 0.5f;

                    Quaternion invRot = b->orientation.conjugate();
                    Vector3 localOrigin = invRot.rotate(ray.origin - b->position);
                    Vector3 localDir = invRot.rotate(ray.direction);

                    Ray localRay(localOrigin, localDir);
                    AABB localBox(Vector3(-half.x, -half.y, -half.z), Vector3(half.x, half.y, half.z));

                    float tMin, tMax;
                    if (localRay.intersects(localBox, tMin, tMax))
                    {
                        float tHit = tMin >= 0.0f ? tMin : tMax;
                        if (tHit >= 0.0f && tHit <= maxDistance)
                        {
                            bestT = tHit;

                            Vector3 localHit = localRay.at(tHit);
                            float dx = half.x - fabsf(localHit.x);
                            float dy = half.y - fabsf(localHit.y);
                            float dz = half.z - fabsf(localHit.z);

                            Vector3 localN(0.0f, 1.0f, 0.0f);
                            if (dx <= dy && dx <= dz)
                            {
                                localN = Vector3((localHit.x > 0.0f) ? 1.0f : -1.0f, 0.0f, 0.0f);
                            }
                            else if (dy <= dz)
                            {
                                localN = Vector3(0.0f, (localHit.y > 0.0f) ? 1.0f : -1.0f, 0.0f);
                            }
                            else
                            {
                                localN = Vector3(0.0f, 0.0f, (localHit.z > 0.0f) ? 1.0f : -1.0f);
                            }

                            hitNormal = b->orientation.rotate(localN);
                            hitFound = true;
                        }
                    }
                }

                if (!hitFound)
                    continue;

                if (bestT < outHit.distance)
                {
                    outHit.hit = true;
                    outHit.body = b;
                    outHit.distance = bestT;
                    outHit.point = ray.at(bestT);
                    outHit.normal = hitNormal;
                }
            }

            return outHit.hit;
        }

        CollisionInfo detectCollision(RigidBody *a, RigidBody *b)
        {
            CollisionInfo info;

            if (!a || !b)
            {
                return info;
            }

            bool bothStatic = a->isStatic && b->isStatic;
            bool bothKinematicNonTrigger = a->isKinematic && b->isKinematic && !a->isTrigger && !b->isTrigger;
            if (bothStatic || bothKinematicNonTrigger)
            {
                return info;
            }

            if (!a->bounds.intersects(b->bounds))
            {
                return info;
            }

            if (a->shape == BODY_SHAPE_SPHERE && b->shape == BODY_SHAPE_SPHERE)
            {
                Vector3 centerA = a->position;
                Vector3 centerB = b->position;
                Vector3 delta = centerB - centerA;
                float distSq = delta.lengthSquared();
                float radiusSum = a->radius + b->radius;
                if (distSq > radiusSum * radiusSum)
                {
                    if (currentDeltaTime > 0.0f)
                    {
                        Vector3 startA = a->previousPosition;
                        Vector3 endA = a->position;
                        Vector3 startB = b->previousPosition;
                        Vector3 endB = b->position;
                        Vector3 relStart = startA - startB;
                        Vector3 relEnd = endA - endB;
                        Vector3 relDir = relEnd - relStart;
                        float relLenSq = relDir.lengthSquared();
                        if (relLenSq > 1e-8f)
                        {
                            Ray ray(relStart, relDir);
                            CollisionSphere expanded(Vector3(0, 0, 0), radiusSum);
                            float t;
                            if (ray.intersects(expanded, t) && t >= 0.0f && t <= 1.0f)
                            {
                                Vector3 posA = startA + (endA - startA) * t;
                                Vector3 posB = startB + (endB - startB) * t;
                                Vector3 hitDelta = posB - posA;
                                float distHitSq = hitDelta.lengthSquared();
                                float distHit = distHitSq > 1e-8f ? sqrtf(distHitSq) : 0.0f;
                                Vector3 normal;
                                if (distHit > 1e-4f)
                                {
                                    normal = hitDelta * (1.0f / distHit);
                                }
                                else
                                {
                                    normal = Vector3(0, 1, 0);
                                }
                                float penetration = radiusSum - distHit;
                                if (penetration < 0.0f)
                                    penetration = 0.0f;
                                Vector3 contact = posA + normal * (a->radius - penetration * 0.5f);

                                info.hasCollision = true;
                                info.bodyA = a;
                                info.bodyB = b;
                                info.normal = normal;
                                info.contactCount = 1;
                                info.contacts[0].pos = contact;
                                info.contacts[0].penetration = penetration;
                                info.contacts[0].accumulatedImpulse = 0.0f;
                                return info;
                            }
                        }
                    }
                    return info;
                }
                float dist = distSq > 1e-8f ? sqrtf(distSq) : 0.0f;
                Vector3 normal;
                if (dist > 1e-4f)
                {
                    normal = delta * (1.0f / dist);
                }
                else
                {
                    normal = Vector3(0, 1, 0);
                }
                float penetration = radiusSum - dist;
                Vector3 contact = centerA + normal * (a->radius - penetration * 0.5f);
                info.hasCollision = true;
                info.bodyA = a;
                info.bodyB = b;
                info.normal = normal;
                info.contactCount = 1;
                info.contacts[0].pos = contact;
                info.contacts[0].penetration = penetration;
                info.contacts[0].accumulatedImpulse = 0.0f;
                return info;
            }

            if (a->shape == BODY_SHAPE_SPHERE && b->shape == BODY_SHAPE_BOX)
            {
                Vector3 sphereCenter = a->position;
                Vector3 boxCenter = b->position;
                Vector3 halfExtents = b->size * 0.5f;

                Quaternion invRot = b->orientation.conjugate();
                Vector3 local = invRot.rotate(sphereCenter - boxCenter);

                float lx = fmaxf(-halfExtents.x, fminf(local.x, halfExtents.x));
                float ly = fmaxf(-halfExtents.y, fminf(local.y, halfExtents.y));
                float lz = fmaxf(-halfExtents.z, fminf(local.z, halfExtents.z));
                Vector3 closestLocal(lx, ly, lz);
                Vector3 closestWorld = b->orientation.rotate(closestLocal) + boxCenter;

                Vector3 diff = closestWorld - sphereCenter;
                float distSq = diff.lengthSquared();
                float r = a->radius;
                if (distSq > r * r)
                {
                    if (currentDeltaTime > 0.0f)
                    {
                        Vector3 start = a->previousPosition;
                        Vector3 end = a->position;
                        Vector3 dir = end - start;
                        float lenSq = dir.lengthSquared();
                        if (lenSq > 1e-8f)
                        {
                            AABB expanded = b->bounds;
                            expanded.min.x -= r;
                            expanded.min.y -= r;
                            expanded.min.z -= r;
                            expanded.max.x += r;
                            expanded.max.y += r;
                            expanded.max.z += r;

                            Ray ray(start, dir);
                            float tMin, tMax;
                            if (ray.intersects(expanded, tMin, tMax) && tMax >= 0.0f && tMin <= 1.0f)
                            {
                                float tHit = tMin;
                                if (tHit < 0.0f)
                                    tHit = 0.0f;
                                if (tHit > 1.0f)
                                    tHit = 1.0f;

                                Vector3 centerHit = start + dir * tHit;
                                Vector3 boxMin = b->bounds.min;
                                Vector3 boxMax = b->bounds.max;
                                float hx = fmaxf(boxMin.x, fminf(centerHit.x, boxMax.x));
                                float hy = fmaxf(boxMin.y, fminf(centerHit.y, boxMax.y));
                                float hz = fmaxf(boxMin.z, fminf(centerHit.z, boxMax.z));
                                Vector3 closestHit(hx, hy, hz);
                                Vector3 diffHit = closestHit - centerHit;
                                float distHitSq = diffHit.lengthSquared();
                                float distHit = distHitSq > 1e-8f ? sqrtf(distHitSq) : 0.0f;

                                Vector3 normal;
                                float penetration;
                                if (distHit > 1e-4f)
                                {
                                    normal = diffHit * (1.0f / distHit);
                                    penetration = r - distHit;
                                }
                                else
                                {
                                    normal = Vector3(0, -1, 0);
                                    penetration = r;
                                }

                                if (penetration < 0.0f)
                                    penetration = 0.0f;

                                info.hasCollision = true;
                                info.bodyA = a;
                                info.bodyB = b;
                                info.normal = normal;
                                info.contactCount = 1;
                                info.contacts[0].pos = closestHit;
                                info.contacts[0].penetration = penetration;
                                info.contacts[0].accumulatedImpulse = 0.0f;
                                return info;
                            }
                        }
                    }

                    return info;
                }

                float dist = distSq > 1e-8f ? sqrtf(distSq) : 0.0f;
                Vector3 normal;
                float penetration;
                if (dist > 1e-4f)
                {
                    normal = diff * (1.0f / dist);
                    penetration = r - dist;
                }
                else
                {
                    float dx = halfExtents.x - fabsf(local.x);
                    float dy = halfExtents.y - fabsf(local.y);
                    float dz = halfExtents.z - fabsf(local.z);

                    Vector3 localN(0, -1, 0);
                    if (dx < dy && dx < dz)
                    {
                        localN = Vector3((local.x > 0.0f) ? 1.0f : -1.0f, 0.0f, 0.0f);
                    }
                    else if (dy < dz)
                    {
                        localN = Vector3(0.0f, (local.y > 0.0f) ? 1.0f : -1.0f, 0.0f);
                    }
                    else
                    {
                        localN = Vector3(0.0f, 0.0f, (local.z > 0.0f) ? 1.0f : -1.0f);
                    }
                    normal = b->orientation.rotate(localN);
                    penetration = r;
                }

                info.hasCollision = true;
                info.bodyA = a;
                info.bodyB = b;
                info.normal = normal;
                info.contactCount = 1;
                info.contacts[0].pos = closestWorld;
                info.contacts[0].penetration = penetration;
                info.contacts[0].accumulatedImpulse = 0.0f;
                return info;
            }

            if (a->shape == BODY_SHAPE_BOX && b->shape == BODY_SHAPE_SPHERE)
            {
                CollisionInfo swapped = detectCollision(b, a);
                if (!swapped.hasCollision)
                {
                    return info;
                }
                info.hasCollision = true;
                info.bodyA = a;
                info.bodyB = b;
                info.normal = swapped.normal * -1.0f;
                info.contactCount = swapped.contactCount;
                for (int i = 0; i < swapped.contactCount && i < 4; ++i)
                {
                    info.contacts[i].pos = swapped.contacts[i].pos;
                    info.contacts[i].penetration = swapped.contacts[i].penetration;
                    info.contacts[i].accumulatedImpulse = 0.0f;
                }
                return info;
            }

            if (a->shape == BODY_SHAPE_BOX && b->shape == BODY_SHAPE_BOX)
            {
                const float eps = 1e-4f;

                Vector3 Ca = a->position;
                Vector3 Cb = b->position;

                Vector3 Aa[3];
                Aa[0] = a->orientation.rotate(Vector3(1, 0, 0));
                Aa[1] = a->orientation.rotate(Vector3(0, 1, 0));
                Aa[2] = a->orientation.rotate(Vector3(0, 0, 1));

                Vector3 Ab[3];
                Ab[0] = b->orientation.rotate(Vector3(1, 0, 0));
                Ab[1] = b->orientation.rotate(Vector3(0, 1, 0));
                Ab[2] = b->orientation.rotate(Vector3(0, 0, 1));

                Vector3 Ea = a->size * 0.5f;
                Vector3 Eb = b->size * 0.5f;

                float R[3][3];
                float AbsR[3][3];
                for (int i = 0; i < 3; ++i)
                {
                    for (int j = 0; j < 3; ++j)
                    {
                        float v = Aa[i].dot(Ab[j]);
                        R[i][j] = v;
                        AbsR[i][j] = fabsf(v) + eps;
                    }
                }

                Vector3 tWorld = Cb - Ca;
                float t[3];
                t[0] = tWorld.dot(Aa[0]);
                t[1] = tWorld.dot(Aa[1]);
                t[2] = tWorld.dot(Aa[2]);

                float minPenetration = FLT_MAX;
                Vector3 bestAxis(0, 1, 0);

                for (int i = 0; i < 3; ++i)
                {
                    float ra = (i == 0 ? Ea.x : (i == 1 ? Ea.y : Ea.z));
                    float rb = Eb.x * AbsR[i][0] + Eb.y * AbsR[i][1] + Eb.z * AbsR[i][2];
                    float dist = fabsf(t[i]);
                    float pen = ra + rb - dist;
                    if (pen < 0.0f)
                        return info;
                    if (pen < minPenetration)
                    {
                        minPenetration = pen;
                        bestAxis = Aa[i] * (t[i] < 0.0f ? -1.0f : 1.0f);
                    }
                }

                for (int j = 0; j < 3; ++j)
                {
                    float ra = Ea.x * AbsR[0][j] + Ea.y * AbsR[1][j] + Ea.z * AbsR[2][j];
                    float rb = (j == 0 ? Eb.x : (j == 1 ? Eb.y : Eb.z));
                    float dist = fabsf(Cb.dot(Ab[j]) - Ca.dot(Ab[j]));
                    float pen = ra + rb - dist;
                    if (pen < 0.0f)
                        return info;
                    if (pen < minPenetration)
                    {
                        minPenetration = pen;
                        float sign = (tWorld.dot(Ab[j]) < 0.0f) ? -1.0f : 1.0f;
                        bestAxis = Ab[j] * sign;
                    }
                }

                for (int i = 0; i < 3; ++i)
                {
                    for (int j = 0; j < 3; ++j)
                    {
                        Vector3 axis = Aa[i].cross(Ab[j]);
                        float axisLenSq = axis.lengthSquared();
                        if (axisLenSq < 1e-8f)
                            continue;
                        float invLen = FastMath::fastInvSqrt(axisLenSq);
                        axis *= invLen;

                        float ra =
                            Ea.x * fabsf(axis.dot(Aa[0])) +
                            Ea.y * fabsf(axis.dot(Aa[1])) +
                            Ea.z * fabsf(axis.dot(Aa[2]));
                        float rb =
                            Eb.x * fabsf(axis.dot(Ab[0])) +
                            Eb.y * fabsf(axis.dot(Ab[1])) +
                            Eb.z * fabsf(axis.dot(Ab[2]));
                        float dist = fabsf(axis.dot(tWorld));
                        float pen = ra + rb - dist;
                        if (pen < 0.0f)
                            return info;
                        if (pen < minPenetration)
                        {
                            minPenetration = pen;
                            bestAxis = axis * (axis.dot(tWorld) < 0.0f ? -1.0f : 1.0f);
                        }
                    }
                }

                if (minPenetration <= 0.0f)
                    return info;

                Vector3 n = bestAxis;

                // Reference face: выбираем грань коробки A, чья ось максимально сонаправлена с нормалью
                int refIndex = 0;
                float maxDotA = Aa[0].dot(n);
                for (int i = 1; i < 3; ++i)
                {
                    float d = Aa[i].dot(n);
                    if (d > maxDotA)
                    {
                        maxDotA = d;
                        refIndex = i;
                    }
                }

                float refExtent = (refIndex == 0 ? Ea.x : (refIndex == 1 ? Ea.y : Ea.z));
                float refSign = (maxDotA >= 0.0f ? 1.0f : -1.0f);
                Vector3 refCenter = Ca + Aa[refIndex] * (refSign * refExtent);
                float planeD = n.dot(refCenter);

                // Incident face: грань коробки B, чья нормаль максимально анти-параллельна нормали коллизии
                int incIndex = 0;
                float minDotB = Ab[0].dot(n);
                for (int j = 1; j < 3; ++j)
                {
                    float d = Ab[j].dot(n);
                    if (d < minDotB)
                    {
                        minDotB = d;
                        incIndex = j;
                    }
                }

                Vector3 incidentLocal[4];
                if (incIndex == 0)
                {
                    float x = (Ab[0].dot(n) < 0.0f) ? Eb.x : -Eb.x;
                    incidentLocal[0] = Vector3(x, -Eb.y, -Eb.z);
                    incidentLocal[1] = Vector3(x, -Eb.y, Eb.z);
                    incidentLocal[2] = Vector3(x, Eb.y, -Eb.z);
                    incidentLocal[3] = Vector3(x, Eb.y, Eb.z);
                }
                else if (incIndex == 1)
                {
                    float y = (Ab[1].dot(n) < 0.0f) ? Eb.y : -Eb.y;
                    incidentLocal[0] = Vector3(-Eb.x, y, -Eb.z);
                    incidentLocal[1] = Vector3(-Eb.x, y, Eb.z);
                    incidentLocal[2] = Vector3(Eb.x, y, -Eb.z);
                    incidentLocal[3] = Vector3(Eb.x, y, Eb.z);
                }
                else
                {
                    float z = (Ab[2].dot(n) < 0.0f) ? Eb.z : -Eb.z;
                    incidentLocal[0] = Vector3(-Eb.x, -Eb.y, z);
                    incidentLocal[1] = Vector3(-Eb.x, Eb.y, z);
                    incidentLocal[2] = Vector3(Eb.x, -Eb.y, z);
                    incidentLocal[3] = Vector3(Eb.x, Eb.y, z);
                }

                Vector3 incidentWorld[4];
                for (int i = 0; i < 4; ++i)
                {
                    incidentWorld[i] = b->orientation.rotate(incidentLocal[i]) + Cb;
                }

                info.hasCollision = true;
                info.bodyA = a;
                info.bodyB = b;
                info.normal = n;
                info.contactCount = 0;

                const float contactEps = 1e-3f;
                for (int i = 0; i < 4; ++i)
                {
                    Vector3 p = incidentWorld[i];
                    float dist = n.dot(p) - planeD;
                    if (dist <= contactEps && info.contactCount < 4)
                    {
                        Contact &c = info.contacts[info.contactCount++];
                        c.pos = p - n * dist; // проекция на плоскость reference face
                        c.penetration = fmaxf(0.0f, -dist);
                        c.accumulatedImpulse = 0.0f;
                    }
                }

                if (info.contactCount == 0)
                {
                    // Фолбэк: одна точка между центрами, как раньше
                    info.contactCount = 1;
                    info.contacts[0].pos = (Ca + Cb) * 0.5f;
                    info.contacts[0].penetration = minPenetration;
                    info.contacts[0].accumulatedImpulse = 0.0f;
                }

                return info;
            }

            return info;
        }

        void resolveCollision(CollisionInfo &info);
    };

}

#include "Solver.h"

#endif
