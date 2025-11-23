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

## LCD and ST7789Display (Low-Level Display Driver)

```cpp
struct LCD
{
    uint16_t w, h;
    int8_t   cs, dc, rst, bl;
    uint32_t freq;      // Hz

    LCD();
    LCD(int8_t cs_, int8_t dc_, int8_t rst_ = 8);

    static LCD pins(int8_t cs, int8_t dc, int8_t rst = 8);

    LCD& size(uint16_t width, uint16_t height);
    LCD& backlight(int8_t pin);
    LCD& speed(uint32_t mhz);
};

LCD S3();
LCD S3(int8_t cs, int8_t dc, int8_t rst = 8);

class ST7789Display
{
public:
    ST7789Display();
    ~ST7789Display();

    bool init(const LCD& cfg = LCD());

    void drawPixel(int16_t x, int16_t y, uint16_t color);
    void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
    void fillScreen(uint16_t color);

    void pushImage(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t* buffer);

    void drawHLine(int16_t x, int16_t y, int16_t w, uint16_t color);
    void drawVLine(int16_t x, int16_t y, int16_t h, uint16_t color);

    void setRotation(uint8_t r);      // 0..3

    uint16_t getWidth() const;
    uint16_t getHeight() const;
};
```

### LCD

- **Purpose**
  - Describes the physical ST7789 panel wiring and SPI frequency.
  - Used as a thin configuration layer between high-level `DisplayConfig` and the hardware driver.

- **Fields**
  - `w, h`
    - Native panel resolution in pixels.
    - Defaults to `320 x 240` which matches the typical ESP32-S3 ST7789 modules.
  - `cs, dc, rst, bl`
    - Chip-select, data/command, reset and backlight pins.
    - `bl` can be `-1` when backlight is hard-wired.
  - `freq`
    - SPI clock in Hz. Default is `80'000'000`.

- **Construction helpers**
  - `LCD(cs, dc, rst)` / `LCD::pins(cs, dc, rst)`
    - Minimal wiring-based constructor when only control pins differ.
  - `size(width, height)`
    - Chainable; overrides resolution.
  - `backlight(pin)`
    - Chainable; selects backlight GPIO.
  - `speed(mhz)`
    - Chainable; sets SPI frequency as `mhz * 1'000'000`.

- **Presets for ESP32-S3**
  - `S3()`
    - Returns default LCD config for a typical 320x240 ST7789 on ESP32-S3 with default pins and 80 MHz SPI.
  - `S3(cs, dc, rst)`
    - Same as `S3()`, but overrides control pins.

Use `LCD` when directly constructing a low-level display driver or when using the `Screen` helper (see `graphics/pip3D_screen.h`). High-level engine code usually works with `DisplayConfig` instead.

### ST7789Display

- **Purpose**
  - Low-level, high-performance SPI driver for ST7789 TFT panels.
  - Owns the SPI device handle, configures the bus, manages DMA-capable buffers and performs endian conversion.
  - Used internally by `FrameBuffer` via `Renderer`, but can also be driven directly for custom UIs.

- **Lifetime and initialization**
  - `bool init(const LCD& cfg = LCD())`
    - Configures GPIO directions for `cs`, `dc`, `rst`, `bl`.
    - Initializes SPI bus on `SPI2_HOST` with DMA, attaches an ST7789 device and sends the recommended power-on sequence:
      - `SWRESET`, `SLPOUT`, `MADCTL`, `COLMOD`, `INVON`, `NORON`, `DISPON`.
    - Allocates an internal DMA-friendly line buffer (`dmaBuffer`) for rectangle fills.
    - Can be called multiple times; previous DMA buffers and SPI device are safely released before reinitialization.
  - `~ST7789Display()`
    - Frees DMA/aligned buffers and detaches the SPI device, releasing the bus.

- **Basic drawing**
  - `drawPixel(x, y, color)`
    - Bounds-checked single-pixel write.
    - Internally uses a fast un-checked path and minimal SPI transaction.
  - `fillRect(x, y, w, h, color)`
    - Clips the rectangle to the current display size.
    - For sufficiently large areas uses an internal DMA buffer filled with the color to minimize SPI overhead.
    - For smaller areas reuses a swap buffer and writes in 32-bit chunks.
  - `fillScreen(color)`
    - Convenience wrapper for `fillRect(0, 0, getWidth(), getHeight(), color)`.
  - `drawHLine(x, y, w, color)` / `drawVLine(x, y, h, color)`
    - Fast helpers that perform clipping on one axis and delegate to `fillRect`.

- **Image upload**
  - `pushImage(x, y, w, h, buffer)`
    - Uploads a block of RGB565 pixels from `buffer` to the display.
    - When `(x, y, w, h)` exactly covers the full screen, uses a large DMA-capable chunk buffer and processes the image in tiles (`DMA_CHUNK_PIXELS`) with endian swapping, maximizing throughput.
    - For partial regions uploads each row sequentially; used by the engine framebuffer to present the final image.

