#ifndef COLLISION_H
#define COLLISION_H

#include "Math.h"
#include <float.h>

namespace pip3D
{

  struct __attribute__((aligned(16))) AABB
  {
    Vector3 min, max;

    AABB() : min(0, 0, 0), max(0, 0, 0) {}
    AABB(const Vector3 &mn, const Vector3 &mx) : min(mn), max(mx) {}

    static AABB fromCenterSize(const Vector3 &center, const Vector3 &size)
    {
      Vector3 halfSize = size * 0.5f;
      return AABB(center - halfSize, center + halfSize);
    }

    __attribute__((always_inline)) inline Vector3 center() const
    {
      return (min + max) * 0.5f;
    }

    __attribute__((always_inline)) inline Vector3 size() const
    {
      return max - min;
    }

    __attribute__((always_inline)) inline bool
    contains(const Vector3 &point) const
    {
      return point.x >= min.x && point.x <= max.x && point.y >= min.y &&
             point.y <= max.y && point.z >= min.z && point.z <= max.z;
    }

    __attribute__((always_inline)) inline bool
    intersects(const AABB &other) const
    {
      return min.x <= other.max.x && max.x >= other.min.x &&
             min.y <= other.max.y && max.y >= other.min.y &&
             min.z <= other.max.z && max.z >= other.min.z;
    }

    __attribute__((always_inline)) inline void expand(const Vector3 &point)
    {
      min.x = fminf(min.x, point.x);
      min.y = fminf(min.y, point.y);
      min.z = fminf(min.z, point.z);
      max.x = fmaxf(max.x, point.x);
      max.y = fmaxf(max.y, point.y);
      max.z = fmaxf(max.z, point.z);
    }

    __attribute__((always_inline)) inline void merge(const AABB &other)
    {
      min.x = fminf(min.x, other.min.x);
      min.y = fminf(min.y, other.min.y);
      min.z = fminf(min.z, other.min.z);
      max.x = fmaxf(max.x, other.max.x);
      max.y = fmaxf(max.y, other.max.y);
      max.z = fmaxf(max.z, other.max.z);
    }
  };

  struct __attribute__((aligned(16))) CollisionSphere
  {
    Vector3 center;
    float radius;

    CollisionSphere() : center(0, 0, 0), radius(0) {}
    CollisionSphere(const Vector3 &c, float r) : center(c), radius(r) {}

    __attribute__((always_inline)) inline bool
    contains(const Vector3 &point) const
    {
      float dx = point.x - center.x;
      float dy = point.y - center.y;
      float dz = point.z - center.z;
      float distSq = dx * dx + dy * dy + dz * dz;
      return distSq <= radius * radius;
    }

    __attribute__((always_inline)) inline bool
    intersects(const CollisionSphere &other) const
    {
      float dx = center.x - other.center.x;
      float dy = center.y - other.center.y;
      float dz = center.z - other.center.z;
      float distSq = dx * dx + dy * dy + dz * dz;
      float radiusSum = radius + other.radius;
      return distSq <= radiusSum * radiusSum;
    }

    __attribute__((always_inline)) inline bool intersects(const AABB &box) const
    {
      float x = fmaxf(box.min.x, fminf(center.x, box.max.x));
      float y = fmaxf(box.min.y, fminf(center.y, box.max.y));
      float z = fmaxf(box.min.z, fminf(center.z, box.max.z));

      float dx = x - center.x;
      float dy = y - center.y;
      float dz = z - center.z;
      float distSq = dx * dx + dy * dy + dz * dz;

      return distSq <= radius * radius;
    }
  };

  struct __attribute__((aligned(16))) Ray
  {
    Vector3 origin;
    Vector3 direction;

    Ray() : origin(0, 0, 0), direction(0, 0, 1) {}
    Ray(const Vector3 &o, const Vector3 &d) : origin(o), direction(d) {}

    __attribute__((always_inline)) inline Vector3 at(float t) const
    {
      return origin + direction * t;
    }

