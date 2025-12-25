#ifndef MATH_H
#define MATH_H

#include <math.h>
#include <stdint.h>
#include <Arduino.h>

namespace pip3D
{

    constexpr float DEG2RAD = DEG_TO_RAD;
    constexpr float RAD2DEG = RAD_TO_DEG;
    constexpr float TWO_PI_256 = 256.0f / TWO_PI;
    class FastMath
    {
    private:
        static const float sinTable[256];
        static const float cosTable[256];

        __attribute__((always_inline)) static inline float normalizeAngle(float angle)
        {
            angle = fmodf(angle, TWO_PI);
            return angle < 0 ? angle + TWO_PI : angle;
        }

    public:
        __attribute__((always_inline)) static inline float fastSin(float angle)
        {
            if (angle < 0.0f || angle >= TWO_PI)
                angle = normalizeAngle(angle);
            const int index = static_cast<int>(angle * TWO_PI_256) & 0xFF;
            return sinTable[index];
        }

        __attribute__((always_inline)) static inline float fastCos(float angle)
        {
            if (angle < 0.0f || angle >= TWO_PI)
                angle = normalizeAngle(angle);
            const int index = static_cast<int>(angle * TWO_PI_256) & 0xFF;
            return cosTable[index];
        }

        __attribute__((always_inline)) static inline float fastInvSqrt(float x)
        {
            float xhalf = 0.5f * x;
            union
            {
                float f;
                uint32_t i;
            } conv;
            conv.f = x;
            conv.i = 0x5f3759df - (conv.i >> 1);
            conv.f = conv.f * (1.5f - xhalf * conv.f * conv.f);
            conv.f = conv.f * (1.5f - xhalf * conv.f * conv.f);
            return conv.f;
        }
    };

    struct Vector3
    {
        float x, y, z;

        constexpr Vector3() : x(0), y(0), z(0) {}
        constexpr Vector3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}

        __attribute__((always_inline)) inline Vector3 operator+(const Vector3 &v) const
        {
            return Vector3(x + v.x, y + v.y, z + v.z);
        }

        __attribute__((always_inline)) inline Vector3 operator-(const Vector3 &v) const
        {
            return Vector3(x - v.x, y - v.y, z - v.z);
        }

        __attribute__((always_inline)) inline Vector3 operator*(float s) const
        {
            return Vector3(x * s, y * s, z * s);
        }

        __attribute__((always_inline)) inline Vector3 &operator+=(const Vector3 &v)
        {
            x += v.x;
            y += v.y;
            z += v.z;
            return *this;
        }

        __attribute__((always_inline)) inline Vector3 &operator*=(float s)
        {
            x *= s;
            y *= s;
            z *= s;
            return *this;
        }

        __attribute__((always_inline)) inline Vector3 &operator-=(const Vector3 &v)
        {
            x -= v.x;
            y -= v.y;
            z -= v.z;
            return *this;
        }

        __attribute__((always_inline)) inline void normalize()
        {
            float lenSq = x * x + y * y + z * z;
            if (lenSq > 1e-8f)
            {
                float invLen = FastMath::fastInvSqrt(lenSq);
                x *= invLen;
                y *= invLen;
                z *= invLen;
            }
        }

        __attribute__((always_inline)) inline float length() const
        {
            float lenSq = x * x + y * y + z * z;
            return lenSq < 1e-8f ? 0.0f : sqrtf(lenSq);
        }

        __attribute__((always_inline)) inline float lengthSquared() const
        {
            return x * x + y * y + z * z;
        }

        __attribute__((always_inline)) inline float dot(const Vector3 &v) const
        {
            return x * v.x + y * v.y + z * v.z;
        }