- **Rotation and geometry**
  - `setRotation(r)`
    - `r` in `[0, 3]` selects one of four standard ST7789 orientations.
    - Updates internal `width`/`height` and writes `MADCTL` accordingly.
  - `getWidth()`, `getHeight()`
    - Return the current logical resolution after rotation.

### Usage patterns

- **Engine-integrated path (recommended)**
  - Application code builds a `DisplayConfig` and passes it to `Renderer::init`.
  - The renderer internally translates it to `LCD` and owns a single `ST7789Display` instance used by the main `FrameBuffer`.

- **Direct low-level control**
  - For lightweight demos or debugging overlays without the full 3D stack you can create and use the driver directly:
    - Construct `LCD` (or use `S3()` helper).
    - Call `init(cfg)` once.
    - Use `fillRect`, `fillScreen` or `pushImage` to draw.
  - For convenience you can also use the `Screen` wrapper (`graphics/pip3D_screen.h`), which embeds `ST7789Display` and exposes a simpler 2D API.

This driver is tuned for ESP32-S3 running without PSRAM: all critical paths use DMA-capable internal memory, avoid unnecessary reallocations and are safe for long-running real-time rendering.

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

## Job System (JobSystem and useDualCore)

The job system provides a minimal asynchronous execution layer built on top of
FreeRTOS (on ESP32) and a synchronous fallback on other platforms. It is
defined in `lib/Pip3D/core/pip3D_jobs.h` and is used by long‑running systems
such as physics and the day/night cycle to offload heavy work to the second CPU
core on ESP32‑S3.

```cpp
typedef void (*JobFunc)(void* userData);

struct Job
{
    JobFunc func;
    void*   userData;
};

class JobSystem
{
public:
    static bool init();
    static void shutdown();
    static bool submit(JobFunc func, void* userData = nullptr);
    static bool isEnabled();

private:
    static void workerLoop(void* param);
};

void useDualCore(bool enabled);
bool isDualCoreEnabled();
```

### Overview

- **Single worker task + bounded queue**
  - Internally maintains a fixed‑size ring buffer of jobs (32 entries) and a
    single worker task pinned to the second core on ESP32‑S3.
  - The worker waits on a semaphore and executes jobs one by one without
    additional allocations.

- **Non‑blocking submission**
  - `submit` is intentionally non‑blocking:
    - returns `false` if the job system is not initialized or disabled,
      the function pointer is `nullptr`, the queue is full or the internal
      mutex is momentarily busy.
  - This design is safe for real‑time rendering loops: game/FX/physics code
    never stalls waiting for the worker.

- **Platform behavior**
  - On **ESP32** (`ARDUINO_ARCH_ESP32`):
    - `init()` creates the FreeRTOS worker task, mutex and counting semaphore.
    - Jobs execute truly asynchronously on the worker core.
  - On **other platforms**:
    - `init()` always reports failure and leaves the system disabled.
    - `submit()` still supports direct synchronous execution: if `func` is not
      `nullptr`, it is called immediately on the calling thread and `true` is
      returned.
    - `isEnabled()` and `isDualCoreEnabled()` remain `false`, so engine systems
      that check them will naturally fall back to synchronous code paths.

### JobFunc and Job

- **JobFunc**
  - Function pointer type `void (*)(void*)`.
  - Must point to a **free function** or a `static` member function; capturing
    lambdas and non‑static member functions are not compatible.

- **Job**
  - Lightweight pair of `func` and `userData` stored in the internal queue.
  - Allocated in a fixed static array; there are no dynamic allocations per job.

### JobSystem API

- **bool JobSystem::init()**
  - Creates internal synchronization primitives and starts the worker task
    (on ESP32).
  - May be called multiple times; subsequent calls after successful
    initialization simply re‑enable the system and return `true` without
    recreating resources.
  - Returns `true` on success, `false` if any FreeRTOS resource could not be
    created.

- **void JobSystem::shutdown()**
  - Stops accepting new jobs and deletes the worker task, mutex and semaphore.
  - Resets the queue indices and internal flags.
  - Intended for full engine shutdown or when permanently disabling dual‑core
    execution.

- **bool JobSystem::submit(JobFunc func, void* userData = nullptr)**
  - Attempts to enqueue a new job.
  - Returns `false` if:
    - the system is not initialized or not enabled;
    - `func` is `nullptr`;
    - the internal mutex is not immediately available;
    - the job queue is full.
  - On success, signals the worker via a semaphore and returns `true`.
  - Jobs are executed at‑least‑once and in FIFO order relative to other
    successfully submitted jobs.

- **bool JobSystem::isEnabled()**
  - Reports whether the job system is currently enabled.
  - On ESP32 this is set by `init()` / `shutdown()` and is used by systems like
    physics and day/night cycle to decide whether to use asynchronous paths.

### Dual‑core helpers

- **void useDualCore(bool enabled)**
  - High‑level switch used by the engine and user code.
  - `enabled = true` calls `JobSystem::init()`.
  - `enabled = false` calls `JobSystem::shutdown()`.
  - Safe to call multiple times; redundant `true` calls keep the system
    initialized.

