#ifndef PIP3D_PHYSICS_SOLVER_H
#define PIP3D_PHYSICS_SOLVER_H

namespace pip3D
{

    inline void PhysicsWorld::preStepConstraint(CollisionInfo &info, float deltaTime)
    {
        RigidBody *a = info.bodyA;
        RigidBody *b = info.bodyB;
        if (!a || !b)
            return;

        if (a->isTrigger || b->isTrigger)
            return;

        float invMassA = a->invMass;
        float invMassB = b->invMass;
        float invMassSum = invMassA + invMassB;
        Vector3 n = info.normal;

        CollisionInfo *old = nullptr;
        size_t prevCount = previousContactConstraints.size();
        for (size_t i = 0; i < prevCount; ++i)
        {
            CollisionInfo &prev = previousContactConstraints[i];
            if (prev.bodyA == a && prev.bodyB == b)
            {
                old = &prev;
                break;
            }
        }

        bool used[4] = {false, false, false, false};

        for (int ci = 0; ci < info.contactCount; ++ci)
        {
            Contact &c = info.contacts[ci];
            c.normalMass = 0.0f;
            c.bias = 0.0f;
            c.accumulatedImpulse = 0.0f;

            if (old)
            {
                float bestDistSq = 0.01f * 0.01f;
                int bestIndex = -1;
                int oldContactCount = old->contactCount;
                if (oldContactCount > 4)
                    oldContactCount = 4;
                for (int oi = 0; oi < oldContactCount; ++oi)
                {
                    if (used[oi])
                        continue;
                    Vector3 diff = old->contacts[oi].pos - c.pos;
                    float distSq = diff.lengthSquared();
                    if (distSq < bestDistSq)
                    {
                        bestDistSq = distSq;
                        bestIndex = oi;
                    }
                }
                if (bestIndex >= 0)
                {
                    c.accumulatedImpulse = old->contacts[bestIndex].accumulatedImpulse;
                    used[bestIndex] = true;
                }
            }

            Vector3 rA = c.pos - a->position;
            Vector3 rB = c.pos - b->position;
            Vector3 rAxn = rA.cross(n);
            Vector3 rBxn = rB.cross(n);
            Vector3 invInertiaA(a->invInertia.x * rAxn.x, a->invInertia.y * rAxn.y, a->invInertia.z * rAxn.z);
            Vector3 invInertiaB(b->invInertia.x * rBxn.x, b->invInertia.y * rBxn.y, b->invInertia.z * rBxn.z);
            Vector3 crossA = invInertiaA.cross(rA);
            Vector3 crossB = invInertiaB.cross(rB);
            float angularTerm = crossA.dot(n) + crossB.dot(n);
            float denom = invMassSum + angularTerm;
            if (denom > 0.0f)
            {
                c.normalMass = 1.0f / denom;
            }
            else
            {
                c.normalMass = 0.0f;
            }

            const float baumgarte = 0.2f;
            const float slop = 0.001f;
            float penetration = c.penetration - slop;
            if (penetration < 0.0f)
                penetration = 0.0f;
            if (penetration > 0.0f && deltaTime > 0.0f)
            {
                c.bias = -baumgarte * penetration / deltaTime;
            }
            else
            {
                c.bias = 0.0f;
            }

            Vector3 vA = a->velocity + a->angularVelocity.cross(rA);
            Vector3 vB = b->velocity + b->angularVelocity.cross(rB);
            Vector3 relativeVelocity = vB - vA;
            float vn = relativeVelocity.dot(n);
            float restitution = fminf(a->restitution, b->restitution);
            const float restitutionThreshold = 1.0f;
            if (vn < -restitutionThreshold)
            {
                c.bias += -restitution * vn;
            }
        }
    }

