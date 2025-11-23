# PIP3D Engine API

## Camera System

This section describes the camera subsystem defined in `lib/Pip3D/core/pip3D_camera.h`.

The camera is designed for real‑time 3D rendering on ESP32‑S3 without PSRAM and follows industry‑standard concepts similar to Unreal Engine and other modern engines.

---

## Overview

- **World‑space camera**: stores position, target (look‑at point) and up vector in world coordinates.
- **Configurable projection**: perspective, orthographic and fisheye projections.
- **Lazy evaluation**: view/projection matrices and frustum are cached and recomputed only when needed.
- **Built‑in helpers**: animation support, free‑fly camera, orbit camera and builder pattern for convenient setup.

Use this document as a reference when configuring or controlling cameras from your application code.

---

## ProjectionType

```cpp
enum ProjectionType
{
    PERSPECTIVE,
    ORTHOGRAPHIC,
    FISHEYE
};
```

- **PERSPECTIVE**
  - Standard 3D camera with perspective projection.
  - Controlled by `fov`, `nearPlane`, `farPlane` and viewport aspect ratio.
- **ORTHOGRAPHIC**
  - Parallel projection without perspective.
  - Controlled by `orthoWidth`, `orthoHeight`, `nearPlane`, `farPlane`.
  - Useful for isometric views, UI or debug overlays.
- **FISHEYE**
  - Wide‑angle perspective with additional radial distortion.
  - Controlled by `fov` and `fisheyeStrength`.

---

## CameraConfig

```cpp
struct CameraConfig
{
    bool  frustumCulling : 1;
    float aspectEps;
};
```

- **frustumCulling**
  - Enables or disables frustum‑based visibility checks in `Camera`.
  - When `false`, all `is*Visible` queries return visible without testing.
  - Default: `true`.

- **aspectEps**
  - Threshold for aspect‑ratio changes in `getProjectionMatrix(aspect)`.
  - If `|aspect - lastAspect| <= aspectEps`, the cached projection is reused.
  - Default: `1e-6f` (effectively recomputed only when aspect truly changes).

Use `CameraConfig` to tune performance vs. precision for your target display.

---

## CameraAnimation

```cpp
struct CameraAnimation
{
    Vector3 startPos, startTgt, startUp;
    Vector3 targetPos, targetTgt, targetUp;
    float   startFov, targetFov;
    float   time, duration;

    enum Type : uint8_t
    {
        LINEAR,
        SMOOTH,
        EASE
    } type;

    bool active : 1;
};
```

High‑level interpolation state used internally by `Camera` for smooth transitions.

- **Fields**
  - `startPos`, `startTgt`, `startUp` – initial camera state at animation start.
  - `targetPos`, `targetTgt`, `targetUp` – goal state at animation end.
  - `startFov`, `targetFov` – initial and target field of view.
  - `time` – accumulated time since animation start.
  - `duration` – total animation duration in seconds.
  - `type` – easing type:
    - `LINEAR` – constant speed interpolation.
    - `SMOOTH` – smoothstep‑style S‑curve.
    - `EASE` – ease‑in/ease‑out behavior.
  - `active` – flag indicating whether an animation is currently running.

You usually do not manipulate `CameraAnimation` directly. Use `Camera::animate*` APIs instead.

---

## Camera

```cpp
struct Camera
{
    Vector3        position;
    Vector3        target;
    Vector3        up;
    ProjectionType projectionType;

    float fov;
    float nearPlane;
    float farPlane;

    float orthoWidth;
    float orthoHeight;

    float fisheyeStrength;

    CameraConfig   config;
};
```

Core view camera used by the renderer. This is the main type you work with when controlling the view.

### Construction

- **Default constructor**
  - Position: `(0, 0, -5)`
  - Target: `(0, 0, 0)`
  - Up: `(0, 1, 0)` (normalized)
  - Projection: `PERSPECTIVE`
  - FOV: `60` degrees
  - Near/far: `0.1f` / `100.0f`
  - Ortho: `orthoWidth = orthoHeight = 10`
  - Fisheye: `fisheyeStrength = 0`

This gives you a reasonable default camera for typical demos.

### Projection setup

- **setPerspective(float fovDegrees = 60, float near = 0.1f, float far = 100)**
  - Configures a perspective projection.
  - FOV is clamped to `[1, 179]` degrees.
  - `nearPlane` is at least `0.001f`.
  - `farPlane` is guaranteed to be at least `nearPlane + 0.1f`.
  - Sets `projectionType = PERSPECTIVE` and clears fisheye distortion.
  - Marks projection cache as dirty.

- **setOrtho(float width = 10, float height = 10, float near = 0.1f, float far = 100)**
  - Configures an orthographic projection.
  - `orthoWidth`/`orthoHeight` are clamped to at least `0.1f`.
  - `nearPlane`/`farPlane` are clamped similarly to `setPerspective`.
  - Sets `projectionType = ORTHOGRAPHIC` and clears fisheye distortion.
  - Marks orthographic and projection caches as dirty.

