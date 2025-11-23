# PIP3D Engine API

## Core System

This section describes low-level core utilities defined in `lib/Pip3D/core/pip3D_core.h`.

They provide display configuration, color utilities, skybox presets, frame timing, viewports,
basic memory helpers and optional systems such as events, profiling and resource management.

---

## DisplayConfig

```cpp
using DisplayConfig = Display;

struct Display
{
    uint16_t width, height;
    int8_t   cs, dc, rst, bl;
    uint32_t spi_freq;
};
```

- **width, height**
  - Target display resolution in pixels.
  - Used by the renderer and framebuffer to size internal buffers.

- **cs, dc, rst, bl**
  - SPI control pins for the ST7789 display.
  - `bl` (backlight) may be `-1` when not used.

- **spi_freq**
  - SPI clock frequency in Hz (for example `80'000'000` or `100'000'000`).

Typical usage is to construct a `DisplayConfig` in `setup()` and pass it to `Renderer::init`.

---

## Color

```cpp
struct Color
{
    uint16_t rgb565;

    Color();
    Color(uint16_t c);
    Color(uint8_t r, uint8_t g, uint8_t b);

    static Color  rgb(uint8_t r, uint8_t g, uint8_t b);
    static Color  fromRGB888(uint8_t r, uint8_t g, uint8_t b);
    static Color  fromTemperature(float kelvin);
    static Color  hsv(float h, float s, float v);

    Color  blend(const Color& other, uint8_t alpha) const;
    Color  darken(uint8_t amount) const;
    Color  lighten(uint8_t amount) const;
    uint8_t brightness() const;
};
```

- **Storage format**
  - `rgb565` packed 16-bit color: R5 G6 B5.
  - Optimized for ST7789 and the internal framebuffer.

- **Construction helpers**
  - `Color(uint8_t r, uint8_t g, uint8_t b)` / `Color::rgb(r, g, b)`
    - Accept 8-bit per channel input and convert to RGB565.
  - `fromRGB888(r, g, b)`
    - Alias for `rgb` for clarity when converting from standard 24-bit colors.
  - `fromTemperature(kelvin)`
    - Converts a color temperature (Kelvin) into an approximate RGB color.
    - Useful for dynamic lighting based on time of day or sky presets.

- **HSV utilities**
  - `hsv(h, s, v)`
    - Creates a color from HSV components:
      - `h` in `[0, 1)` (wrapped if out of range),
      - `s` and `v` in `[0, 1]`.
    - Internally converted to RGB and then to RGB565.

- **Color operations**
  - `blend(other, alpha)`
    - Alpha-blends `other` over this color.
    - `alpha` in `[0, 255]` (0 = keep original, 255 = fully other).
  - `darken(amount)` / `lighten(amount)`
    - Scales color towards black or towards maximum channel values.
  - `brightness()`
    - Returns approximate perceived brightness in `[0, 255]`.

- **Predefined RGB565 constants**
  - `BLACK, WHITE, RED, GREEN, BLUE, CYAN, MAGENTA, YELLOW`.
  - Extended palette: `GRAY, DARK_GRAY, LIGHT_GRAY, ORANGE, PINK, PURPLE, BROWN, LIME`.

Use `Color` throughout the engine for all color parameters: clear colors, materials,
particle systems, skyboxes and lighting.

---

## SkyboxType and Skybox

```cpp
enum SkyType
{
    DAY,
    SUNSET,
    NIGHT,
    DAWN,
    OVERCAST,
    CUSTOM
};

using SkyboxType = SkyType;
using Skybox     = Sky;
```

```cpp
struct Sky
{
    SkyType type;
    Color   top, horizon, ground;
    bool    enabled;

    Sky();
    Sky(SkyType preset);
    Sky(Color top, Color horizon, Color ground);

    void   setPreset(SkyType preset);
    void   setCustom(Color top, Color horizon, Color ground);
    float  getRecommendedLightTemperature() const;
    Color  getLightColor() const;
    Color  getColorAtY(int16_t y, int16_t height) const;
};
```

- **Presets**
  - `DAY`, `SUNSET`, `NIGHT`, `DAWN`, `OVERCAST` map to predefined gradients
    for sky top, horizon and ground.
  - `CUSTOM` is used when calling `setCustom`.

- **setPreset(SkyType preset)**
  - Selects one of the built-in gradients.
  - Used by the framebuffer and day-night system to change the sky quickly.

- **setCustom(top, horizon, ground)**
  - Defines a fully custom gradient and marks the sky as `CUSTOM`.

- **getRecommendedLightTemperature()**
  - Returns an approximate color temperature (Kelvin) that matches the current sky preset.
  - Can be fed into directional light color computation.

- **getLightColor()**
  - Convenience wrapper that converts the recommended temperature into a `Color`.

- **getColorAtY(y, height)**
  - Returns the sky color for a given screen-space Y coordinate and viewport height.
  - Used internally by the framebuffer to draw the background gradient.

