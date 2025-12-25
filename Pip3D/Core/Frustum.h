#ifndef FRUSTUM_H
#define FRUSTUM_H

#include "Core.h"

namespace pip3D
{

    struct FrustumPlane
    {
        Vector3 n;
        float d;

        FrustumPlane() : n(0, 1, 0), d(0) {}
        FrustumPlane(const Vector3 &normal, float dist) : n(normal), d(dist) {}
        FrustumPlane(const Vector3 &p0, const Vector3 &p1, const Vector3 &p2)
        {
            const Vector3 v1 = p1 - p0, v2 = p2 - p0;
            n = v1.cross(v2);
            const float lenSq = n.lengthSquared();
            if (likely(lenSq > 1e-12f))
            {
                const float invLen = 1.0f / sqrtf(lenSq);
                n *= invLen;
            }
            d = -n.dot(p0);
        }

        __attribute__((always_inline)) inline float distanceToPoint(const Vector3 &p) const
        {
            return n.dot(p) + d;
        }

        __attribute__((always_inline)) inline bool containsSphere(const Vector3 &center, float radius) const
        {
            return distanceToPoint(center) > -radius;
        }

        __attribute__((always_inline)) inline bool containsPoint(const Vector3 &p) const
        {
            return distanceToPoint(p) >= 0;
        }
    };

    enum CullingResult
    {
        CULLED = 0,
        PARTIAL = 1,
        VISIBLE = 2
    };

    class CameraFrustum
    {
    private:
        FrustumPlane planes[6];

    public:
        enum
        {
            NEAR = 0,
            FAR = 1,
            LEFT = 2,
            RIGHT = 3,
            TOP = 4,
            BOTTOM = 5
        };

        void extractFromViewProjection(const Matrix4x4 &vp)
        {
            const float *__restrict__ m = vp.m;

            planes[NEAR].n.x = m[3] + m[2];
            planes[NEAR].n.y = m[7] + m[6];
            planes[NEAR].n.z = m[11] + m[10];
            planes[NEAR].d = m[15] + m[14];

            planes[FAR].n.x = m[3] - m[2];
            planes[FAR].n.y = m[7] - m[6];
            planes[FAR].n.z = m[11] - m[10];
            planes[FAR].d = m[15] - m[14];

            planes[LEFT].n.x = m[3] + m[0];
            planes[LEFT].n.y = m[7] + m[4];
            planes[LEFT].n.z = m[11] + m[8];
            planes[LEFT].d = m[15] + m[12];

            planes[RIGHT].n.x = m[3] - m[0];
            planes[RIGHT].n.y = m[7] - m[4];
            planes[RIGHT].n.z = m[11] - m[8];
            planes[RIGHT].d = m[15] - m[12];

            planes[TOP].n.x = m[3] - m[1];
            planes[TOP].n.y = m[7] - m[5];
            planes[TOP].n.z = m[11] - m[9];
            planes[TOP].d = m[15] - m[13];

            planes[BOTTOM].n.x = m[3] + m[1];
            planes[BOTTOM].n.y = m[7] + m[5];
            planes[BOTTOM].n.z = m[11] + m[9];
            planes[BOTTOM].d = m[15] + m[13];

            for (int i = 0; i < 6; ++i)
            {
                const float lenSq = planes[i].n.lengthSquared();
                if (likely(lenSq > 1e-12f))
                {
                    const float invLen = 1.0f / sqrtf(lenSq);
                    planes[i].n *= invLen;
                    planes[i].d *= invLen;
                }
            }
        }

        __attribute__((always_inline)) inline bool testSphere(const Vector3 &center, float radius) const
        {
            if (unlikely(planes[NEAR].distanceToPoint(center) < -radius))
                return false;
            if (unlikely(planes[FAR].distanceToPoint(center) < -radius))
                return false;
            if (unlikely(planes[LEFT].distanceToPoint(center) < -radius))
                return false;
            if (unlikely(planes[RIGHT].distanceToPoint(center) < -radius))
                return false;
            if (unlikely(planes[TOP].distanceToPoint(center) < -radius))
                return false;
            if (unlikely(planes[BOTTOM].distanceToPoint(center) < -radius))
                return false;
            return true;
        }

        CullingResult testSphereDetailed(const Vector3 &center, float radius) const
        {
            int insideCount = 0;

            for (int i = 0; i < 6; ++i)
            {
                const float dist = planes[i].distanceToPoint(center);

                if (unlikely(dist < -radius))
                    return CULLED;
                if (dist > radius)
                    insideCount++;
            }

            return (insideCount == 6) ? VISIBLE : PARTIAL;
        }

        float getVisibilityFactor(const Vector3 &center, float radius) const
        {
            const CullingResult result = testSphereDetailed(center, radius);
            if (result == CULLED)
                return 0.0f;
            if (result == VISIBLE)
                return 1.0f;

            float minDist = radius;
            for (int i = 0; i < 6; ++i)
            {
                const float dist = planes[i].distanceToPoint(center);
                if (dist < radius)
                {
                    const float d = dist + radius;
                    if (d < minDist)
                        minDist = d;
                }
            }
            return clamp(minDist / radius, 0.0f, 1.0f);
        }

        bool testAABB(const Vector3 &min, const Vector3 &max) const
        {
            for (int i = 0; i < 6; ++i)
            {
                const Vector3 p(
                    planes[i].n.x > 0 ? max.x : min.x,
                    planes[i].n.y > 0 ? max.y : min.y,
                    planes[i].n.z > 0 ? max.z : min.z);
                if (unlikely(planes[i].distanceToPoint(p) < 0))
                    return false;
            }
            return true;
        }

        __attribute__((always_inline)) inline bool testPoint(const Vector3 &p) const
        {
            return planes[NEAR].distanceToPoint(p) >= 0 &&
                   planes[FAR].distanceToPoint(p) >= 0 &&
                   planes[LEFT].distanceToPoint(p) >= 0 &&
                   planes[RIGHT].distanceToPoint(p) >= 0 &&
                   planes[TOP].distanceToPoint(p) >= 0 &&
                   planes[BOTTOM].distanceToPoint(p) >= 0;
        }

        const FrustumPlane &getPlane(int i) const { return planes[i]; }

        void extract(const Matrix4x4 &vp) { extractFromViewProjection(vp); }

        bool sphere(const Vector3 &center, float radius) const { return testSphere(center, radius); }
        bool box(const Vector3 &min, const Vector3 &max) const { return testAABB(min, max); }
        bool point(const Vector3 &p) const { return testPoint(p); }
        CullingResult cull(const Vector3 &center, float radius) const { return testSphereDetailed(center, radius); }
        float factor(const Vector3 &center, float radius) const { return getVisibilityFactor(center, radius); }
    };

    using Frustum = CameraFrustum;

}

#endif