    inline void PhysicsWorld::warmStartConstraints()
    {
        size_t count = contactConstraints.size();
        for (size_t ci = 0; ci < count; ++ci)
        {
            CollisionInfo &info = contactConstraints[ci];
            RigidBody *a = info.bodyA;
            RigidBody *b = info.bodyB;
            if (!a || !b)
                continue;
            if (a->isTrigger || b->isTrigger)
                continue;
            if (a->isStatic && b->isStatic)
                continue;

            float invMassA = a->invMass;
            float invMassB = b->invMass;
            Vector3 n = info.normal;

            for (int j = 0; j < info.contactCount; ++j)
            {
                Contact &c = info.contacts[j];
                float jn = c.accumulatedImpulse;
                if (jn == 0.0f)
                    continue;

                Vector3 impulse = n * jn;
                Vector3 rA = c.pos - a->position;
                Vector3 rB = c.pos - b->position;

                if (!a->isStatic && invMassA > 0.0f)
                {
                    a->velocity -= impulse * invMassA;
                    Vector3 angImpulse = rA.cross(impulse);
                    a->angularVelocity.x -= angImpulse.x * a->invInertia.x;
                    a->angularVelocity.y -= angImpulse.y * a->invInertia.y;
                    a->angularVelocity.z -= angImpulse.z * a->invInertia.z;
                }
                if (!b->isStatic && invMassB > 0.0f)
                {
                    b->velocity += impulse * invMassB;
                    Vector3 angImpulse = rB.cross(impulse);
                    b->angularVelocity.x += angImpulse.x * b->invInertia.x;
                    b->angularVelocity.y += angImpulse.y * b->invInertia.y;
                    b->angularVelocity.z += angImpulse.z * b->invInertia.z;
                }
            }
        }
    }

    inline void PhysicsWorld::positionalCorrection()
    {
        const float percent = 0.4f;
        const float slop = 0.001f;

        size_t count = contactConstraints.size();
        for (size_t i = 0; i < count; ++i)
        {
            CollisionInfo &info = contactConstraints[i];
            RigidBody *a = info.bodyA;
            RigidBody *b = info.bodyB;
            if (!a || !b)
                continue;
            if (a->isTrigger || b->isTrigger)
                continue;
            if (a->isStatic && b->isStatic)
                continue;

            float invMassA = a->invMass;
            float invMassB = b->invMass;
            float invMassSum = invMassA + invMassB;
            if (invMassSum <= 0.0f)
                continue;

            float maxPenetration = 0.0f;
            for (int ci = 0; ci < info.contactCount; ++ci)
            {
                float p = info.contacts[ci].penetration;
                if (p > maxPenetration)
                    maxPenetration = p;
            }

            float correctionMag = maxPenetration - slop;
            if (correctionMag < 0.0f)
                correctionMag = 0.0f;
            correctionMag *= percent;
            if (correctionMag > 0.0f)
            {
                Vector3 correction = info.normal * correctionMag;
                float aFactor = a->isStatic ? 0.0f : invMassA / invMassSum;
                float bFactor = b->isStatic ? 0.0f : invMassB / invMassSum;
                if (!a->isStatic)
                {
                    a->position -= correction * aFactor;
                    a->updateBoundsFromTransform();
                }
                if (!b->isStatic)
                {
                    b->position += correction * bFactor;
                    b->updateBoundsFromTransform();
                }
            }
        }
    }