- **bool isDualCoreEnabled()**
  - Thin wrapper around `JobSystem::isEnabled()`.
  - Intended for application‑level checks, for example to expose a "dual‑core
    active" flag in debug UI or telemetry.

### Typical usage

- **Enabling dual‑core execution in `setup()`**
  - The renderer already calls `useDualCore(true)` from `Renderer::init`, and
    the demo additionally calls it from `setup()`. In your own projects you can
    simply ensure that `useDualCore(true)` is called once during startup before
    you submit jobs.

- **Scheduling heavy background work**
  - Create a small context struct with all data needed for the job.
  - Pass a pointer to this context as `userData`.
  - The job function should cast `userData` back to your type, perform the
    work and write results in a way that is safe to read from the main thread
    (for example, by updating a result struct and setting an atomic flag).

- **Engine integration examples**
  - `PhysicsWorld::stepAsync` uses `JobSystem::submit` to run physics steps on
    the worker core when asynchronous mode is enabled.
  - `TimeOfDayController` uses the job system to compute sky and lighting state
    in the background and then applies cached results on the main thread.

Use the job system for medium‑weight, embarrassingly parallel tasks where an
occasional dropped job (when the queue is full or busy) is acceptable, but
blocking the render loop is not.

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

---

## Frustum and Culling

This section describes the view frustum utilities defined in `lib/Pip3D/core/pip3D_frustum.h`.

The frustum is extracted from a camera view‑projection matrix and used to quickly reject
objects that are completely outside the camera field of view. It is lightweight and
designed for real‑time use on ESP32‑S3 without PSRAM.

The renderer and instance manager use this system internally:

- `Renderer` maintains a single `Frustum` built from the active `Camera`.
- `Renderer::drawMesh` / `drawMeshInstance` perform a fast sphere test before drawing.
- `InstanceManager::cull(const Frustum&, std::vector<MeshInstance*>&)` filters instances
  using the same sphere test and returns only visible objects.

---

## FrustumPlane

```cpp
struct FrustumPlane
{
    Vector3 n;   // plane normal (pointing inward into the frustum)
    float   d;   // plane distance in the equation n·p + d = 0

    FrustumPlane();
    FrustumPlane(const Vector3& normal, float dist);
    FrustumPlane(const Vector3& p0, const Vector3& p1, const Vector3& p2);

    float distanceToPoint(const Vector3& p) const;
    bool  containsSphere(const Vector3& center, float radius) const;
    bool  containsPoint(const Vector3& p) const;
};
```

- **Representation**
  - Standard plane form `n·p + d = 0` in world space.
  - `n` is normalized when constructed from three points.

- **distanceToPoint(p)**
  - Signed distance from `p` to the plane.
  - Positive values mean the point lies inside the frustum half‑space.

- **containsPoint(p)**
  - Returns `true` if `p` lies on or inside the plane (`distanceToPoint(p) >= 0`).

- **containsSphere(center, radius)**
  - Returns `true` if a sphere `(center, radius)` is not fully behind the plane.
  - Used as a building block for frustum/sphere tests.

You rarely use `FrustumPlane` directly; it is primarily exposed via `CameraFrustum::getPlane`.

---

## CullingResult

```cpp
enum CullingResult
{
    CULLED  = 0,
    PARTIAL = 1,
    VISIBLE = 2
};
```

- **CULLED** – the tested volume lies completely outside the frustum.
- **VISIBLE** – the volume is fully inside the frustum.
- **PARTIAL** – the volume intersects the frustum (partly visible).

`CullingResult` is returned by detailed sphere tests and is useful when you want more
information than a simple yes/no visibility check.

---

## CameraFrustum and Frustum

```cpp
class CameraFrustum
{
public:
    enum
    {
        NEAR   = 0,
        FAR    = 1,
        LEFT   = 2,
        RIGHT  = 3,
        TOP    = 4,
        BOTTOM = 5
    };

    void extractFromViewProjection(const Matrix4x4& vp);
    void extract(const Matrix4x4& vp);       // alias for extractFromViewProjection

    bool sphere(const Vector3& center, float radius) const;
    bool box(const Vector3& min, const Vector3& max) const;
    bool point(const Vector3& p) const;

    CullingResult cull(const Vector3& center, float radius) const;
    float         factor(const Vector3& center, float radius) const;

    const FrustumPlane& getPlane(int i) const;  // index by NEAR/FAR/LEFT/RIGHT/TOP/BOTTOM
};

using Frustum = CameraFrustum;
```

- **extractFromViewProjection(vp) / extract(vp)**
  - Builds six frustum planes from a combined view‑projection matrix (`proj * view`).
  - All later tests assume that input positions are in the same world space that the
    matrix was built from (usually the `Camera` world space).
  - Called internally by `Renderer::beginFrame()` whenever the camera view or projection
    changes.

- **sphere(center, radius)**
  - Fast inclusion test for a bounding sphere.
  - Returns `false` if the sphere is completely outside at least one frustum plane.
  - Returns `true` when the sphere is fully inside or intersects the frustum.
  - Used by the renderer and instance manager for primary visibility culling.