- **setFisheye(float fovDegrees = 120, float strength = 1, float near = 0.1f, float far = 100)**
  - Configures a fisheye‑style projection with radial distortion.
  - FOV is clamped to `[10, 359]` degrees.
  - `strength` is clamped to `[0, 1]`.
  - Sets `projectionType = FISHEYE`.
  - Marks projection cache as dirty.

When changing projection or its parameters directly (e.g. writing to `fov`), call `markDirty()` afterwards so cached matrices and frustums are rebuilt.

### View/projection matrices

- **const Matrix4x4& getViewMatrix() const**
  - Returns the camera view matrix (world → view).
  - Rebuilds the matrix lazily from `position`, `target`, `up` when marked dirty.

- **const Matrix4x4& getProjectionMatrix(float aspect) const**
  - Returns the projection matrix using the given aspect ratio.
  - Rebuilds the matrix when projection parameters change or when
    `|aspect - lastAspect| > config.aspectEps`.

- **const Matrix4x4& getViewProjectionMatrix(float aspect) const**
  - Returns the cached `projection * view` matrix.
  - Updated lazily when either view or projection matrix becomes dirty.

### Frustum and visibility queries

- **const Frustum& getFrustum(float aspect) const**
  - Returns the camera frustum derived from the current view‑projection matrix.
  - Uses internal caching and only recomputes when necessary.

- **bool isPointVisible(const Vector3& point, float aspect) const**
  - Returns `true` if frustum culling is disabled, or if `point` lies inside the frustum.

- **bool isSphereVisible(const Vector3& center, float radius, float aspect) const**
  - Returns `true` if a sphere is at least partially within the frustum.

- **bool isAABBVisible(const Vector3& min, const Vector3& max, float aspect) const**
  - Returns `true` if an axis‑aligned bounding box is at least partially within the frustum.

These helpers are convenient for custom culling or debug tools. The main renderer uses its own frustum instance for draw‑calls.

### Orientation vectors

- **const Vector3& forward() const**
  - Returns a normalized forward vector from camera position to target.
  - Lazily computed and cached.

- **const Vector3& right() const**
  - Returns a normalized right vector (perpendicular to forward and up).

- **const Vector3& upVec() const**
  - Returns the current up vector as stored in the camera.

Use these vectors for local‑space movement and custom controls.

### Movement API

- **void move(float forwardAmount, float rightAmount, float upAmount)**
  - Moves the camera along its local forward, right and up axes.
  - Updates both `position` and `target`, preserving camera orientation.
  - Marks the view matrix as dirty.

- **void moveForward(float distance)** / **moveBackward**, **moveRight**, **moveLeft**, **moveUp**, **moveDown**
  - Convenience wrappers around `move` for single‑axis motion.

These methods are appropriate for free‑fly controls and FPS‑like cameras.

### Rotation and look‑at

- **void rotate(float yaw, float pitch, bool degrees = true)**
  - Rotates the camera around its local axes.
  - If `degrees = true`, the inputs are interpreted as degrees.

- **void rotateDeg(float yawDegrees, float pitchDegrees)**
  - Rotates using yaw/pitch specified in degrees.

- **void rotateRad(float yawRad, float pitchRad)**
  - Rotates using yaw/pitch specified in radians.

- **void lookAt(const Vector3& newTarget)**
  - Changes the camera target while keeping position and up vector.

- **void lookAt(const Vector3& newTarget, const Vector3& newUp)**
  - Changes both target and up vector; up is normalized automatically.

- **void orbit(const Vector3& center, float radius, float azimuth, float elevation, bool degrees = true)**
  - Places the camera on a sphere around `center` with given `radius` and angles.
  - Sets `position` to the orbiting point and `target` to `center`.

Use these APIs for third‑person or trackball‑style camera behavior.

### Animation API

- **void animateTo(const Vector3& newPos, const Vector3& newTgt, float duration = 1.0f, CameraAnimation::Type type = CameraAnimation::SMOOTH)**
  - Starts an animation from the current camera state to `newPos` / `newTgt` over `duration` seconds.

- **void animatePos(const Vector3& newPos, float duration = 1.0f)**
  - Animates only the position; target moves by the same offset to preserve viewing direction.

- **void animateTarget(const Vector3& newTgt, float duration = 1.0f)**
  - Animates only the target; position remains fixed.

- **void animateFOV(float newFov, float duration = 1.0f)**
  - Animates the field of view while keeping position and target.

- **void updateAnim(float deltaTime)**
  - Advances the current animation by `deltaTime` seconds.
  - Updates position, target, up and FOV based on the configured easing.
  - Calls `markDirty()` when values change.

- **void stopAnim()**
  - Immediately stops the current animation.

- **bool isAnimating() const**
  - Returns whether an animation is currently active.

Typical usage is to call `updateAnim(deltaTime)` once per frame in your game loop.

### Dirty management

- **void markDirty()**
  - Marks all camera caches (view, projection, combined matrix, frustum, vectors, orthographic extents) as dirty.
  - Call this after changing camera fields directly (e.g. writing to `position`, `fov`, `projectionType`) instead of going through the `set*`/`move*`/`rotate*` APIs.