    inline void PhysicsWorld::resolveCollision(CollisionInfo &info)
    {
        RigidBody *a = info.bodyA;
        RigidBody *b = info.bodyB;
        if ((a && a->isTrigger) || (b && b->isTrigger))
            return;
        if (a->isStatic && b->isStatic)
            return;
        if (info.contactCount <= 0)
            return;

        float invMassA = a->invMass;
        float invMassB = b->invMass;
        float invMassSum = invMassA + invMassB;
        if (invMassSum <= 0.0f)
            return;

        Vector3 n = info.normal;
        float frictionCoeff = fminf(a->friction, b->friction);

        for (int ci = 0; ci < info.contactCount; ++ci)
        {
            Contact &ct = info.contacts[ci];
            Vector3 contactPoint = ct.pos;

            Vector3 rA = contactPoint - a->position;
            Vector3 rB = contactPoint - b->position;
            Vector3 vA = a->velocity + a->angularVelocity.cross(rA);
            Vector3 vB = b->velocity + b->angularVelocity.cross(rB);
            Vector3 relativeVelocity = vB - vA;
            float velocityAlongNormal = relativeVelocity.dot(n);

            float lambda = -(velocityAlongNormal + ct.bias) * ct.normalMass;

            float oldImpulse = ct.accumulatedImpulse;
            ct.accumulatedImpulse = oldImpulse + lambda;
            if (ct.accumulatedImpulse < 0.0f)
                ct.accumulatedImpulse = 0.0f;
            float deltaImpulse = ct.accumulatedImpulse - oldImpulse;
            if (deltaImpulse == 0.0f)
                continue;

            Vector3 impulse = n * deltaImpulse;

            if (!a->isStatic && invMassA > 0.0f)
            {
                a->velocity -= impulse * invMassA;
                Vector3 angImpulse = rA.cross(impulse);
                a->angularVelocity.x -= angImpulse.x * a->invInertia.x;
                a->angularVelocity.y -= angImpulse.y * a->invInertia.y;
                a->angularVelocity.z -= angImpulse.z * a->invInertia.z;
            }
            if (!b->isStatic && invMassB > 0.0f)
            {
                b->velocity += impulse * invMassB;
                Vector3 angImpulse = rB.cross(impulse);
                b->angularVelocity.x += angImpulse.x * b->invInertia.x;
                b->angularVelocity.y += angImpulse.y * b->invInertia.y;
                b->angularVelocity.z += angImpulse.z * b->invInertia.z;
            }

            vA = a->velocity + a->angularVelocity.cross(rA);
            vB = b->velocity + b->angularVelocity.cross(rB);
            relativeVelocity = vB - vA;
            Vector3 tangent = relativeVelocity - n * relativeVelocity.dot(n);
            float tLenSq = tangent.lengthSquared();
            if (tLenSq > 1e-8f)
            {
                float invTL = FastMath::fastInvSqrt(tLenSq);
                tangent *= invTL;
                Vector3 rAxT = rA.cross(tangent);
                Vector3 rBxT = rB.cross(tangent);
                Vector3 invInertiaAT(a->invInertia.x * rAxT.x, a->invInertia.y * rAxT.y, a->invInertia.z * rAxT.z);
                Vector3 invInertiaBT(b->invInertia.x * rBxT.x, b->invInertia.y * rBxT.y, b->invInertia.z * rBxT.z);
                Vector3 crossAT = invInertiaAT.cross(rA);
                Vector3 crossBT = invInertiaBT.cross(rB);
                float angularTermT = crossAT.dot(tangent) + crossBT.dot(tangent);
                float denomT = invMassSum + angularTermT;
                if (denomT > 0.0f)
                {
                    float jt = -relativeVelocity.dot(tangent) / denomT;
                    float maxFriction = frictionCoeff * ct.accumulatedImpulse;
                    if (jt > maxFriction)
                        jt = maxFriction;
                    if (jt < -maxFriction)
                        jt = -maxFriction;
                    Vector3 frictionImpulse = tangent * jt;
                    if (!a->isStatic && invMassA > 0.0f)
                    {
                        a->velocity -= frictionImpulse * invMassA;
                        Vector3 angF = rA.cross(frictionImpulse);
                        a->angularVelocity.x -= angF.x * a->invInertia.x;
                        a->angularVelocity.y -= angF.y * a->invInertia.y;
                        a->angularVelocity.z -= angF.z * a->invInertia.z;
                    }
                    if (!b->isStatic && invMassB > 0.0f)
                    {
                        b->velocity += frictionImpulse * invMassB;
                        Vector3 angF = rB.cross(frictionImpulse);
                        b->angularVelocity.x += angF.x * b->invInertia.x;
                        b->angularVelocity.y += angF.y * b->invInertia.y;
                        b->angularVelocity.z += angF.z * b->invInertia.z;
                    }
                }
            }
        }
    }

}

#endif