Typical high-level usage is through `Renderer`:

- `setSkyboxEnabled(bool)`
- `setSkyboxType(SkyboxType)`
- `setSkyboxWithLighting(SkyboxType)`
- `Skybox& getSkybox()` for fine-grained control.

---

## PerformanceCounter (PerfCounter)

```cpp
using PerformanceCounter = PerfCounter;

class PerfCounter
{
public:
    void   begin();
    void   endFrame();
    float  getFPS() const;
    float  getAverageFPS() const;
    uint32_t getFrameTime() const;      // microseconds
    uint32_t getFrameTimeMs() const;    // milliseconds
    bool   isStable() const;
    float  getEfficiency() const;       // 0..1 relative to 120 FPS
    void   reset();
};
```

- **Usage pattern**
  - Call `begin()` at the start of each frame.
  - Call `endFrame()` after all rendering work is done.
  - Query `getFPS()`, `getAverageFPS()` or `getFrameTime()` from the main loop or UI.

- **Metrics**
  - `getFPS()`
    - Last-frame FPS (subject to basic sanity filtering).
  - `getAverageFPS()`
    - Smoothed FPS based on a rolling window of recent measurements.
  - `getFrameTime()` / `getFrameTimeMs()`
    - Raw duration of the last frame.
  - `isStable()`
    - Returns `true` once a stable average has been accumulated and it stays above 5 FPS.
  - `getEfficiency()`
    - Normalized performance indicator in `[0, 1]` relative to a 120 FPS target.

The renderer exposes these metrics via methods like `Renderer::getFPS()` for HUD overlays
and performance diagnostics.

---

## Viewport

```cpp
struct Viewport
{
    int16_t  x, y;
    uint16_t width, height;

    Viewport();
    Viewport(int16_t x, int16_t y, uint16_t w, uint16_t h);
    Viewport(uint16_t w, uint16_t h);

    bool      contains(int16_t px, int16_t py) const;
    float     aspect() const;
    uint32_t  area() const;
};
```

- **x, y, width, height**
  - Defines the active rendering region in framebuffer coordinates.

- **contains(px, py)**
  - Checks whether a pixel lies inside the viewport.

- **aspect()**
  - Returns `width / height` as a float.
  - Used to build correct projection matrices.

- **area()**
  - Returns total pixel count (`width * height`).

The renderer stores the active viewport and exposes it via `Renderer::getViewport()`;
other systems (e.g. FX) can use it for screen-space operations.

---

## MemUtils

```cpp
struct MemUtils
{
    static size_t getFreeHeap();
    static size_t getFreePSRAM();
    static size_t getLargestFreeBlock();

    static void*  allocAligned(size_t size, size_t align = 4);
    static void   freeAligned(void* ptr);
    static bool   isInPSRAM(void* ptr);
};
```

- **Diagnostics**
  - `getFreeHeap()`
    - Returns free heap size in bytes.
  - `getFreePSRAM()`
    - Returns free PSRAM size in bytes (0 if PSRAM is not present or disabled).
  - `getLargestFreeBlock()`
    - Returns the largest allocatable block size from the heap.

- **Allocation helpers**
  - `allocAligned(size, align)`
    - On systems with PSRAM enabled may route large allocations to PSRAM.
    - Used by `ResourceManager` for resource data buffers.
  - `freeAligned(ptr)`
    - Safe wrapper that checks for `nullptr` before freeing.

- **isInPSRAM(ptr)**
  - Checks whether the pointer resides in the PSRAM address range
    (platform-dependent; on ESP32-S3 without PSRAM this will always be false).

You can use these helpers in your own systems to monitor and manage memory usage.

---

## CoreConfig and Palette

```cpp
struct CoreConfig
{
    struct Performance
    {
        static constexpr int   FPS_SAMPLES   = 60;
        static constexpr int   MAX_FPS       = 120;
        static constexpr int   MIN_FPS       = 5;
        static constexpr uint32_t FRAME_TIME_US = 16667;
    };

    struct Rendering
    {
        static constexpr int   MAX_VERTICES  = 10000;
        static constexpr int   MAX_TRIANGLES = 20000;
        static constexpr float Z_NEAR        = 0.1f;
        static constexpr float Z_FAR         = 1000.0f;
    };
};
```

- **CoreConfig**
  - Collection of engine-wide constants and suggested limits.
  - Does not allocate any memory by itself; all fields are `constexpr`.

```cpp
struct Palette
{
    static Color get(const Color* palette, int size, float t);
};
```

- **Palette::get(palette, size, t)**
  - Returns a color by interpolating between entries in a user-provided palette.
  - `t` in `[0, 1]` selects a position along the palette.
  - Uses `Color::blend` internally for smooth transitions.

---

## Event System