This ensures that subsequent calls to `getViewMatrix`, `getProjectionMatrix` and `getFrustum` will recompute internal state correctly.

---

## FreeCam

```cpp
class FreeCam : public Camera
{
public:
    float rotSpeed;
    float moveSpeed;
};
```

High‑level helper camera that provides free‑fly controls driven by input axes and buttons.

- **Properties**
  - `rotSpeed` – base rotation speed in degrees per second.
  - `moveSpeed` – base movement speed in world units per second.

- **FreeCam(const Vector3& pos = Vector3(0, 0, -5))**
  - Initializes camera at `pos`, looking forward along +Z.

- **void handleJoystick(float joyX, float joyY, float deltaTime)**
  - Applies rotation based on joystick axes in range `[-1, 1]`.
  - Ignores small deadzone near zero.

- **void handleButtons(bool fwd, bool back, bool left, bool right, bool up, bool down, float deltaTime)**
  - Moves the camera along local axes based on directional buttons.

- **void handleDPad(int8_t dirX, int8_t dirY, float deltaTime)**
  - Moves the camera in a grid‑like fashion using D‑pad direction.

- **void handleRotateButtons(bool rotLeft, bool rotRight, bool rotUp, bool rotDown, float deltaTime)**
  - Applies incremental yaw/pitch rotation using discrete buttons.

Use `FreeCam` when you need an immediate free‑fly camera for debug views or editors.

---

## OrbitCam

```cpp
class OrbitCam : public Camera
{
public:
    Vector3 center;
    float   radius;
    float   azimuth;
    float   elevation;
    float   zoomSpd;
    float   rotSpd;
};
```

Camera that orbits around a focus point at a configurable radius and angles.

- **Properties**
  - `center` – world‑space orbit center (usually the object being inspected).
  - `radius` – distance from camera to `center`.
  - `azimuth` – horizontal angle around the center (radians).
  - `elevation` – vertical angle relative to the horizontal plane (radians).
  - `zoomSpd` – zoom speed factor.
  - `rotSpd` – rotation speed in degrees per second.

- **OrbitCam(const Vector3& c = Vector3(0, 0, 0), float r = 10.0f)**
  - Sets `center = c`, `radius = r` and positions the camera accordingly.

- **void setCenter(const Vector3& c)**
  - Changes orbit center and updates camera position.

- **void zoom(float delta)**
  - Adjusts `radius` by `delta * zoomSpd` with a minimum radius of `0.1f`.
  - Recomputes camera position on the orbit.

- **void handleJoystick(float joyX, float joyY, float deltaTime)**
  - Changes azimuth and elevation based on joystick input.
  - Elevation is clamped to avoid flipping over the poles.

- **void handleButtons(bool zoomIn, bool zoomOut, float deltaTime)**
  - Zooms in or out using buttons.

Use `OrbitCam` for object inspection, isometric cameras and third‑person orbiting views.

---

## CameraBuilder

```cpp
class CameraBuilder
{
public:
    CameraBuilder& at(const Vector3& pos);
    CameraBuilder& lookAt(const Vector3& tgt);
    CameraBuilder& withUp(const Vector3& up);
    CameraBuilder& persp(float fov = 60, float near = 0.1f, float far = 100);
    CameraBuilder& ortho(float w = 10, float h = 10, float near = 0.1f, float far = 100);
    CameraBuilder& fisheye(float fov = 120, float str = 1, float near = 0.1f, float far = 100);
    CameraBuilder& withConfig(const CameraConfig& cfg);
    Camera           build();
};
```

Fluent helper for constructing a configured `Camera` in a single expression.

- **CameraBuilder& at(const Vector3& pos)**
  - Sets camera `position`.

- **CameraBuilder& lookAt(const Vector3& tgt)**
  - Sets camera `target`.

- **CameraBuilder& withUp(const Vector3& up)**
  - Sets `up` direction and normalizes it.

- **CameraBuilder& persp(float fov, float near, float far)**
  - Applies `setPerspective` with provided parameters.

- **CameraBuilder& ortho(float w, float h, float near, float far)**
  - Applies `setOrtho` with provided parameters.

- **CameraBuilder& fisheye(float fov, float str, float near, float far)**
  - Applies `setFisheye` with provided parameters.

- **CameraBuilder& withConfig(const CameraConfig& cfg)**
  - Copies `CameraConfig` into the underlying camera.

- **Camera build()**
  - Finalizes the camera, calls `markDirty()` and returns it by value.

Use `CameraBuilder` when you want to create and configure a camera in a single, readable chain of calls.

---

## Integration Notes

- The renderer stores and manages one or more `Camera` instances.
- Scene graph (`CameraNode`) applies its transform and projection parameters to a `Camera` and then calls `Camera::markDirty()` so all caches are refreshed.
- Utility functions in `pip3D_camera_utils.h` provide quick setup helpers (`CameraHelper`, `MultiCameraHelper`) built on top of this API.

When writing your own systems, prefer using the public `Camera` methods (`setPerspective`, `setOrtho`, `move*`, `rotate*`, `markDirty`) instead of manually editing internal cache fields. This keeps behavior predictable and aligned with the rest of the engine.