- **box(min, max)**
  - Tests an axis‑aligned bounding box in world space.
  - Uses the standard “positive vertex” technique per plane to quickly reject boxes
    that lie entirely outside.

- **point(p)**
  - Returns `true` if `p` is inside all six planes.
  - Equivalent to `sphere(p, 0)` but implemented without extra branches.

- **cull(center, radius)**
  - Performs a detailed sphere test and returns `CullingResult`:
    - `CULLED` if the sphere is completely outside,
    - `VISIBLE` if fully inside all planes,
    - `PARTIAL` otherwise.
  - Useful for LOD systems or importance sampling where full/partial visibility matters.

- **factor(center, radius)**
  - Returns a continuous visibility factor in `[0, 1]` for a sphere:
    - `0` when culled,
    - `1` when fully inside,
    - smoothly interpolated in between when intersecting the frustum.
  - Can drive fade‑in/out, particle density, or other quality scaling near frustum edges.

- **getPlane(i)**
  - Provides read‑only access to individual planes (`NEAR`, `FAR`, etc.).
  - Intended for advanced effects (e.g. custom clipping, debugging visualizations).

### Typical usage

- High‑level rendering:
  - Let the engine handle culling via `Renderer::drawMesh*` and `InstanceManager::cull`.
- Manual culling:
  - Obtain the current frustum from `Renderer::getFrustum()`.
  - Use `sphere(center, radius)` or `cull(center, radius)` to decide whether to draw or
    update an object.
  - Use `factor(center, radius)` for smooth transitions when objects approach frustum
    boundaries.

All frustum operations are designed to be allocation‑free and fast under `-O3` with
`-ffast-math` on ESP32‑S3.

## Mesh Geometry (Mesh, Vertex, Face, PackedNormal)

The mesh system provides a compact, ESP32‑S3‑friendly representation of triangle meshes
with quantized positions and packed normals. It is defined in
`lib/Pip3D/geometry/pip3D_mesh.h` and used throughout the engine by primitive shapes,
the renderer, instances and scene graph.

---

### Overview

- **Mesh**
  - Owns vertex and index buffers for a single triangle mesh.
  - Stores positions in a quantized 16‑bit format in a fixed world‑space range.
  - Stores normals in a 16‑bit octahedral encoding (`PackedNormal`).
  - Caches world transform and bounding sphere for fast rendering and culling.

- **Vertex**
  - Internal storage for a single vertex: quantized position (`px, py, pz`) plus
    packed normal.
  - Created implicitly via `Mesh::addVertex`.

- **Face**
  - Internal triangle structure with three 16‑bit indices (`v0, v1, v2`).
  - Created implicitly via `Mesh::addFace`.

- **PackedNormal**
  - Encodes a unit normal into a 16‑bit value using signed octahedral encoding.
  - Provides fast encode/decode suitable for software rasterization on ESP32‑S3.

Most users interact only with `Mesh`; `Vertex`, `Face` and `PackedNormal` are considered
low‑level implementation details but are documented here for completeness.

---

### Mesh

```cpp
class Mesh
{
public:
    Mesh(uint16_t maxVerts = 64,
         uint16_t maxFcs  = 128,
         const Color& color = Color::WHITE);
    virtual ~Mesh();

    uint16_t addVertex(const Vector3& pos);
    bool     addFace(uint16_t v0, uint16_t v1, uint16_t v2);

    void     finalizeNormals();
    void     calculateBoundingSphere();

    void     setPosition(float x, float y, float z);
    void     setRotation(float x, float y, float z);
    void     setScale(float x, float y, float z);
    void     rotate(float x, float y, float z);
    void     translate(float x, float y, float z);

    Vector3  pos() const;
    Vector3  center() const;
    float    radius() const;

    Vector3  vertex(uint16_t index) const;      // world‑space position
    Vector3  normal(uint16_t index) const;      // world‑space normal

    Vector3  decodePosition(const Vertex& v) const; // local‑space position from quantized data

    void     clear();

    uint16_t       numFaces() const;
    const Face&    face(uint16_t i) const;
    const Vertex&  vert(uint16_t i) const;

    Color          color() const;
    void           color(const Color& c);
    void           show();
    void           hide();
    bool           isVisible() const;

    const Matrix4x4& getTransform() const;
};
```

#### Construction and lifetime

- **Mesh(maxVerts, maxFcs, color)**
  - Allocates internal arrays for up to `maxVerts` vertices and `maxFcs` triangle faces.
  - On boards with PSRAM, data is placed there; otherwise it uses internal 8‑bit RAM.
  - Initial transform: position `(0,0,0)`, rotation `(0,0,0)` (Euler angles in degrees),
    scale `(1,1,1)`.

- **~Mesh()**
  - Frees all owned vertex and face memory.
  - Safe to destroy even if no geometry was ever added.

Meshes are **non‑copyable**. Always store and pass them by pointer or reference
(`Mesh*`, `Mesh&`).

#### Geometry building