```cpp
enum EventType
{
    EVENT_FRAME_START   = 0,
    EVENT_FRAME_END     = 1,
    EVENT_MESH_LOADED   = 2,
    EVENT_TEXTURE_LOADED= 3,
    EVENT_CAMERA_CHANGED= 4,
    EVENT_SCENE_CHANGED = 5,
    EVENT_MEMORY_LOW    = 6,
    EVENT_FPS_CHANGED   = 7,
    EVENT_USER_CUSTOM   = 100
};

struct EventSystem
{
    static bool subscribe(EventType type,
                          void (*callback)(EventType, void*),
                          void* userData = nullptr);
    static void unsubscribe(void (*callback)(EventType, void*));
    static void emit(EventType type, void* data = nullptr);
    static void cleanup();
    static int  getListenerCount();
};
```

- **subscribe(type, callback, userData)**
  - Registers a listener for the given event type.
  - `userData` is passed back to the callback when events are emitted (if no explicit data is provided).

- **unsubscribe(callback)**
  - Marks matching listeners as inactive; call `cleanup()` to compact the internal list.

- **emit(type, data)**
  - Broadcasts an event to all active listeners.
  - If `data` is `nullptr`, each listener receives its own `userData` pointer.

The event system is lightweight and entirely optional; it is primarily used by the
resource manager when resources are loaded.

---

## Profiler

```cpp
struct Profiler
{
    static void  beginSection(const char* name);
    static void  endSection();
    static void  printReport();
    static void  reset();
    static float getSectionTime(const char* name);
};
```

- **beginSection(name) / endSection()**
  - Mark a timed code region identified by `name`.
  - Multiple calls accumulate total time and call counts per section.

- **printReport()**
  - Prints a profiling summary to the serial console:
    average time per section and percentage of total frame time.

- **reset()**
  - Clears accumulated profiling data.

- **getSectionTime(name)**
  - Returns average time (in milliseconds) for the named section.

This profiler is intended for low-level performance investigation on-device,
complementing higher-level tools.

---

## ResourceManager

```cpp
enum ResourceType
{
    RES_TEXTURE = 0,
    RES_MESH    = 1,
    RES_SOUND   = 2,
    RES_DATA    = 3
};

struct ResourceManager
{
    static void* load(const char* path, ResourceType type, size_t size);
    static void  unload(const char* path);
    static void  unloadAll();

    static size_t getMemoryUsage();
    static size_t getMaxMemory();
    static int    getResourceCount();
    static void   printStatus();
};
```

- **init(maxMemBytes)**
  - Initializes internal bookkeeping and sets an upper limit on total resource memory.

- **load(path, type, size)**
  - Allocates a raw memory block of `size` bytes for the specified resource and tracks it.
  - If the resource with the same `path` is already loaded, increases its reference count
    and returns the existing data pointer.
  - On success, emits an event:
    - `EVENT_TEXTURE_LOADED` for `RES_TEXTURE`,
    - `EVENT_MESH_LOADED` for `RES_MESH`,
    - `EVENT_USER_CUSTOM` for other types.

- **unload(path)**
  - Decreases the reference count; when it reaches zero, frees the memory and marks
    the entry as unloaded.

- **unloadAll()**
  - Frees all loaded resources and resets internal state.

- **getMemoryUsage() / getMaxMemory() / getResourceCount()**
  - Introspection helpers for current resource usage.

- **printStatus()**
  - Prints a summary of all tracked resources to the serial console.

The resource manager focuses on lifetime and memory tracking; loading file contents
into the allocated buffers is left to higher-level systems.

---

## Camera System

This section describes the camera subsystem defined in `lib/Pip3D/core/pip3D_camera.h`.

The camera is designed for real‑time 3D rendering on ESP32‑S3 without PSRAM and follows industry‑standard concepts similar to Unreal Engine and other modern engines.

---

## Overview

- **World‑space camera**: stores position, target (look‑at point) and up vector in world coordinates.
- **Configurable projection**: perspective, orthographic and fisheye projections.
- **Lazy evaluation**: view and projection matrices are cached and recomputed only when needed.
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
    float aspectEps;
};
```

- **aspectEps**
  - Threshold for aspect‑ratio changes in `getProjectionMatrix(aspect)`.
  - If `|aspect - lastAspect| <= aspectEps`, the cached projection is reused.
  - Default: `1e-6f` (effectively recomputed only when aspect truly changes).

Use `CameraConfig` to tune when projection matrices are recomputed for a changing viewport aspect.

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

When changing projection or its parameters directly (e.g. writing to `fov`), call `markDirty()` afterwards so cached matrices are rebuilt.

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
  - Marks all camera caches (view, projection, combined matrix, vectors, orthographic extents) as dirty.
  - Call this after changing camera fields directly (e.g. writing to `position`, `fov`, `projectionType`) instead of going through the `set*`/`move*`/`rotate*` APIs.

This ensures that subsequent calls to `getViewMatrix` and `getProjectionMatrix` will recompute internal state correctly.

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
