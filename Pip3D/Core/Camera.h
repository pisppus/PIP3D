#ifndef CAMERA_H
#define CAMERA_H

#include "../Math/Math.h"
#include <cmath>

#ifndef likely
#define likely(x) __builtin_expect(!!(x), 1)
#endif
#ifndef unlikely
#define unlikely(x) __builtin_expect(!!(x), 0)
#endif

namespace pip3D
{

  enum ProjectionType
  {
    PERSPECTIVE,
    ORTHOGRAPHIC,
    FISHEYE
  };

  struct CameraConfig
  {
    float aspectEps;

    CameraConfig() : aspectEps(1e-6f) {}
  };

  struct CameraAnimation
  {
    Vector3 startPos, startTgt, startUp, targetPos, targetTgt, targetUp;
    float startFov, targetFov, time, duration, invDuration;
    enum Type : uint8_t
    {
      LINEAR,
      SMOOTH,
      EASE
    } type;
    bool active : 1;

    CameraAnimation() : time(0), duration(1), invDuration(1.0f), type(SMOOTH), active(false) {}
  };

  struct Camera
  {
    Vector3 position;
    Vector3 target;
    Vector3 up;

    ProjectionType projectionType;

    float fov;
    float nearPlane;
    float farPlane;

    float orthoWidth;
    float orthoHeight;

    float fisheyeStrength;

    CameraConfig config;
    mutable CameraAnimation anim;

    struct Cache
    {
      Matrix4x4 view, proj, viewProj;
      Vector3 cachedForward, cachedRight;
      float lastAspect, halfW, halfH;

      struct
      {
        bool viewDirty : 1;
        bool projDirty : 1;
        bool vpDirty : 1;
        bool orthoDirty : 1;
        bool vectorsDirty : 1;
      } flags;

      Cache() : lastAspect(0), halfW(0), halfH(0)
      {
        flags = {true, true, true, true, true};
      }
    };
    mutable Cache cache;

    Camera(const Vector3 &pos = Vector3(0, 0, -5),
           const Vector3 &tgt = Vector3(0, 0, 0),
           const Vector3 &upVec = Vector3(0, 1, 0))
        : position(pos), target(tgt), up(upVec), projectionType(PERSPECTIVE),
          fov(60), nearPlane(0.1f), farPlane(100), orthoWidth(10),
          orthoHeight(10), fisheyeStrength(0)
    {
      up.normalize();
    }

    const Matrix4x4 &getViewMatrix() const
    {
      if (unlikely(cache.flags.viewDirty))
      {
        cache.view.lookAt(position, target, up);
        cache.flags.viewDirty = false;
        cache.flags.vpDirty = true;
      }
      return cache.view;
    }

    const Matrix4x4 &getProjectionMatrix(float aspect) const
    {
      if (unlikely(cache.flags.projDirty))
      {
        updateProjectionMatrix(aspect);
      }
      else
      {
        const float absAspectDiff = fabsf(aspect - cache.lastAspect);
        if (unlikely(absAspectDiff > config.aspectEps))
        {
          updateProjectionMatrix(aspect);
        }
      }
      return cache.proj;
    }

    void markDirty() { setAllDirty(); }

  private:
    __attribute__((always_inline)) inline void setAllDirty() const
    {
      cache.flags = {true, true, true, true, true};
    }

    __attribute__((always_inline)) inline void invalidateView() const
    {
      cache.flags.viewDirty = true;
      cache.flags.vpDirty = true;
      cache.flags.vectorsDirty = true;
    }

    __attribute__((always_inline)) inline void invalidateProjection() const
    {
      cache.flags.projDirty = true;
      cache.flags.vpDirty = true;
    }

    __attribute__((always_inline)) inline void invalidateOrtho() const
    {
      cache.flags.orthoDirty = true;
      invalidateProjection();
    }

    void updateProjectionMatrix(float aspect) const
    {
      if (likely(projectionType == PERSPECTIVE))
      {
        cache.proj.setPerspective(fov, aspect, nearPlane, farPlane);
      }
      else if (projectionType == FISHEYE)
      {
        setFisheyeProjection(aspect);
      }
      else
      {
        if (cache.flags.orthoDirty)
        {
          cache.halfW = orthoWidth * 0.5f;
          cache.halfH = orthoHeight * 0.5f;
          cache.flags.orthoDirty = false;
        }
        const float aspectFactor = fmaxf(1.0f, aspect);
        const float adjW = cache.halfW * aspectFactor;
        const float adjH = cache.halfH / aspectFactor;
        cache.proj.setOrthographic(-adjW, adjW, -adjH, adjH, nearPlane, farPlane);
      }
      cache.flags.projDirty = false;
      cache.lastAspect = aspect;
      cache.flags.vpDirty = true;
    }