- **addVertex(pos)**
  - Adds a vertex at `pos` in local‑space coordinates.
  - Position is quantized into 16‑bit components in the range `[-32, 32]` on each axis.
  - Returns the index of the new vertex, or `0xFFFF` if the vertex buffer is full or
    memory was not allocated.

- **addFace(v0, v1, v2)**
  - Adds a triangle referencing three existing vertex indices.
  - Returns `false` if indices are out of range, the face buffer is full, or memory for
    faces was not allocated.

- **finalizeNormals()**
  - Computes smooth per‑vertex normals by accumulating and normalizing face normals.
  - Writes packed normals into each `Vertex` via `PackedNormal`.
  - Does nothing if there are no vertices, no faces or a temporary buffer cannot be
    allocated.

- **calculateBoundingSphere()**
  - Computes a tight bounding sphere in **local space** from all vertices.
  - Center is the average of all vertex positions; radius is the maximum distance to
    this center.
  - Called lazily by `center()` / `radius()` when bounds are invalid.

#### Transform and bounds

- **setPosition/Rotation/Scale**, **rotate**, **translate**
  - Modify the local transform of the mesh and mark internal caches as dirty.
  - Rotation is interpreted as Euler angles in degrees around X/Y/Z.

- **pos()**
  - Returns the current local‑space position of the mesh.

- **center() / radius()**
  - Return the **world‑space** center and radius of the mesh bounding sphere.
  - Internally:
    - Rebuilds the world transform matrix if needed.
    - Recomputes local bounds if they are invalid.
    - Transforms the local bounding center by the world transform.
    - Scales the radius by the maximum component of the current scale and a small
      safety margin.
  - Used by the renderer and instance system for frustum culling.

- **getTransform()**
  - Returns the cached world transform matrix (`Matrix4x4`).
  - Lazily recomputed from position, rotation and scale when marked dirty.

#### Accessing data for rendering

- **vertex(index)**
  - Decodes the quantized local position of vertex `index`, applies the world
    transform and returns a world‑space position.
  - Returns `(0,0,0)` if `index` is out of range.

- **normal(index)**
  - Decodes the packed normal of vertex `index`, transforms it by the world transform
    and returns a normalized world‑space normal.
  - Returns `(0,1,0)` if `index` is out of range.

- **decodePosition(v)**
  - Low‑level helper that converts a `Vertex` back to a local‑space `Vector3` using the
    same quantization parameters as `addVertex`.
  - Used by `finalizeNormals`, `calculateBoundingSphere` and instance rendering.

- **numFaces() / face(i) / vert(i)**
  - Provide read‑only access to the underlying triangle list and vertices for custom
    rendering or tools.

#### Color and visibility

- **color() / color(c)**
  - Get or set the mesh base color used by the renderer when no per‑instance override
    is provided.

- **show() / hide() / isVisible()**
  - Control whether the mesh participates in rendering.
  - `Renderer::drawMesh` checks `isVisible()` before submitting triangles.

#### Utility

- **clear()**
  - Resets `vertexCount` and `faceCount` to zero without freeing the underlying
    buffers.
  - Invalidates cached bounds so they will be recomputed when needed.

Use `Mesh` directly when you build one‑off procedural geometry or static props. For
large numbers of objects sharing the same mesh, prefer `MeshInstance` and
`InstanceManager` described below.

---

## Primitive Shapes

Primitive shapes are ready‑to‑use `Mesh` subclasses that generate common 3D geometry
procedurally. They share the same transform, color and visibility API as `Mesh` and
respect the same quantized position range `[-32, 32]` on each axis.

All shapes build their geometry in **local space** around the origin and then rely on
`Mesh` / `MeshInstance` transforms to place them in the world.

### Shared notes

- All constructors accept an optional `Color` which initializes the base mesh color.
- Geometry is generated once in the constructor and then kept in RAM for the lifetime
  of the object.
- Positions are quantized to 16‑bit coordinates in `[-32, 32]` per axis, which gives a
  resolution of about `1e-3` units across a local span of 64 units.
  - Typical usage is to model shapes in a **compact local space** (sizes on the order
    of `0.1 .. 32`) and use transforms to scale them up or down in world space.

---

### Cube

```cpp
class Cube : public Mesh
{
public:
    Cube(float size = 1.0f,
         const Color& color = Color::WHITE);
};
```

- Axis‑aligned cube centered at the origin.
- **Parameters**
  - `size` – edge length in local units.
  - `color` – base color.
- Generated as 8 vertices and 12 triangles.

Typical usage is as a simple prop, debug volume or building block for voxel‑like scenes.

---

### Pyramid

```cpp
class Pyramid : public Mesh
{
public:
    Pyramid(float size = 1.0f,
            const Color& color = Color::WHITE);
};
```

- Square‑base pyramid with its base in the XZ plane and apex above the origin.
- **Parameters**
  - `size` – approximate base size and height.
  - `color` – base color.

Useful for simple architectural details and debug markers.

---

### Sphere