    bool intersects(const AABB &box, float &tMin, float &tMax) const
    {
      tMin = 0.0f;
      tMax = FLT_MAX;
      float t0, t1;

      if (fabsf(direction.x) < 1e-8f)
      {
        if (origin.x < box.min.x || origin.x > box.max.x)
          return false;
      }
      else
      {
        float invDx = 1.0f / direction.x;
        t0 = (box.min.x - origin.x) * invDx;
        t1 = (box.max.x - origin.x) * invDx;
        if (invDx < 0.0f)
        {
          float temp = t0;
          t0 = t1;
          t1 = temp;
        }
        if (t0 > tMin)
          tMin = t0;
        if (t1 < tMax)
          tMax = t1;
        if (tMax < tMin)
          return false;
      }

      if (fabsf(direction.y) < 1e-8f)
      {
        if (origin.y < box.min.y || origin.y > box.max.y)
          return false;
      }
      else
      {
        float invDy = 1.0f / direction.y;
        t0 = (box.min.y - origin.y) * invDy;
        t1 = (box.max.y - origin.y) * invDy;
        if (invDy < 0.0f)
        {
          float temp = t0;
          t0 = t1;
          t1 = temp;
        }
        if (t0 > tMin)
          tMin = t0;
        if (t1 < tMax)
          tMax = t1;
        if (tMax < tMin)
          return false;
      }

      if (fabsf(direction.z) < 1e-8f)
      {
        if (origin.z < box.min.z || origin.z > box.max.z)
          return false;
      }
      else
      {
        float invDz = 1.0f / direction.z;
        t0 = (box.min.z - origin.z) * invDz;
        t1 = (box.max.z - origin.z) * invDz;
        if (invDz < 0.0f)
        {
          float temp = t0;
          t0 = t1;
          t1 = temp;
        }
        if (t0 > tMin)
          tMin = t0;
        if (t1 < tMax)
          tMax = t1;
        if (tMax < tMin)
          return false;
      }

      return true;
    }

    bool intersects(const CollisionSphere &sphere, float &t) const
    {
      Vector3 oc = origin - sphere.center;
      float a = direction.lengthSquared();

      if (a <= 1e-8f)
      {
        float distSq = oc.lengthSquared();
        float radiusSq = sphere.radius * sphere.radius;
        if (distSq <= radiusSq)
        {
          t = 0.0f;
          return true;
        }
        return false;
      }

      float halfB = oc.dot(direction);
      float c = oc.lengthSquared() - sphere.radius * sphere.radius;
      float discriminant = halfB * halfB - a * c;

      if (discriminant < 0.0f)
        return false;

      float sqrtD = sqrtf(discriminant);
      float t0 = (-halfB - sqrtD) / a;
      float t1 = (-halfB + sqrtD) / a;

      if (t0 >= 0.0f)
      {
        t = t0;
        return true;
      }
      if (t1 >= 0.0f)
      {
        t = t1;
        return true;
      }
      return false;
    }
  };

  struct __attribute__((aligned(16))) CollisionPlane
  {
    Vector3 normal;
    float distance;

    CollisionPlane() : normal(0, 1, 0), distance(0) {}

    CollisionPlane(const Vector3 &n, float d)
    {
      normal = n;
      float lenSq = normal.lengthSquared();
      if (lenSq > 1e-8f)
      {
        float invLen = FastMath::fastInvSqrt(lenSq);
        normal *= invLen;
        distance = d * invLen;
      }
      else
      {
        normal = Vector3(0, 1, 0);
        distance = 0.0f;
      }
    }

    CollisionPlane(const Vector3 &n, const Vector3 &point)
    {
      normal = n;
      float lenSq = normal.lengthSquared();
      if (lenSq > 1e-8f)
      {
        float invLen = FastMath::fastInvSqrt(lenSq);
        normal *= invLen;
        distance = normal.dot(point);
      }
      else
      {
        normal = Vector3(0, 1, 0);
        distance = 0.0f;
      }
    }

    __attribute__((always_inline)) inline float
    distanceToPoint(const Vector3 &point) const
    {
      return normal.dot(point) - distance;
    }

    __attribute__((always_inline)) inline bool
    intersects(const CollisionSphere &sphere) const
    {
      float dist = fabsf(distanceToPoint(sphere.center));
      return dist <= sphere.radius;
    }
  };

}

#endif