  public:
    void setPerspective(float fovDegrees = 60, float near = 0.1f,
                        float far = 100)
    {
      fov = fmaxf(1, fminf(179, fovDegrees));
      nearPlane = fmaxf(0.001f, near);
      farPlane = fmaxf(nearPlane + 0.1f, far);
      projectionType = PERSPECTIVE;
      fisheyeStrength = 0;
      invalidateProjection();
    }

    void setOrtho(float width = 10, float height = 10, float near = 0.1f,
                  float far = 100)
    {
      orthoWidth = fmaxf(0.1f, width);
      orthoHeight = fmaxf(0.1f, height);
      nearPlane = fmaxf(0.001f, near);
      farPlane = fmaxf(nearPlane + 0.1f, far);
      projectionType = ORTHOGRAPHIC;
      fisheyeStrength = 0;
      invalidateOrtho();
    }

    void setFisheye(float fovDegrees = 120, float strength = 1, float near = 0.1f,
                    float far = 100)
    {
      fov = fmaxf(10, fminf(359, fovDegrees));
      fisheyeStrength = fmaxf(0, fminf(1, strength));
      nearPlane = fmaxf(0.001f, near);
      farPlane = fmaxf(nearPlane + 0.1f, far);
      projectionType = FISHEYE;
      invalidateProjection();
    }

  private:
    void updateVectors() const
    {
      if (unlikely(cache.flags.vectorsDirty))
      {
        cache.cachedForward = target - position;
        cache.cachedForward.normalize();
        cache.cachedRight = cache.cachedForward.cross(up);
        cache.cachedRight.normalize();
        cache.flags.vectorsDirty = false;
      }
    }

  public:
    __attribute__((always_inline)) inline const Vector3 &forward() const
    {
      if (unlikely(cache.flags.vectorsDirty))
        updateVectors();
      return cache.cachedForward;
    }
    __attribute__((always_inline)) inline const Vector3 &right() const
    {
      if (unlikely(cache.flags.vectorsDirty))
        updateVectors();
      return cache.cachedRight;
    }
    const Vector3 &upVec() const { return up; }

    void move(float forwardAmount, float rightAmount, float upAmount)
    {
      if (forwardAmount == 0.0f && rightAmount == 0.0f && upAmount == 0.0f)
        return;

      Vector3 delta;
      if (forwardAmount != 0.0f || rightAmount != 0.0f)
      {
        if (unlikely(cache.flags.vectorsDirty))
          updateVectors();
        delta = cache.cachedForward * forwardAmount + cache.cachedRight * rightAmount;
        if (upAmount != 0.0f)
          delta += up * upAmount;
      }
      else
      {
        delta = up * upAmount;
      }

      position += delta;
      target += delta;
      invalidateView();
    }

    void moveForward(float distance) { move(distance, 0, 0); }
    void moveBackward(float distance) { move(-distance, 0, 0); }
    void moveRight(float distance) { move(0, distance, 0); }
    void moveLeft(float distance) { move(0, -distance, 0); }
    void moveUp(float distance) { move(0, 0, distance); }
    void moveDown(float distance) { move(0, 0, -distance); }

    void rotate(float yaw, float pitch, bool degrees = true)
    {
      if (degrees)
      {
        rotateDeg(yaw, pitch);
      }
      else
      {
        rotateRad(yaw, pitch);
      }
    }

    void rotateDeg(float yawDegrees, float pitchDegrees)
    {
      rotateRad(yawDegrees * DEG2RAD, pitchDegrees * DEG2RAD);
    }

    void rotateRad(float yawRad, float pitchRad)
    {
      if (unlikely(cache.flags.vectorsDirty))
        updateVectors();

      const Vector3 &fwd = cache.cachedForward;
      const float cy = cosf(yawRad), sy = sinf(yawRad);
      const float cp = cosf(pitchRad), sp = sinf(pitchRad);

      Vector3 newFwd(fwd.x * cy - fwd.z * sy, fwd.y, fwd.x * sy + fwd.z * cy);
      Vector3 finalFwd(newFwd.x, newFwd.y * cp - newFwd.z * sp,
                       newFwd.y * sp + newFwd.z * cp);
      finalFwd.normalize();

      const float dist = (target - position).length();
      target = position + finalFwd * dist;
      invalidateView();
    }