```cpp
class Sphere : public Mesh
{
public:
    Sphere(float radius = 1.0f,
           uint8_t segments = 8,
           uint8_t rings    = 6,
           const Color& color = Color::WHITE);

    Sphere(float radius, const Color& color);
};
```

- UV‑style sphere built from latitude (`rings`) and longitude (`segments`) bands.
- **Parameters**
  - `radius` – sphere radius in local units.
  - `segments` – number of segments around the equator (minimum effective value: 3).
  - `rings` – number of horizontal rings including poles (minimum effective value: 2).
  - `color` – base color.

Invalid or too small values are clamped internally to produce at least a valid closed
mesh. Very large values increase RAM usage and construction time; for ESP32‑S3 typical
values are in the range `segments 8..32`, `rings 6..24`.

The convenience overload `Sphere(radius, color)` creates a medium‑detail sphere
(`segments = 16`, `rings = 12`).

---

### Plane

```cpp
class Plane : public Mesh
{
public:
    Plane(float width  = 2.0f,
          float depth  = 2.0f,
          uint8_t subdivisions = 1,
          const Color& color   = Color::WHITE);
};
```

- Rectangular grid in the XZ plane centered at the origin.
- **Parameters**
  - `width`, `depth` – plane size in local units.
  - `subdivisions` – number of grid cells per side (minimum effective value: 1).
  - `color` – base color.

Geometry consists of `(subdivisions + 1)^2` vertices and `subdivisions^2 * 2`
triangles. Large subdivision counts quickly increase RAM usage; for terrain‑like
patches on ESP32‑S3 typical values are `1..32`.

---

### Cylinder

```cpp
class Cylinder : public Mesh
{
public:
    Cylinder(float radius   = 1.0f,
             float height   = 2.0f,
             uint8_t segments = 16,
             const Color& color = Color::WHITE);
};
```

- Closed cylinder aligned with the Y axis, centered at the origin.
- **Parameters**
  - `radius` – base radius.
  - `height` – total height (`+Y` to `-Y`).
  - `segments` – number of segments around the circumference (minimum effective value: 3).
  - `color` – base color.

Includes top and bottom caps plus a quad strip for the side.

---

### Cone

```cpp
class Cone : public Mesh
{
public:
    Cone(float radius   = 1.0f,
         float height   = 2.0f,
         uint8_t segments = 16,
         const Color& color = Color::WHITE);
};
```

- Right circular cone aligned with the Y axis, apex above the origin and base in the
  XZ plane.
- **Parameters**
  - `radius` – base radius.
  - `height` – total height.
  - `segments` – number of base segments (minimum effective value: 3).
  - `color` – base color.

---

### Capsule

```cpp
class Capsule : public Mesh
{
public:
    Capsule(float radius   = 1.0f,
            float height   = 2.0f,
            uint8_t segments = 12,
            uint8_t rings    = 6,
            const Color& color = Color::WHITE);
};
```

- Capsule aligned with the Y axis: a cylinder with hemispherical ends.
- **Parameters**
  - `radius` – radius of the hemispheres and cylinder.
  - `height` – total end‑to‑end height; must be at least `2 * radius` to avoid a
    negative cylindrical section.
  - `segments` – segments around the circumference (minimum effective value: 3).
  - `rings` – number of rings used to tessellate both hemispheres (minimum effective
    value: 2; odd values are effectively rounded down).
  - `color` – base color.

Capsules are useful for player and NPC proxies, physics shapes and stylized props.

---

### Teapot

```cpp
class Teapot : public Mesh
{
public:
    Teapot(float scale = 1.0f,
           const Color& color = Color::WHITE);
};
```

- High‑detail teapot mesh generated procedurally from a fixed profile.
- **Parameters**
  - `scale` – uniform scale in local space.
  - `color` – base color.

This shape is heavier than the basic primitives and is intended primarily for demos,
benchmarks and visual testing.

---

### TrefoilKnot

```cpp
class TrefoilKnot : public Mesh
{
public:
    TrefoilKnot(float scale      = 1.0f,
                uint8_t segments    = 64,
                uint8_t tubeSegments = 12,
                const Color& color   = Color::WHITE);
};
```

- Tubular mesh built along a trefoil knot curve.
- **Parameters**
  - `scale` – overall size of the knot in local space.
  - `segments` – samples along the main curve.
  - `tubeSegments` – segments around the tube cross‑section.
  - `color` – base color.

This is a high‑poly decorative shape used for stress‑testing the renderer and for
special visual effects.

## Instance System (MeshInstance and InstanceManager)

The instance system provides a lightweight way to render many copies of the same mesh with
different transforms and colors, while sharing geometry and minimizing allocations.

It is defined in `lib/Pip3D/core/pip3D_instance.h` and tightly integrated with the renderer
and frustum culling.

---

### Overview

- **MeshInstance**
  - Represents a single placed mesh in the world.
  - Stores position, rotation, scale, per‑instance color and visibility.
  - Caches world transform and bounding sphere for fast rendering and culling.