        __attribute__((always_inline)) inline Vector3 cross(const Vector3 &v) const
        {
            return Vector3(
                y * v.z - z * v.y,
                z * v.x - x * v.z,
                x * v.y - y * v.x);
        }
    };

    struct __attribute__((aligned(16))) Matrix4x4
    {
        float m[16];

        Matrix4x4() { identity(); }

        explicit Matrix4x4(bool initializeIdentity)
        {
            if (initializeIdentity)
            {
                identity();
            }
        }

        void identity()
        {
            memset(m, 0, sizeof(m));
            m[0] = m[5] = m[10] = m[15] = 1.0f;
        }

        Matrix4x4 operator*(const Matrix4x4 &b) const
        {
            Matrix4x4 result(false);
            const float *a = m;
            const float *bm = b.m;
            float *r = result.m;

            float a0 = a[0], a1 = a[4], a2 = a[8], a3 = a[12];
            r[0] = a0 * bm[0] + a1 * bm[1] + a2 * bm[2] + a3 * bm[3];
            r[4] = a0 * bm[4] + a1 * bm[5] + a2 * bm[6] + a3 * bm[7];
            r[8] = a0 * bm[8] + a1 * bm[9] + a2 * bm[10] + a3 * bm[11];
            r[12] = a0 * bm[12] + a1 * bm[13] + a2 * bm[14] + a3 * bm[15];

            a0 = a[1];
            a1 = a[5];
            a2 = a[9];
            a3 = a[13];
            r[1] = a0 * bm[0] + a1 * bm[1] + a2 * bm[2] + a3 * bm[3];
            r[5] = a0 * bm[4] + a1 * bm[5] + a2 * bm[6] + a3 * bm[7];
            r[9] = a0 * bm[8] + a1 * bm[9] + a2 * bm[10] + a3 * bm[11];
            r[13] = a0 * bm[12] + a1 * bm[13] + a2 * bm[14] + a3 * bm[15];

            a0 = a[2];
            a1 = a[6];
            a2 = a[10];
            a3 = a[14];
            r[2] = a0 * bm[0] + a1 * bm[1] + a2 * bm[2] + a3 * bm[3];
            r[6] = a0 * bm[4] + a1 * bm[5] + a2 * bm[6] + a3 * bm[7];
            r[10] = a0 * bm[8] + a1 * bm[9] + a2 * bm[10] + a3 * bm[11];
            r[14] = a0 * bm[12] + a1 * bm[13] + a2 * bm[14] + a3 * bm[15];

            a0 = a[3];
            a1 = a[7];
            a2 = a[11];
            a3 = a[15];
            r[3] = a0 * bm[0] + a1 * bm[1] + a2 * bm[2] + a3 * bm[3];
            r[7] = a0 * bm[4] + a1 * bm[5] + a2 * bm[6] + a3 * bm[7];
            r[11] = a0 * bm[8] + a1 * bm[9] + a2 * bm[10] + a3 * bm[11];
            r[15] = a0 * bm[12] + a1 * bm[13] + a2 * bm[14] + a3 * bm[15];

            return result;
        }

        __attribute__((always_inline)) inline Vector3 transform(const Vector3 &v) const
        {
            float w = m[3] * v.x + m[7] * v.y + m[11] * v.z + m[15];
            float invW = 1.0f / w;

            return Vector3(
                (m[0] * v.x + m[4] * v.y + m[8] * v.z + m[12]) * invW,
                (m[1] * v.x + m[5] * v.y + m[9] * v.z + m[13]) * invW,
                (m[2] * v.x + m[6] * v.y + m[10] * v.z + m[14]) * invW);
        }

        __attribute__((always_inline)) inline Vector3 transformNoDiv(const Vector3 &v) const
        {
            return Vector3(
                m[0] * v.x + m[4] * v.y + m[8] * v.z + m[12],
                m[1] * v.x + m[5] * v.y + m[9] * v.z + m[13],
                m[2] * v.x + m[6] * v.y + m[10] * v.z + m[14]);
        }

        __attribute__((always_inline)) inline Vector3 transformNormal(const Vector3 &n) const
        {
            Vector3 result(
                m[0] * n.x + m[4] * n.y + m[8] * n.z,
                m[1] * n.x + m[5] * n.y + m[9] * n.z,
                m[2] * n.x + m[6] * n.y + m[10] * n.z);
            result.normalize();
            return result;
        }

        void setPerspective(float fov, float aspect, float nearPlane, float farPlane)
        {
            float f = 1.0f / tanf(fov * 0.5f * DEG2RAD);
            float rangeInv = 1.0f / (nearPlane - farPlane);

            memset(m, 0, sizeof(m));
            m[0] = f / aspect;
            m[5] = f;
            m[10] = (farPlane + nearPlane) * rangeInv;
            m[11] = -1.0f;
            m[14] = 2.0f * nearPlane * farPlane * rangeInv;
        }

        void setOrthographic(float left, float right, float bottom, float top, float nearPlane, float farPlane)
        {
            float rl = 1.0f / (right - left);
            float tb = 1.0f / (top - bottom);
            float fn = 1.0f / (farPlane - nearPlane);

            memset(m, 0, sizeof(m));
            m[0] = 2.0f * rl;
            m[5] = 2.0f * tb;
            m[10] = -2.0f * fn;
            m[12] = -(right + left) * rl;
            m[13] = -(top + bottom) * tb;
            m[14] = -(farPlane + nearPlane) * fn;
            m[15] = 1.0f;
        }

        void lookAt(const Vector3 &eye, const Vector3 &target, const Vector3 &up)
        {
            Vector3 f = target - eye;
            f.normalize();

            Vector3 s = f.cross(up);
            s.normalize();

            Vector3 u = s.cross(f);

            m[0] = s.x;
            m[4] = s.y;
            m[8] = s.z;
            m[1] = u.x;
            m[5] = u.y;
            m[9] = u.z;
            m[2] = -f.x;
            m[6] = -f.y;
            m[10] = -f.z;
            m[3] = 0;
            m[7] = 0;
            m[11] = 0;

            m[12] = -s.dot(eye);
            m[13] = -u.dot(eye);
            m[14] = f.dot(eye);
            m[15] = 1.0f;
        }

        void setTranslation(float x, float y, float z)
        {
            identity();
            m[12] = x;
            m[13] = y;
            m[14] = z;
        }

        void setRotationX(float angle)
        {
            identity();
            float angleRad = angle * DEG2RAD;
            float c = FastMath::fastCos(angleRad);
            float s = FastMath::fastSin(angleRad);
            m[5] = c;
            m[6] = s;
            m[9] = -s;
            m[10] = c;
        }

        void setRotationY(float angle)
        {
            identity();
            float angleRad = angle * DEG2RAD;
            float c = FastMath::fastCos(angleRad);
            float s = FastMath::fastSin(angleRad);
            m[0] = c;
            m[2] = -s;
            m[8] = s;
            m[10] = c;
        }

        void setRotationZ(float angle)
        {
            identity();
            float angleRad = angle * DEG2RAD;
            float c = FastMath::fastCos(angleRad);
            float s = FastMath::fastSin(angleRad);
            m[0] = c;
            m[1] = s;
            m[4] = -s;
            m[5] = c;
        }

        void setScale(float x, float y, float z)
        {
            identity();
            m[0] = x;
            m[5] = y;
            m[10] = z;
        }
    };

    struct __attribute__((aligned(16))) Quaternion
    {
        float x, y, z, w;

        Quaternion() : x(0), y(0), z(0), w(1) {}
        Quaternion(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}

        static Quaternion fromAxisAngle(const Vector3 &axis, float angle)
        {
            float halfAngle = angle * 0.5f;
            float s = FastMath::fastSin(halfAngle);
            return Quaternion(axis.x * s, axis.y * s, axis.z * s, FastMath::fastCos(halfAngle));
        }

        static Quaternion fromEuler(float pitch, float yaw, float roll)
        {
            float cy = FastMath::fastCos(yaw * 0.5f);
            float sy = FastMath::fastSin(yaw * 0.5f);
            float cp = FastMath::fastCos(pitch * 0.5f);
            float sp = FastMath::fastSin(pitch * 0.5f);
            float cr = FastMath::fastCos(roll * 0.5f);
            float sr = FastMath::fastSin(roll * 0.5f);

            return Quaternion(
                sr * cp * cy - cr * sp * sy,
                cr * sp * cy + sr * cp * sy,
                cr * cp * sy - sr * sp * cy,
                cr * cp * cy + sr * sp * sy);
        }

        __attribute__((always_inline)) inline Quaternion conjugate() const
        {
            return Quaternion(-x, -y, -z, w);
        }

        __attribute__((always_inline)) inline void normalize()
        {
            float lenSq = x * x + y * y + z * z + w * w;
            if (lenSq > 1e-8f)
            {
                float invLen = FastMath::fastInvSqrt(lenSq);
                x *= invLen;
                y *= invLen;
                z *= invLen;
                w *= invLen;
            }
        }

        __attribute__((always_inline)) inline Quaternion operator*(const Quaternion &q) const
        {
            return Quaternion(
                w * q.x + x * q.w + y * q.z - z * q.y,
                w * q.y - x * q.z + y * q.w + z * q.x,
                w * q.z + x * q.y - y * q.x + z * q.w,
                w * q.w - x * q.x - y * q.y - z * q.z);
        }

        __attribute__((always_inline)) inline Vector3 rotate(const Vector3 &v) const
        {
            float qx = x + x, qy = y + y, qz = z + z;
            float qxx = x * qx, qyy = y * qy, qzz = z * qz;
            float qxy = x * qy, qxz = x * qz, qyz = y * qz;
            float qwx = w * qx, qwy = w * qy, qwz = w * qz;

            return Vector3(
                (1.0f - qyy - qzz) * v.x + (qxy - qwz) * v.y + (qxz + qwy) * v.z,
                (qxy + qwz) * v.x + (1.0f - qxx - qzz) * v.y + (qyz - qwx) * v.z,
                (qxz - qwy) * v.x + (qyz + qwx) * v.y + (1.0f - qxx - qyy) * v.z);
        }

        __attribute__((always_inline)) inline void toMatrix(Matrix4x4 &out) const
        {
            float qx = x + x, qy = y + y, qz = z + z;
            float qxx = x * qx, qyy = y * qy, qzz = z * qz;
            float qxy = x * qy, qxz = x * qz, qyz = y * qz;
            float qwx = w * qx, qwy = w * qy, qwz = w * qz;

            out.identity();
            out.m[0] = 1.0f - qyy - qzz;
            out.m[1] = qxy + qwz;
            out.m[2] = qxz - qwy;

            out.m[4] = qxy - qwz;
            out.m[5] = 1.0f - qxx - qzz;
            out.m[6] = qyz + qwx;

            out.m[8] = qxz + qwy;
            out.m[9] = qyz - qwx;
            out.m[10] = 1.0f - qxx - qyy;
        }

        static Quaternion slerp(const Quaternion &a, const Quaternion &b, float t)
        {
            float dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;

            Quaternion b_adjusted = b;
            if (dot < 0.0f)
            {
                b_adjusted = Quaternion(-b.x, -b.y, -b.z, -b.w);
                dot = -dot;
            }

            if (dot > 0.9995f)
            {
                Quaternion result(
                    a.x + t * (b_adjusted.x - a.x),
                    a.y + t * (b_adjusted.y - a.y),
                    a.z + t * (b_adjusted.z - a.z),
                    a.w + t * (b_adjusted.w - a.w));
                result.normalize();
                return result;
            }

            float theta = acosf(dot);
            float invSin = 1.0f / sinf(theta);
            float scale0 = sinf((1.0f - t) * theta) * invSin;
            float scale1 = sinf(t * theta) * invSin;

            return Quaternion(
                scale0 * a.x + scale1 * b_adjusted.x,
                scale0 * a.y + scale1 * b_adjusted.y,
                scale0 * a.z + scale1 * b_adjusted.z,
                scale0 * a.w + scale1 * b_adjusted.w);
        }
    };

}

#endif