    void lookAt(const Vector3 &newTarget)
    {
      target = newTarget;
      invalidateView();
    }

    void lookAt(const Vector3 &newTarget, const Vector3 &newUp)
    {
      target = newTarget;
      up = newUp;
      up.normalize();
      invalidateView();
    }

    void orbit(const Vector3 &center, float radius, float azimuth,
               float elevation, bool degrees = true)
    {
      const float az = degrees ? azimuth * DEG2RAD : azimuth;
      const float el = degrees ? elevation * DEG2RAD : elevation;
      const float cosEl = cosf(el);

      position = Vector3(center.x + radius * cosEl * cosf(az),
                         center.y + radius * sinf(el),
                         center.z + radius * cosEl * sinf(az));
      target = center;
      invalidateView();
    }

    const Matrix4x4 &getViewProjectionMatrix(float aspect) const
    {
      if (unlikely(cache.flags.vpDirty))
      {
        const Matrix4x4 &view = getViewMatrix();
        const Matrix4x4 &proj = getProjectionMatrix(aspect);
        cache.viewProj = proj * view;
        cache.flags.vpDirty = false;
      }
      return cache.viewProj;
    }

    void animateTo(const Vector3 &newPos, const Vector3 &newTgt, float duration = 1.0f,
                   CameraAnimation::Type type = CameraAnimation::SMOOTH)
    {
      initAnimation(newPos, newTgt, up, fov, duration, type);
    }

    void animatePos(const Vector3 &newPos, float duration = 1.0f)
    {
      animateTo(newPos, target + (newPos - position), duration);
    }

    void animateTarget(const Vector3 &newTgt, float duration = 1.0f)
    {
      animateTo(position, newTgt, duration);
    }

    void animateFOV(float newFov, float duration = 1.0f)
    {
      initAnimation(position, target, up, newFov, duration, CameraAnimation::SMOOTH);
    }

  private:
    void initAnimation(const Vector3 &targetPos, const Vector3 &targetTgt,
                       const Vector3 &targetUp, float targetFov,
                       float duration, CameraAnimation::Type type)
    {
      anim.startPos = position;
      anim.startTgt = target;
      anim.startUp = up;
      anim.startFov = fov;
      anim.targetPos = targetPos;
      anim.targetTgt = targetTgt;
      anim.targetUp = targetUp;
      anim.targetFov = targetFov;
      anim.duration = duration;
      anim.invDuration = (duration > 0.0f) ? (1.0f / duration) : 0.0f;
      anim.time = 0;
      anim.type = type;
      anim.active = true;
    }

  public:
    void updateAnim(float deltaTime)
    {
      if (!anim.active)
        return;

      anim.time += deltaTime;
      float t = (anim.duration > 0.0f) ? fminf(anim.time * anim.invDuration, 1.0f) : 1.0f;
      if (t >= 1.0f)
        anim.active = false;

      float st = t;
      switch (anim.type)
      {
      case CameraAnimation::SMOOTH:
        st = t * t * (3 - 2 * t);
        break;
      case CameraAnimation::EASE:
        st = t < 0.5f ? 2 * t * t : 1 - 2 * (1 - t) * (1 - t);
        break;
      }

      position = anim.startPos + (anim.targetPos - anim.startPos) * st;
      target = anim.startTgt + (anim.targetTgt - anim.startTgt) * st;
      up = anim.startUp + (anim.targetUp - anim.startUp) * st;
      up.normalize();
      fov = anim.startFov + (anim.targetFov - anim.startFov) * st;

      markDirty();
    }

    void stopAnim() { anim.active = false; }
    bool isAnimating() const { return anim.active; }

  private:
    void setFisheyeProjection(float aspect) const
    {
      float fovRad = fov * DEG2RAD;
      float f = 1.0f / tanf(fovRad * 0.5f);

      cache.proj.identity();
      cache.proj.m[0] = f / aspect;
      cache.proj.m[5] = f;
      cache.proj.m[10] = (farPlane + nearPlane) / (nearPlane - farPlane);
      cache.proj.m[11] = -1.0f;
      cache.proj.m[14] = (2.0f * farPlane * nearPlane) / (nearPlane - farPlane);
      cache.proj.m[15] = 0.0f;

      float factor = 1.0f + fisheyeStrength * 0.5f;
      cache.proj.m[0] *= factor;
      cache.proj.m[5] *= factor;
    }