- **InstanceManager**
  - Owns and reuses `MeshInstance` objects via an internal pool.
  - Provides creation, removal, bulk operations and frustum‑based culling.
  - Designed for long‑running scenes on ESP32‑S3 without PSRAM.

- **Renderer integration**
  - `Renderer::drawMeshInstance(MeshInstance*)` draws a single instance with per‑frame
    frustum culling.
  - `Renderer::drawInstances(InstanceManager&)` performs batched culling + sorting and
    draws all visible instances in one call.

Use this system when you want to render multiple copies of the same mesh (tiles, props,
crowds, particles with mesh geometry) efficiently.

---

### MeshInstance

```cpp
class MeshInstance
{
public:
    MeshInstance(Mesh* mesh = nullptr);

    void   reset(Mesh* mesh);

    void   setMesh(Mesh* mesh);
    Mesh*  getMesh() const;

    void   setPosition(const Vector3& pos);
    void   setPosition(float x, float y, float z);

    void   setRotation(const Quaternion& rot);
    void   setEuler(float pitch, float yaw, float roll);
    void   rotate(const Quaternion& deltaRot);

    void   setScale(const Vector3& scl);
    void   setScale(float uniform);
    void   setScale(float x, float y, float z);

    void   setColor(const Color& c);
    Color  color() const;

    void   show();
    void   hide();
    bool   isVisible() const;

    void         updateTransform();
    const Matrix4x4& transform();

    Vector3 center();
    float   radius();

    const Vector3&    pos() const;
    const Quaternion& rot() const;
    const Vector3&    scl() const;

    MeshInstance* at(float x, float y, float z);
    MeshInstance* at(const Vector3& pos);
    MeshInstance* euler(float pitch, float yaw, float roll);
    MeshInstance* size(float s);
    MeshInstance* size(float x, float y, float z);
    MeshInstance* color(const Color& c);
};
```

#### Construction and reset

- **MeshInstance(Mesh* mesh = nullptr)**
  - Initializes an instance bound to `mesh` (may be `nullptr`).
  - Default transform: position `(0,0,0)`, rotation identity, scale `(1,1,1)`.
  - Visible by default, transform and bounds marked dirty.

- **reset(mesh)**
  - Reinitializes an existing instance to the same default state as the constructor,
    binding it to a new `mesh`.
  - Used internally by `InstanceManager` when reusing instances from the pool.

#### Transform API

- **setPosition(pos)** / **setPosition(x, y, z)**
  - Sets world‑space position and marks transform and bounds as dirty.

- **setRotation(rot)**
  - Sets world‑space rotation from a quaternion.
  - Marks transform and bounds as dirty.

- **setEuler(pitch, yaw, roll)**
  - Sets rotation from Euler angles in degrees.
  - Internally converted to a quaternion and then to a matrix on demand.

- **rotate(deltaRot)**
  - Multiplies current rotation by `deltaRot` (incremental rotation).
  - Marks transform and bounds as dirty.

- **setScale(scl)** / **setScale(uniform)** / **setScale(x, y, z)**
  - Sets non‑uniform or uniform scale.
  - Marks transform and bounds as dirty.

These methods are appropriate when you want explicit control over the instance transform.

#### Color and visibility

- **setColor(c)** / **color()**
  - Stores a per‑instance color used by the renderer when drawing this instance.
  - Color is stored as `Color` (`rgb565` internally), consistent with the rest of the engine.

- **show() / hide()**
  - Toggles the `visible` flag.

- **isVisible()**
  - Returns `true` only when both `visible` is set and `getMesh()` is non‑null.
  - Used by the renderer and instance manager as a quick rejection test.

#### Transform and bounds access

- **updateTransform()**
  - Rebuilds the internal world transform matrix from position, rotation and scale if
    marked dirty.
  - Typically called implicitly by `transform()`; you rarely need to call it directly.

- **const Matrix4x4& transform()**
  - Returns the cached world transform matrix.
  - Lazily updates the matrix when transform is dirty.

- **center() / radius()**
  - Compute and cache a world‑space bounding sphere.
  - Center is derived by transforming the mesh local center by the instance transform.
  - Radius is the mesh local radius scaled by the maximum of the absolute scale components.
  - Used by frustum culling (`Renderer::drawMeshInstance`, `InstanceManager::cull`).

#### Accessors and fluent configuration

- **pos() / rot() / scl()**
  - Read‑only access to the current position, rotation and scale.
  - Useful for sorting, debugging and UI overlays.

- **at(x, y, z)** / **at(pos)**
  - Sets position and returns `this` for fluent configuration.

- **euler(pitch, yaw, roll)**
  - Sets rotation from Euler angles (degrees) and returns `this`.

- **size(s)** / **size(x, y, z)**
  - Sets uniform or non‑uniform scale and returns `this`.

- **color(c)**
  - Sets per‑instance color and returns `this`.

These fluent methods mirror the builder pattern used elsewhere in the engine and are
intended for concise setup code:

```cpp
MeshInstance* inst = manager.create(mesh)
    ->at(0.0f, 0.0f, 0.0f)
    ->size(1.0f)
    ->color(Color::WHITE);
```