  public:
  };

  class FreeCam : public Camera
  {
  public:
    float rotSpeed = 90.0f, moveSpeed = 5.0f;

    FreeCam(const Vector3 &pos = Vector3(0, 0, -5))
        : Camera(pos, pos + Vector3(0, 0, 1)) {}

    void handleJoystick(float joyX, float joyY, float deltaTime)
    {
      if (fabsf(joyX) > 0.1f || fabsf(joyY) > 0.1f)
      {
        rotate(joyX * rotSpeed * deltaTime, joyY * rotSpeed * deltaTime);
      }
    }

    void handleButtons(bool fwd, bool back, bool left, bool right, bool up,
                       bool down, float deltaTime)
    {
      const float spd = moveSpeed * deltaTime;
      move(fwd ? spd : (back ? -spd : 0), right ? spd : (left ? -spd : 0),
           up ? spd : (down ? -spd : 0));
    }

    void handleDPad(int8_t dirX, int8_t dirY, float deltaTime)
    {
      const float spd = moveSpeed * deltaTime;
      move(dirY * spd, dirX * spd, 0);
    }

    void handleRotateButtons(bool rotLeft, bool rotRight, bool rotUp,
                             bool rotDown, float deltaTime)
    {
      const float rotSpd = rotSpeed * deltaTime;
      if (rotLeft || rotRight)
        rotate((rotRight ? 1 : -1) * rotSpd, 0);
      if (rotUp || rotDown)
        rotate(0, (rotDown ? 1 : -1) * rotSpd);
    }
  };

  class OrbitCam : public Camera
  {
  public:
    Vector3 center = Vector3(0, 0, 0);
    float radius = 10.0f, azimuth = 0, elevation = 0, zoomSpd = 1.0f,
          rotSpd = 90.0f;

    OrbitCam(const Vector3 &c = Vector3(0, 0, 0), float r = 10.0f)
        : center(c), radius(r)
    {
      updatePos();
    }

    void setCenter(const Vector3 &c)
    {
      center = c;
      updatePos();
    }
    void zoom(float delta)
    {
      radius = fmaxf(0.1f, radius + delta * zoomSpd);
      updatePos();
    }

    void handleJoystick(float joyX, float joyY, float deltaTime)
    {
      if (fabsf(joyX) > 0.1f || fabsf(joyY) > 0.1f)
      {
        const float radSpeed = rotSpd * deltaTime * DEG2RAD;
        azimuth += joyX * radSpeed;
        const float halfPi = 90.0f * DEG2RAD;
        elevation = fmaxf(-halfPi + 0.1f,
                          fminf(halfPi - 0.1f,
                                elevation + joyY * radSpeed));
        updatePos();
      }
    }

    void handleButtons(bool zoomIn, bool zoomOut, float deltaTime)
    {
      if (zoomIn)
        zoom(-zoomSpd * deltaTime);
      if (zoomOut)
        zoom(zoomSpd * deltaTime);
    }

  private:
    void updatePos() { orbit(center, radius, azimuth, elevation, false); }
  };

  class CameraBuilder
  {
    Camera cam;

  public:
    CameraBuilder &at(const Vector3 &pos)
    {
      cam.position = pos;
      return *this;
    }
    CameraBuilder &lookAt(const Vector3 &tgt)
    {
      cam.target = tgt;
      return *this;
    }
    CameraBuilder &withUp(const Vector3 &up)
    {
      cam.up = up;
      cam.up.normalize();
      return *this;
    }
    CameraBuilder &persp(float fov = 60, float near = 0.1f, float far = 100)
    {
      cam.setPerspective(fov, near, far);
      return *this;
    }
    CameraBuilder &ortho(float w = 10, float h = 10, float near = 0.1f,
                         float far = 100)
    {
      cam.setOrtho(w, h, near, far);
      return *this;
    }
    CameraBuilder &fisheye(float fov = 120, float str = 1, float near = 0.1f,
                           float far = 100)
    {
      cam.setFisheye(fov, str, near, far);
      return *this;
    }
    CameraBuilder &withConfig(const CameraConfig &cfg)
    {
      cam.config = cfg;
      return *this;
    }
    Camera build()
    {
      cam.markDirty();
      return cam;
    }
  };

}

#endif