---

### InstanceManager

```cpp
class InstanceManager
{
public:
    InstanceManager();
    ~InstanceManager();

    void           destroyAll();

    MeshInstance*  create(Mesh* mesh);
    std::vector<MeshInstance*> batch(Mesh* mesh, size_t count);
    MeshInstance*  spawn(Mesh* mesh, float x, float y, float z);

    void           remove(MeshInstance* inst);
    void           clear();

    const std::vector<MeshInstance*>& all() const;
    size_t         count() const;

    void           cull(const Frustum& frustum,
                        std::vector<MeshInstance*>& result) const;
    void           sort(const Vector3& cameraPos,
                        std::vector<MeshInstance*>& insts);

    void           hideAll();
    void           showAll();
    void           tint(const Color& color);
};
```

#### Construction and lifetime

- **InstanceManager() / ~InstanceManager()**
  - Default constructor initializes empty instance and pool arrays.
  - Destructor calls `destroyAll()` and frees all owned `MeshInstance` objects.

- **destroyAll()**
  - Deletes every managed instance (both active and pooled), clears internal containers
    and shrinks them to release memory.
  - Use this when unloading or fully resetting a scene to reclaim RAM.

#### Creation and pooling

- **create(mesh)**
  - Returns a new `MeshInstance*` bound to `mesh`.
  - First tries to reuse an object from the internal pool via `MeshInstance::reset(mesh)`;
    if the pool is empty, allocates a new instance.
  - Appends the instance to the active list; ownership is retained by the manager.

- **batch(mesh, count)**
  - Creates `count` instances of the same mesh via repeated calls to `create(mesh)`.
  - Returns a `std::vector<MeshInstance*>` with all newly created instances.
  - Useful for bulk spawning of tiles, grass patches, etc.

- **spawn(mesh, x, y, z)**
  - Convenience wrapper around `create(mesh)` followed by `at(x, y, z)`.
  - Returns the configured instance pointer.

#### Lifetime management

- **remove(inst)**
  - Removes `inst` from the active list in **O(1)** time using an internal index.
  - Moves the last active instance into the freed slot, updates its index and pushes
    `inst` back into the pool for reuse.
  - If `inst` is not currently managed by this `InstanceManager`, the call is ignored.

- **clear()**
  - Moves all active instances into the pool without deleting them.
  - After `clear()`, `all()` is empty but memory for instances remains allocated and can
    be reused cheaply by subsequent `create()` calls.

- **destroyAll()** (see above)
  - Fully frees memory and resets both active list and pool.

#### Accessors

- **all()**
  - Returns a reference to the internal array of active instances.
  - Intended for manual iteration in custom rendering or logic code.

- **count()**
  - Returns the current number of active instances.

#### Culling and sorting

- **cull(frustum, result)**
  - Clears `result` and appends only those instances that are visible and whose bounding
    spheres pass `frustum.sphere(center, radius)`.
  - Uses `MeshInstance::isVisible()`, `center()` and `radius()`; no allocations are
    performed inside.

- **sort(cameraPos, insts)**
  - Sorts `insts` in place by **descending** squared distance to `cameraPos`.
  - Common usage is to sort transparent instances from back to front for correct alpha
    blending or to control overdraw.

#### Bulk operations

- **hideAll() / showAll()**
  - Iterate over all active instances and call `hide()` / `show()`.
  - Affect only the visibility flag; geometry and transforms remain unchanged.

- **tint(color)**
  - Applies `setColor(color)` to all active instances.
  - Useful for quick global effects (e.g. fade to monochrome, selection highlighting).

---

### Typical usage

#### Simple manual rendering

```cpp
InstanceManager manager;

MeshInstance* cube = manager.create(cubeMesh)
    ->at(0.0f, 0.0f, 0.0f)
    ->size(1.0f)
    ->color(Color::WHITE);

// In the render loop:
for (MeshInstance* inst : manager.all())
{
    renderer.drawMeshInstance(inst);
}
```

- The renderer performs frustum culling per instance in `drawMeshInstance`.
- Suitable when you have full control over which instances to draw and in which order.

#### Batched culling and drawing

```cpp
InstanceManager manager;

// ... create instances ...

// In the render loop:
renderer.drawInstances(manager);
```

- `drawInstances` uses `InstanceManager::cull` to filter visible instances based on the
  current camera frustum, then `InstanceManager::sort` to order them by distance and
  finally draws each instance without repeating the frustum test.
- Recommended when you want a high‑level, allocation‑free instance rendering pipeline
  similar to instanced static meshes in larger engines.

#### Scene reset and memory management

- For long‑running applications on ESP32‑S3 without PSRAM:

  - Use `clear()` when you want to temporarily discard all instances but keep memory
    reserved for quick reuse.
  - Use `destroyAll()` when unloading a level or performing a full scene reset to
    actually free memory and shrink internal containers.

This instance system is designed to be safe for continuous operation: pooling avoids
frequent allocations, and explicit lifetime APIs let you control when memory is reused
or released.
