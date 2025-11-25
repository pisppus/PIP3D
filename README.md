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

## FrameBuffer

```cpp
class FrameBuffer
{
public:
    FrameBuffer();

    bool init(const DisplayConfig& cfg, ST7789Display* display);

    void beginFrame();
    void endFrame();
    void endFrameRegion(int16_t x, int16_t y, int16_t w, int16_t h);

    uint16_t*       getBuffer();
    const uint16_t* getBuffer() const;
    const DisplayConfig& getConfig() const;

    void     setSkyboxEnabled(bool enabled);
    void     setSkyboxType(SkyboxType type);
    void     setClearColor(Color color);

    Skybox&       getSkybox();
    const Skybox& getSkybox() const;
    bool          isSkyboxEnabled() const;
};
```

### Overview

- **Purpose**
  - Owns the main RGB565 back buffer for the 3D renderer.
  - Bridges high-level rendering code and the low-level `ST7789Display::pushImage`.
  - Implements skybox/clear logic and provides a raw pointer for rasterizers and HUD.

- **Lifetime**
  - Typically embedded as a member of `Renderer`.
  - `Renderer::init` creates a single `ST7789Display` and passes it to `FrameBuffer::init`.
  - The framebuffer allocates a single DMA-capable `width * height` buffer and frees it in its destructor.

### Initialization and configuration

- `bool init(const DisplayConfig& cfg, ST7789Display* display)`
  - Allocates an aligned RGB565 buffer of size `cfg.width * cfg.height`.
  - Uses PSRAM (`MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA`) when available, otherwise internal DMA-capable RAM.
  - Returns `false` if called more than once on the same instance or when allocation fails.
  - Stores `cfg` and the display pointer for later presentation.

- `const DisplayConfig& getConfig() const`
  - Returns the configuration used for buffer allocation.
  - Width/height define the layout of the raw framebuffer pointer.

- `uint16_t* getBuffer()` / `const uint16_t* getBuffer() const`
  - Provides direct access to the backing RGB565 buffer.
  - Used by rasterizers, shadow renderer, HUD renderer and FX systems.
  - Returns `nullptr` when `init` has not succeeded.

### Frame lifecycle

- `void beginFrame()`
  - Early-out when the buffer is not allocated.
  - If the skybox is enabled (`isSkyboxEnabled()` and `Skybox::enabled`):
    - Fills the entire buffer with a vertical gradient computed by `Skybox::getColorAtY`.
    - Applies a subtle per-line dithering pattern to reduce banding.
  - Otherwise clears the buffer uniformly to the current clear color using a 32-bit optimized path.

- `void endFrame()`
  - If the buffer or display pointer is null, does nothing.
  - Uploads the entire framebuffer to the display via `ST7789Display::pushImage(0, 0, width, height, buffer)`.
  - Used by `DirtyRegionHelper` when a full-screen refresh is more efficient than partial updates.

- `void endFrameRegion(int16_t x, int16_t y, int16_t w, int16_t h)`
  - If the buffer or display pointer is null, does nothing.
  - Requests a partial upload of the rectangle `(x, y, w, h)` from the main buffer.
  - Coordinates are in framebuffer space; clipping and bounds checks are handled by `ST7789Display::pushImage`.
  - Called by `DirtyRegionHelper::finalizeFrame` for dirty rectangles and HUD regions.

### Skybox and clear color control

- `void setSkyboxEnabled(bool enabled)`
  - Toggles usage of the skybox gradient during `beginFrame()`.
  - When disabled, `beginFrame()` uses the solid clear color instead.

- `void setSkyboxType(SkyboxType type)`
  - Forwards to `Skybox::setPreset`.
  - Does not automatically change lighting; use `Renderer::setSkyboxWithLighting` for coupled sky/lighting changes.

- `Skybox& getSkybox()` / `const Skybox& getSkybox() const`
  - Provides direct access to the underlying `Skybox` instance for fine-grained customization.
  - Common uses: tweaking horizon/ground colors, enabling/disabling the skybox at runtime.

- `bool isSkyboxEnabled() const`
  - Returns the high-level flag controlling whether `beginFrame()` draws the skybox gradient or a flat clear.

- `void setClearColor(Color color)`
  - Sets the color used by the fast clear path when the skybox is disabled.
  - The color is stored as RGB565 and used directly by `fastClear()`.

Typical high-level usage is through `Renderer`:

- `setSkyboxEnabled(bool)`
- `setSkyboxType(SkyboxType)`
- `setSkyboxWithLighting(SkyboxType)`
- `Skybox& getSkybox()`

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

---

## Dirty Regions and Partial Frame Updates (DirtyRegionHelper)

```cpp
static constexpr int MAX_WORLD_DIRTY_INSTANCES = 32;

struct WorldInstanceDirtySlot
{
    MeshInstance* instance;
    int16_t       curMinX, curMinY, curMaxX, curMaxY;
    int16_t       lastMinX, lastMinY, lastMaxX, lastMaxY;
    bool          hasCurrent;
    bool          hasLast;
};

class DirtyRegionHelper
{
public:
    static void addDirtyRect(MeshInstance* instance,
                             int16_t x, int16_t y, int16_t w, int16_t h,
                             const Viewport& viewport,
                             WorldInstanceDirtySlot worldInstanceDirty[MAX_WORLD_DIRTY_INSTANCES],
                             int16_t& worldDirtyMinX,
                             int16_t& worldDirtyMinY,
                             int16_t& worldDirtyMaxX,
                             int16_t& worldDirtyMaxY,
                             bool& hasWorldDirtyRegion);

    static void addHudDirtyRect(int16_t x, int16_t y, int16_t w, int16_t h,
                                const Viewport& viewport,
                                int16_t& hudDirtyMinX,
                                int16_t& hudDirtyMinY,
                                int16_t& hudDirtyMaxX,
                                int16_t& hudDirtyMaxY,
                                bool& hasHudDirtyRegion);

    static void finalizeFrame(FrameBuffer& framebuffer,
                              PerformanceCounter& perfCounter,
                              WorldInstanceDirtySlot worldInstanceDirty[MAX_WORLD_DIRTY_INSTANCES],
                              int16_t& worldDirtyMinX,
                              int16_t& worldDirtyMinY,
                              int16_t& worldDirtyMaxX,
                              int16_t& worldDirtyMaxY,
                              int16_t& lastWorldDirtyMinX,
                              int16_t& lastWorldDirtyMinY,
                              int16_t& lastWorldDirtyMaxX,
                              int16_t& lastWorldDirtyMaxY,
                              bool& hasWorldDirtyRegion,
                              bool& hasLastWorldDirtyRegion,
                              int16_t& hudDirtyMinX,
                              int16_t& hudDirtyMinY,
                              int16_t& hudDirtyMaxX,
                              int16_t& hudDirtyMaxY,
                              bool& hasHudDirtyRegion,
                              bool cameraChangedThisFrame,
                              uint32_t statsTrianglesTotal,
                              uint32_t statsTrianglesBackfaceCulled,
                              uint32_t statsInstancesTotal,
                              uint32_t statsInstancesFrustumCulled,
                              uint32_t statsInstancesOcclusionCulled);
};
```

### Overview

- **Purpose**
  - Tracks screen‑space regions that changed this frame and performs partial buffer
    uploads instead of full‑screen refreshes.
  - Designed for ESP32‑S3 + ST7789 where SPI bandwidth is the main bottleneck.
  - Used internally by `Renderer` together with `FrameBuffer` and `Viewport`.

- **High‑level behavior**
  - Each moving `MeshInstance` owns a `WorldInstanceDirtySlot` storing its previous and
    current dirty rectangle in framebuffer coordinates.
  - Between frames, dirty rectangles are merged and compared against the total
    framebuffer area.
  - If dirty coverage is small, only the affected regions are pushed to the display.
  - If coverage grows beyond a threshold (≈70% of the screen) or the camera changes,
    the system falls back to a full‑screen refresh.

Typical user code does not call `DirtyRegionHelper` directly; it is driven from
`Renderer::drawMeshInstance*` and HUD helpers. Advanced users can interact with it
via custom render paths if needed.

### MAX_WORLD_DIRTY_INSTANCES

- **Value**: `32`.
- **Meaning**
  - Upper bound on the number of world‑space instances tracked individually for
    per‑object dirty rectangles.
  - Additional instances fall back to a single aggregated world dirty region, which
    is still used for partial updates, but without per‑instance separation.

Choose this constant based on your typical number of dynamic objects. Increasing it
trades a little more RAM in the renderer for potentially finer dirty tracking.

### WorldInstanceDirtySlot

- **Fields**
  - `MeshInstance* instance`
    - Pointer to the tracked instance or `nullptr` when the slot is free.
  - `curMinX, curMinY, curMaxX, curMaxY`
    - Current frame dirty rectangle in inclusive‑exclusive form `[min, max)` in
      framebuffer coordinates.
  - `lastMinX, lastMinY, lastMaxX, lastMaxY`
    - Same rectangle for the previous frame.
  - `hasCurrent`, `hasLast`
    - Flags indicating whether the corresponding rectangle is valid.

Slots are maintained by `addDirtyRect` and `finalizeFrame`. Application code should
treat them as an internal implementation detail of the renderer.

### DirtyRegionHelper::addDirtyRect

- **Signature**
  - See declaration above.

- **Behavior**
  - Clips the input rectangle `(x, y, w, h)` to the active `Viewport`.
  - If `instance == nullptr`, expands the global world dirty region
    (`worldDirtyMin*`/`worldDirtyMax*`).
  - Otherwise:
    - Searches for an existing slot for `instance` or allocates a free one.
    - If no free slot is available, falls back to expanding the global world dirty
      region instead of losing information.
    - Merges the new rectangle into `cur*` of the instance’s slot.

`Renderer` calls this indirectly from its internal `addDirtyFromSphere` helper,
which approximates a 3D bounding sphere as a 2D screen‑space rectangle.

### DirtyRegionHelper::addHudDirtyRect

- **Purpose**
  - Tracks 2D HUD/UI dirty regions separately from world geometry.
  - Used by helpers like `Renderer::drawText` to keep HUD updates efficient.

- **Behavior**
  - Clips `(x, y, w, h)` to the viewport.
  - Initializes or expands the single HUD dirty rectangle
    (`hudDirtyMin*`/`hudDirtyMax*`).
  - Ignores fully off‑screen rectangles.

HUD dirt is stored separately so that UI changes alone can trigger partial refreshes
even when the 3D world is static.

### DirtyRegionHelper::finalizeFrame

- **Purpose**
  - Computes the minimal set of screen rectangles that must be sent to the display at
    the end of the frame.
  - Chooses between full‑frame and partial updates based on dirty coverage.
  - Updates performance counters and periodically prints frame statistics.

- **Inputs**
  - `framebuffer`
    - Provides size (`DisplayConfig`) and the backing RGB565 buffer.
  - `perfCounter`
    - Receives `endFrame()` call after the upload path is chosen.
  - `worldInstanceDirty[]`
    - Array of per‑instance slots for moving meshes.
  - World / last world dirty bounds and flags
    - Pairs of min/max coordinates for aggregated regions and corresponding
      `hasWorldDirtyRegion` / `hasLastWorldDirtyRegion` flags.
  - HUD dirty bounds and flag
    - Single rectangle and `hasHudDirtyRegion`.
  - `cameraChangedThisFrame`
    - When `true`, forces a full‑screen refresh (camera movement invalidates all
      previous dirty tracking).
  - Statistics counters (`statsTriangles*`, `statsInstances*`)
    - Used only for periodic debug prints; do not affect dirty logic.

- **Algorithm (simplified)**
  - If the camera changed or there are no world/HUD dirty regions at all, perform a
    full‑screen refresh and reset all slots.
  - For each `WorldInstanceDirtySlot`:
    - Build a rectangle that covers both current and last positions when needed.
    - Discard degenerate rectangles (`width <= 0` or `height <= 0`).
    - Append to a temporary list and roll current → last for the next frame.
  - Fold aggregated world dirty regions into the list as one extra rectangle.
  - Optionally compute a HUD rectangle and its area.
  - If there is still no world or HUD dirt, perform a full‑screen refresh.
  - Iteratively merge overlapping world rectangles into a smaller set.
  - Compute `worldArea` as the sum of merged rectangle areas and `combinedArea` as
    `worldArea + hudArea`.
  - If `combinedArea` exceeds about 70% of the full framebuffer area, fall back to a
    full‑screen refresh.
  - Otherwise call `FrameBuffer::endFrameRegion` for each merged world rectangle and,
    if present, for the HUD rectangle, then finish with `perfCounter.endFrame()`.

This approach is entirely allocation‑free, uses only fixed‑size arrays on the stack
and is safe for long‑running real‑time rendering on ESP32‑S3 without PSRAM.

---

## Z-Buffer and Dithering (ZBuffer, BAYER_MATRIX_4X4)

Z-buffer and ordered dithering are defined in `lib/Pip3D/rendering/display/pip3D_zbuffer.h`
and used throughout the renderer, rasterizer and shadow system.

```cpp
namespace pip3D
{
    static constexpr float BAYER_MATRIX_4X4[4][4];

    template <uint16_t WIDTH, uint16_t HEIGHT>
    class ZBuffer
    {
    public:
        ZBuffer();
        ZBuffer(const ZBuffer&) = delete;
        ZBuffer& operator=(const ZBuffer&) = delete;

        bool     init();
        void     clear();

        bool     testAndSet(uint16_t x, uint16_t y, float z);
        bool     hasGeometry(uint16_t x, uint16_t y) const;
        bool     hasShadow(uint16_t x, uint16_t y) const;
        void     markShadow(uint16_t x, uint16_t y);

        void     testAndSetScanline(uint16_t y,
                                    uint16_t x_start,
                                    uint16_t x_end,
                                    float    z_start,
                                    float    z_step,
                                    uint16_t* frameBuffer,
                                    uint16_t  color);
    };
}
```

### BAYER_MATRIX_4X4

- **Purpose**
  - 4×4 Bayer matrix used for ordered dithering when converting HDR lighting
    results to 16-bit RGB565 output.
  - Accessed by `Shading::applyDithering` and indirectly by all triangle
    rasterization paths.

- **Layout**
  - Normalized values in `[0, 1)` pre-divided by `16.0f`:
    - Covers the standard Bayer pattern for 4×4 tiles.
  - Indexed as `BAYER_MATRIX_4X4[y & 3][x & 3]` for any integer pixel
    coordinates.

Typical users do not access this matrix directly; it is part of the
internal shading pipeline.

### ZBuffer<WIDTH, HEIGHT>

- **Purpose**
  - Per-pixel depth buffer with integrated shadow-mask bit used by the
    software rasterizer, culling and shadow renderer.
  - Stores one 16-bit value per pixel for a fixed resolution determined by
    the template parameters `WIDTH` and `HEIGHT`.

- **Internal layout (high level)**
  - Each pixel is a 16-bit signed value:
    - Lower 15 bits store the depth in a fixed-point range based on
      `0 .. MAX_DEPTH`.
    - The top bit is a **shadow flag** used to mark pixels that already
      received a shadow contribution.
  - The exact bit layout is an implementation detail but is stable across
    the engine and used consistently by all consumers.

- **Construction and lifetime**
  - `ZBuffer()`
    - Default-constructs an empty buffer with `buffer == nullptr`.
    - Does not allocate memory by itself.
  - `ZBuffer(const ZBuffer&) = delete`, `operator= = delete`
    - Z-buffer instances are non-copyable to avoid accidental sharing of the
      underlying memory.

- **bool init()**
  - Allocates an internal `WIDTH * HEIGHT` array of 16-bit values.
  - On boards with PSRAM, uses `ps_malloc`; otherwise falls back to the
    regular `malloc` heap.
  - Safe to call multiple times:
    - Any previous buffer is freed before a new one is allocated.
  - On success, immediately clears the buffer to the far depth value.
  - Returns `true` on success and `false` when allocation fails.
  - Marked with `[[nodiscard]]` (via `__attribute__((warn_unused_result))`)
    to encourage callers to check for allocation failures.

- **void clear()**
  - Fills the entire buffer with a special "clear" depth value representing
    empty space and no geometry.
  - Called at the start of each frame by `Renderer::beginFrame`.
  - Safe to call when the buffer is not yet allocated (no-op in that case).

- **bool testAndSet(x, y, z)**
  - Per-pixel depth test used during triangle rasterization.
  - Inputs:
    - `x, y` – pixel coordinates in framebuffer space.
    - `z` – depth value in the same range used by the projection
      matrices (typically `0..1` after perspective divide).
  - Behavior:
    - If `(x, y)` lies outside `[0, WIDTH) × [0, HEIGHT)`, returns `false`
      and leaves the buffer unchanged.
    - Converts `z` to a 16-bit fixed-point depth and compares it with the
      currently stored depth at `(x, y)` (ignoring the shadow flag).
    - If the new depth is **closer**, updates the depth at `(x, y)` while
      preserving any existing shadow flag and returns `true`.
    - Otherwise returns `false` and leaves the buffer as is.
  - Used heavily by `Rasterizer::fillTriangle*` and similar routines.

- **bool hasGeometry(x, y) const**
  - Returns `true` when the pixel at `(x, y)` contains any geometry (i.e. its
    depth is different from the cleared far value).
  - Returns `false` when out of bounds, the buffer is not allocated or the
    pixel is still in the cleared state.
  - Used primarily by the occlusion culling system
    (`Culling::isInstanceOccluded`).

- **bool hasShadow(x, y) const**
  - Checks whether the shadow flag is set for pixel `(x, y)`.
  - Returns `false` when out of bounds or when the buffer is not allocated.
  - Used by the shadow renderer to avoid over-applying shadows on the same
    pixel.

- **void markShadow(x, y)**
  - Sets the shadow flag for pixel `(x, y)` while keeping the stored depth
    unchanged.
  - Does nothing when out of bounds or when the buffer is not allocated.
  - Called by `Rasterizer::fillShadowTriangle` once a shadow contribution has
    been blended into the framebuffer.

- **void testAndSetScanline(y, x_start, x_end, z_start, z_step, frameBuffer, color)**
  - Optimized scanline version of `testAndSet` used for flat-colored
    triangles.
  - Inputs:
    - `y` – scanline index in framebuffer coordinates.
    - `x_start, x_end` – inclusive horizontal bounds of the scanline segment.
    - `z_start` – depth at `x_start`.
    - `z_step` – per-pixel depth increment along X.
    - `frameBuffer` – pointer to the RGB565 framebuffer.
    - `color` – 16-bit RGB565 color to write when a pixel passes the depth
      test.
  - Behavior:
    - Clips `x_start`/`x_end` to `[0, WIDTH)` and early-exits when the
      scanline is outside the buffer.
    - Iterates across the scanline, maintaining a fixed-point depth value.
    - For each pixel that is closer than the stored depth, updates the
      z-buffer (preserving any existing shadow flag) and writes `color` to
      the corresponding entry in `frameBuffer`.
    - The inner loop is unrolled in groups of four pixels for better
      performance on ESP32-S3.
  - Used by `Rasterizer::fillTriangle` for solid-colored geometry.

The renderer owns a single `ZBuffer<320, 240>` instance associated with the main
`FrameBuffer`. Application code normally does not interact with `ZBuffer`
directly; instead, it drives high-level rendering APIs (meshes, instances,
shadows) and lets the engine keep the depth buffer consistent.

---

## Rasterizer (Triangle Rasterization)

The low-level triangle rasterizer is defined in
`lib/Pip3D/rendering/raster/pip3D_rasterizer.h` and is used by
`MeshRenderer` and `ShadowRenderer` to convert projected triangles into
RGB565 pixels inside the main framebuffer.

```cpp
class Rasterizer
{
public:
    static void fillShadowTriangle(int16_t x0, int16_t y0,
                                    int16_t x1, int16_t y1,
                                    int16_t x2, int16_t y2,
                                    uint16_t shadowColor,
                                    uint8_t alpha,
                                    uint16_t* frameBuffer,
                                    ZBuffer<320, 240>* zBuffer,
                                    const DisplayConfig& config,
                                    bool softEdges = true);

    static void fillTriangleSmooth(int16_t x0, int16_t y0, float z0,
                                    int16_t x1, int16_t y1, float z1,
                                    int16_t x2, int16_t y2, float z2,
                                    float r0, float g0, float b0,
                                    float r1, float g1, float b1,
                                    float r2, float g2, float b2,
                                    uint16_t* frameBuffer,
                                    ZBuffer<320, 240>* zBuffer,
                                    const DisplayConfig& config);

    static void fillTriangle(int16_t x0, int16_t y0, float z0,
                              int16_t x1, int16_t y1, float z1,
                              int16_t x2, int16_t y2, float z2,
                              uint16_t color,
                              uint16_t* frameBuffer,
                              ZBuffer<320, 240>* zBuffer,
                              const DisplayConfig& config);
};
```

### Overview

- **Purpose**
  - Converts already projected triangles (screen-space X/Y, depth `z`) into
    RGB565 pixels in the main framebuffer.
  - Performs per-pixel depth testing against `ZBuffer<320, 240>`.
  - Provides three specialized paths:
    - planar shadow overlay (`fillShadowTriangle`),
    - Gouraud-shaded triangles with per-vertex colors (`fillTriangleSmooth`),
    - solid-colored triangles (`fillTriangle`).

-- **Inputs**
  - Geometry triangles (`fillTriangleSmooth`, `fillTriangle`):
    - `x0,y0,z0` .. `x2,y2,z2` – vertices in **screen space** after projection.
    - `x` and `y` are in framebuffer pixel coordinates.
    - `z` is the depth value used for z-buffer comparisons (typically `0..1`).
  - Shadow triangles (`fillShadowTriangle`):
    - `x0,y0` .. `x2,y2` – projected shadow hull in screen space.
    - Depth comes from existing geometry in `ZBuffer`; the function does not
      accept or modify `z` explicitly.
  - `frameBuffer`
    - Pointer to the main RGB565 framebuffer owned by `FrameBuffer`.
  - `zBuffer`
    - Pointer to the engine-wide `ZBuffer<320, 240>` instance.
  - `config`
    - Display resolution; only `width`/`height` are used here.

- **Safety**
  - All functions early-out when `frameBuffer == nullptr` or `zBuffer == nullptr`.
  - Y-coordinates are clipped to `[0, height)` per scanline.
  - X-coordinates are clipped to `[0, width)` before any framebuffer access.
  - Triangles that are fully degenerate in Y or X are skipped.

- **Usage pattern**
  - Application code normally does **not** call `Rasterizer` directly.
  - Use high-level APIs instead:
    - `MeshRenderer::drawMesh` / `drawTriangle3D*` for lit geometry.
    - `ShadowRenderer::drawMeshShadow` / `drawMeshInstanceShadow` for planar shadows.
  - Direct calls are possible for custom low-level effects (debug overlays,
    procedural geometry) when you already have projected vertices and access
    to the engine framebuffer and z-buffer.

### fillShadowTriangle

- **Purpose**
  - Applies a single planar shadow contribution over existing geometry.
  - Used exclusively by `ShadowRenderer` after the lit geometry pass.

- **Color and alpha**
  - `shadowColor`
    - Base RGB565 color of the shadow, usually pre-darkened by
      `ShadowRenderer` based on `ShadowSettings::shadowOpacity`.
  - `alpha`
    - 8-bit opacity in `[0, 255]` controlling how strongly the shadow
      darkens the background.
  - When `softEdges` is `true`, edge pixels use a reduced effective
    alpha to create a soft penumbra around the shadow hull.

- **Depth and masking**
  - The function does **not** modify the z-buffer values themselves.
  - Per-pixel conditions:
    - Skips pixels where `ZBuffer::hasGeometry(x, y) == false` (no
      geometry to receive a shadow).
    - Skips pixels where `ZBuffer::hasShadow(x, y) == true` to avoid
      double-darkening the same pixel.
    - Marks each affected pixel via `ZBuffer::markShadow(x, y)` after
      blending.

- **Blending model**
  - Background and shadow colors are expressed in RGB565 (R5 G6 B5) and
    blended per channel using the 8-bit `alpha` value.
  - Result is written back into `frameBuffer` while preserving the depth
    stored in the z-buffer.

### fillTriangleSmooth

- **Purpose**
  - Rasterizes a triangle with **per-vertex RGB colors** (Gouraud
    shading).
  - Used by `MeshRenderer::drawTriangle3DSmooth` for smoothly lit meshes.

- **Color inputs**
  - `r0,g0,b0` .. `r2,g2,b2`
    - Linear RGB components in `[0, 1]` computed by the lighting system.
    - Interpolated across the triangle in screen space.
  - For each pixel that passes the depth test, the interpolated color is
    converted to RGB565 via `Shading::applyDithering` (which also applies a
    small ordered dithering pattern using `BAYER_MATRIX_4X4`).

- **Depth handling**
  - Uses `ZBuffer::testAndSet(x, y, z)` per pixel:
    - Rejects pixels behind existing geometry.
    - Updates depth for closer pixels while preserving the shadow flag.

- **Performance notes**
  - All interpolation work is done with 32-bit floats, which are efficiently
    supported by ESP32-S3's FPU.
  - The function is fully inline-able and allocation-free; it operates only
    on stack variables and external buffers.

### fillTriangle

- **Purpose**
  - Rasterizes a **solid-colored** triangle.
  - Used by `MeshRenderer::drawTriangle3D` when per-vertex shading is not
    required.

- **Color input**
  - `color`
    - RGB565 value applied uniformly to every pixel that passes the depth
      test.

- **Depth handling**
  - Uses the optimized scanline helper
    `ZBuffer<320, 240>::testAndSetScanline(...)` for each affected
    horizontal span.
  - This minimizes per-pixel branching and leverages small unrolled inner
    loops for ESP32-S3.

---

## Shading (Lighting and Tone Mapping)

```cpp
class Shading
{
public:
    static constexpr float AMBIENT_LIGHT;
    static constexpr float DIFFUSE_STRENGTH;
    static constexpr float SPECULAR_STRENGTH;
    static constexpr float RIM_STRENGTH;
    static constexpr float HDR_EXPOSURE;
    static constexpr float DIFFUSE_WRAP;
    static constexpr float CONTRAST;
    static constexpr float SATURATION;

    static void calculateLighting(
        const Vector3& fragPos,
        const Vector3& normal,
        const Vector3& viewDir,
        const Light*   lights,
        int            lightCount,
        float          baseR, float baseG, float baseB,
        float&         outR,  float& outG,  float& outB);

    static uint16_t applyDithering(float r, float g, float b,
                                   int16_t x, int16_t y);
};
```

### Overview

- **Purpose**
  - Implements per-fragment lighting, rim lighting, tone mapping and simple
    color grading for all lit geometry.
  - Designed for ESP32-S3 without PSRAM: fully static, allocation-free and
    heavily inlined.
  - Consumes world-space fragment data and the engine `Light` array and
    produces linear RGB values in `[0, 1]` which are later converted to
    RGB565.

- **Coordinate space and inputs**
  - `fragPos`
    - World-space position of the shaded point (triangle centroid or vertex).
  - `normal`
    - Normalized world-space surface normal used for diffuse/specular/rim
      terms.
  - `viewDir`
    - Normalized direction from the fragment towards the active camera.
  - `lights`, `lightCount`
    - Pointer to a contiguous array of `Light` structs and the number of
      active lights to process (`[0, lightCount)`).
  - `baseR`, `baseG`, `baseB`
    - Base linear RGB albedo in `[0, 1]`, typically decoded from a mesh
      color (`Color::rgb565`) or material.

- **Lighting model (high level)**
  - Hemispherical ambient
    - Uses `AMBIENT_LIGHT` scaled by a hemisphere factor derived from
      `normal.y` so that upward-facing surfaces are slightly brighter than
      downward-facing ones.
  - Wrapped diffuse
    - Uses a modified Lambert term with `DIFFUSE_WRAP` to soften the
      lighting terminator and avoid hard bands, similar to wrap lighting in
      larger engines.
  - Blinn-Phong specular
    - Computes a half-vector between `lightDir` and `viewDir` and raises
      `max(dot(normal, halfway), 0)` to a high power via repeated squaring
      to approximate a tight highlight.
    - `SPECULAR_STRENGTH` controls the overall intensity of the specular
      lobe.
  - Rim lighting
    - Adds a view-dependent highlight based on
      `1 - dot(normal, viewDir)` squared, scaled by `RIM_STRENGTH`, to
      emphasize silhouettes.

- **Tone mapping and color grading**
  - HDR tone mapping
    - Applies a simple Reinhard-style curve per channel using
      `HDR_EXPOSURE`: `c = c * exposure / (1 + c * exposure)`.
  - Gamma correction
    - Uses `sqrtf` per channel as an inexpensive approximation of gamma
      ≈ 2.0.
  - Saturation
    - Computes luma (`0.299 * R + 0.587 * G + 0.114 * B`) and blends
      between luma and the original color using `SATURATION`.
  - Contrast
    - Recenters around 0.5 and applies `CONTRAST` as a simple linear
      contrast control.
  - Final clamp
    - Uses `clamp` to keep all outputs in `[0, 1]` before RGB565
      conversion.

### calculateLighting(...)

- **Signature**
  - `static void calculateLighting(const Vector3& fragPos,`
  - `                              const Vector3& normal,`
  - `                              const Vector3& viewDir,`
  - `                              const Light*   lights,`
  - `                              int            lightCount,`
  - `                              float          baseR, float baseG, float baseB,`
  - `                              float&         outR,  float& outG,  float& outB);`

- **Behavior**
  - Initializes `outR/G/B` with hemispherical ambient.
  - Iterates over `[0, lightCount)`:
    - Directional lights
      - Use the negated, normalized `Light::direction` as `lightDir`.
    - Point lights
      - Use `(light.position - fragPos)` and `Light::range` for
        attenuation.
      - Compute distance squared once and:
        - Skip fragments outside the range.
        - Normalize `lightDir` via `FastMath::fastInvSqrt` when
          distance is non-zero.
        - Compute a smooth falloff `1 / (1 + (d / range)^2)`.
    - Uses `Light::getCachedRGB` to fetch light color in `[0, 1]`.
    - Accumulates diffuse and specular contributions into `outR/G/B` in
      linear space.
  - Adds rim lighting, then applies tone mapping, gamma, saturation,
    contrast and clamp.

- **Performance notes**
  - Marked `always_inline, hot` and implemented in the header for maximal
    inlining in hot rasterization paths.
  - Uses squared distances and `FastMath::fastInvSqrt` to avoid redundant
    `sqrtf` calls in the point light path.
  - Contains no dynamic allocations or persistent state; safe for
    long-running use on ESP32-S3 without PSRAM.

### applyDithering(...)

- **Signature**
  - `static uint16_t applyDithering(float r, float g, float b,`
  - `                               int16_t x, int16_t y);`

- **Inputs**
  - `r, g, b`
    - Linear RGB components in `[0, 1]` after lighting and tone mapping.
  - `x, y`
    - Integer pixel coordinates in framebuffer space.

- **Behavior**
  - Converts `r, g, b` to the native RGB565 ranges (R5 G6 B5).
  - Looks up a small ordered-dithering offset from `BAYER_MATRIX_4X4`
    using `x & 3` and `y & 3` as indices.
  - Applies a tiny, channel-specific offset before rounding and clamps to
    the legal 5/6-bit ranges.
  - Packs the result into a single `uint16_t` in RGB565 format.
  - Used by `Rasterizer::fillTriangleSmooth` and by flat-shaded paths
    when they already have linear RGB values.

## Math Utilities (FastMath, Vector3, Matrix4x4, Quaternion)

This section describes the core math types defined in `lib/Pip3D/math/pip3D_math.h`.

They are used pervasively across the engine (camera, meshes, physics, culling) and are
designed to be small, inline‑friendly and efficient on ESP32‑S3 without PSRAM.

### Coordinate system and units

- Right‑handed world with **Y up**, **X right**, **Z forward** (into the screen).
- Distances and positions are expressed in arbitrary **world units** (meters‑like).
- Angles:
  - Low‑level math (`FastMath`, `Quaternion::fromEuler`) uses **radians**.
  - High‑level APIs (camera FOV, `Matrix4x4::setRotationX/Y/Z`, `MeshInstance::setEuler`)
    accept **degrees** and internally convert via `DEG2RAD`.

### FastMath

```cpp
class FastMath
{
public:
    static float fastSin(float angleRad);
    static float fastCos(float angleRad);
    static float fastInvSqrt(float x);
};
```

- **Purpose**
  - Provides fast approximations for sine, cosine and inverse square root.
  - Backed by small lookup tables (256 entries) and the classic Quake III `1/sqrt(x)`
    algorithm with two Newton iterations.

- **fastSin(angleRad) / fastCos(angleRad)**
  - Input is an angle in **radians**.
  - Internally normalizes the angle to `[0, 2π)` and samples the lookup table.
  - Suitable for animation, procedural geometry and camera motion where a small
    approximation error is acceptable.

- **fastInvSqrt(x)**
  - Returns an approximation of `1 / sqrt(x)` for positive `x`.
  - Used internally by `Vector3::normalize` and `Quaternion::normalize`.
  - Prefer this over repeated `sqrtf` calls when normalizing many vectors.

Typical usage:

```cpp
float angle = timeSeconds * 2.0f;      // radians
float s     = FastMath::fastSin(angle);
float c     = FastMath::fastCos(angle);
Vector3 dir = Vector3(c, 0.0f, s);     // unit circle in XZ plane
```

---

### Vector3

```cpp
struct Vector3
{
    float x, y, z;

    constexpr Vector3();
    constexpr Vector3(float x, float y, float z);

    Vector3  operator+(const Vector3& v) const;
    Vector3  operator-(const Vector3& v) const;
    Vector3  operator*(float s) const;
    Vector3& operator+=(const Vector3& v);
    Vector3& operator-=(const Vector3& v);
    Vector3& operator*=(float s);

    void     normalize();
    float    length() const;
    float    lengthSquared() const;
    float    dot(const Vector3& v) const;
    Vector3  cross(const Vector3& v) const;
};
```

- **Purpose**
  - Fundamental 3D vector type for positions, directions and normals.
  - Used everywhere: camera properties, mesh vertices, physics primitives, frustum, etc.

- **Arithmetic operators**
  - Standard component‑wise addition/subtraction and scalar multiplication.
  - Compound operators (`+=`, `-=`, `*=`) modify in place and return `*this`.

- **length() / lengthSquared()**
  - `lengthSquared()` avoids the square root and is preferred for comparisons
    (`distA2 < distB2`).
  - `length()` returns `0` for very small vectors to avoid numerical noise.

- **normalize()**
  - Normalizes the vector in place using `FastMath::fastInvSqrt`.
  - Does nothing when length is almost zero.

- **dot(v) / cross(v)**
  - `dot` returns the scalar product, used for angles, projections and backface tests.
  - `cross` returns a vector perpendicular to both inputs (right‑handed).

Typical usage:

```cpp
Vector3 up(0.0f, 1.0f, 0.0f);
Vector3 forward(0.0f, 0.0f, 1.0f);
Vector3 right = forward.cross(up);   // (1, 0, 0)

Vector3 velocity = forward * 3.0f;   // move at 3 units/sec along +Z
```

---

### Matrix4x4

```cpp
struct Matrix4x4
{
    float m[16];

    Matrix4x4();

    void      identity();
    Matrix4x4 operator*(const Matrix4x4& b) const;

    Vector3   transform(const Vector3& v) const;     // with perspective divide
    Vector3   transformNoDiv(const Vector3& v) const;
    Vector3   transformNormal(const Vector3& n) const;

    void      setPerspective(float fovDeg, float aspect,
                             float nearPlane, float farPlane);
    void      setOrthographic(float left, float right,
                              float bottom, float top,
                              float nearPlane, float farPlane);

    void      lookAt(const Vector3& eye,
                     const Vector3& target,
                     const Vector3& up);

    void      setTranslation(float x, float y, float z);
    void      setRotationX(float angleDeg);
    void      setRotationY(float angleDeg);
    void      setRotationZ(float angleDeg);
    void      setScale(float x, float y, float z);
};
```

- **Storage and layout**
  - 4×4 float matrix, 16‑byte aligned.
  - Layout matches the rest of the engine and is compatible with camera/view/projection
    matrices.

- **identity() / operator***
  - `identity()` resets the matrix to an identity transform.
  - Multiplication composes two transforms (`result = this * b`).

- **transform(v)**
  - Applies the full 4×4 transform to a position, including translation and
    perspective divide (`w` component).
  - Used by the renderer to convert world‑space vertices into clip/screen space.

- **transformNoDiv(v)**
  - Applies the affine part only (no divide by `w`).
  - Used for world‑space operations like transforming bounding sphere centers.

- **transformNormal(n)**
  - Transforms a direction vector by the upper‑left 3×3 submatrix and renormalizes.
  - Use this for normals instead of `transform()` to avoid translation.

- **setPerspective(fovDeg, aspect, nearPlane, farPlane)**
  - Builds a standard perspective projection matrix.
  - `fovDeg` is in **degrees**; other parameters are positive distances.

- **setOrthographic(left, right, bottom, top, nearPlane, farPlane)**
  - Builds an orthographic projection volume.
  - Used by the camera for orthographic views.

- **lookAt(eye, target, up)**
  - Builds a right‑handed view matrix from camera `eye`, `target` and `up` vectors.

Typical usage (manual camera matrix):

```cpp
Matrix4x4 view, proj, vp;
view.lookAt(cameraPos, cameraTarget, Vector3(0, 1, 0));
proj.setPerspective(60.0f, aspect, 0.1f, 100.0f);
vp = proj * view;
```

---

### Quaternion

```cpp
struct Quaternion
{
    float x, y, z, w;

    Quaternion();
    Quaternion(float x, float y, float z, float w);

    static Quaternion fromAxisAngle(const Vector3& axis, float angleRad);
    static Quaternion fromEuler(float pitchRad, float yawRad, float rollRad);

    Quaternion conjugate() const;
    void       normalize();

    Quaternion operator*(const Quaternion& q) const;

    Vector3    rotate(const Vector3& v) const;
    void       toMatrix(Matrix4x4& out) const;

    static Quaternion slerp(const Quaternion& a,
                            const Quaternion& b,
                            float t);
};
```

- **Purpose**
  - Compact rotation representation with no gimbal lock and smooth interpolation.
  - Used by `MeshInstance` for world‑space rotation and by camera utilities.

- **fromAxisAngle(axis, angleRad)**
  - Constructs a rotation around a normalized axis by `angleRad` radians.

- **fromEuler(pitchRad, yawRad, rollRad)**
  - Builds a quaternion from Euler angles in **radians**.
  - High‑level APIs like `MeshInstance::setEuler` accept degrees and convert before
    calling this function.

- **conjugate() / normalize()**
  - `conjugate` flips the vector part and leaves `w` unchanged (inverse for unit
    quaternions).
  - `normalize` rescales `(x, y, z, w)` to unit length using `FastMath::fastInvSqrt`.

- **operator*(q)**
  - Composes two rotations (`this` then `q`).
  - Commonly used for incremental rotation updates.

- **rotate(v)**
  - Rotates a vector by the quaternion, returning the transformed direction.

- **toMatrix(out)**
  - Writes the equivalent 3×3 rotation into the upper‑left part of `out`.
  - Used by instance transforms and camera matrices.

- **slerp(a, b, t)**
  - Spherical linear interpolation between rotations `a` and `b`.
  - Handles the shortest‑arc path automatically and falls back to linear interpolation
    when `a` and `b` are nearly identical.
  - `t` is in `[0, 1]`.

Typical usage (instance rotation):

```cpp
Quaternion start = Quaternion::fromAxisAngle(Vector3(0, 1, 0), 0.0f);
Quaternion end   = Quaternion::fromAxisAngle(Vector3(0, 1, 0), PI);

float t = 0.5f; // halfway
Quaternion mid = Quaternion::slerp(start, end, t);

Matrix4x4 rotM;
mid.toMatrix(rotM);
```

---

## Collision Primitives (AABB, CollisionSphere, Ray, CollisionPlane)

These low-level collision and intersection helpers are defined in
`lib/Pip3D/math/pip3D_collision.h`. They are used by the physics system and can also be
used directly in gameplay code for custom collision queries.

---

### AABB

```cpp
struct AABB
{
    Vector3 min, max;

    AABB();
    AABB(const Vector3& mn, const Vector3& mx);

    static AABB fromCenterSize(const Vector3& center, const Vector3& size);

    Vector3 center() const;
    Vector3 size() const;

    bool    contains(const Vector3& point) const;
    bool    intersects(const AABB& other) const;

    void    expand(const Vector3& point);
    void    merge(const AABB& other);
};
```

- **Purpose**
  - Axis-aligned bounding box in world space.
  - Used by `RigidBody` in the physics system for broad-phase overlap tests and by
    higher-level systems that need simple spatial bounds.

- **min, max**
  - World-space corners of the box.
  - `min` holds the smallest components, `max` the largest.

- **fromCenterSize(center, size)**
  - Builds an AABB from a center point and full extents.
  - Equivalent to `min = center - size * 0.5f`, `max = center + size * 0.5f`.

- **center() / size()**
  - Convenience accessors returning the box center and extents.

- **contains(point)**
  - Returns `true` if the point lies inside or on the box (inclusive checks).

- **intersects(other)**
  - Fast AABB–AABB overlap test; returns `true` when intervals overlap on all axes.

- **expand(point)**
  - Grows the box so that it also contains `point`.
  - Useful when building bounds from a stream of positions.

- **merge(other)**
  - Expands this box to contain `other` as well.
  - Can be used to accumulate world bounds of multiple objects.

---

### CollisionSphere

```cpp
struct CollisionSphere
{
    Vector3 center;
    float   radius;

    CollisionSphere();
    CollisionSphere(const Vector3& c, float r);

    bool contains(const Vector3& point) const;
    bool intersects(const CollisionSphere& other) const;
    bool intersects(const AABB& box) const;
};
```

- **Purpose**
  - Simple bounding sphere primitive used by physics and culling code.
  - Ideal for cheap radial checks and broad-phase tests.

- **center, radius**
  - World-space center and radius of the sphere.

- **contains(point)**
  - Returns `true` if the point is inside or on the sphere (squared-distance check).

- **intersects(other)**
  - Sphere–sphere overlap test using squared distances.

- **intersects(box)**
  - Sphere–AABB overlap test: clamps the center to the box and compares squared distance
    to `radius^2`.

---

### Ray

```cpp
struct Ray
{
    Vector3 origin;
    Vector3 direction;

    Ray();
    Ray(const Vector3& o, const Vector3& d);

    Vector3 at(float t) const;

    bool intersects(const AABB& box, float& tMin, float& tMax) const;
    bool intersects(const CollisionSphere& sphere, float& t) const;
};
```

- **Purpose**
  - Parametric ray `origin + direction * t` used for continuous collision checks.
  - Physics uses it internally for swept sphere–sphere and sphere–box tests.

- **origin, direction**
  - World-space ray origin and direction (not required to be normalized).

- **at(t)**
  - Evaluates the point along the ray at parameter `t`.

- **intersects(box, tMin, tMax)**
  - Ray–AABB intersection using the standard slab method.
  - On success, writes the entering (`tMin`) and exiting (`tMax`) parameters along the ray
    and returns `true`.
  - Works with non-normalized directions; `t` is measured in units of the ray parameter
    such that `origin + direction * t` is the hit point.

- **intersects(sphere, t)**
  - Ray–sphere intersection based on the quadratic solution.
  - Returns the nearest non-negative intersection parameter in `t` when a hit occurs.
  - If the ray origin lies inside the sphere and the direction is effectively zero-length,
    the function reports a hit with `t = 0`.

---

### CollisionPlane

```cpp
struct CollisionPlane
{
    Vector3 normal;
    float   distance;

    CollisionPlane();
    CollisionPlane(const Vector3& n, float d);
    CollisionPlane(const Vector3& n, const Vector3& point);

    float distanceToPoint(const Vector3& point) const;
    bool  intersects(const CollisionSphere& sphere) const;
};
```

- **Purpose**
  - General plane primitive for simple plane–point and plane–sphere tests.
  - Suitable for ground planes, triggers and simple clipping volumes.

- **normal, distance**
  - `normal` is stored normalized.
  - `distance` is the signed distance from the origin along `normal` to the plane
    (in the equation `normal · p - distance = 0`).

- **distanceToPoint(point)**
  - Signed distance from `point` to the plane.
  - Negative values mean the point lies behind the plane, positive in front.

- **intersects(sphere)**
  - Returns `true` if a sphere intersects or touches the plane.

---

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

## Physics System (PhysicsMaterial, RigidBody, CollisionInfo, PhysicsWorld)

The physics system provides a lightweight rigid body simulation suitable for ESP32-S3
without PSRAM. It is defined in `lib/Pip3D/physics/pip3D_physics.h` and focuses on box
and sphere shapes, fast broad-phase checks and an impulse-based contact solver.

Physics is fully optional: you can construct and step `PhysicsWorld` manually, or
integrate it with the job system to run simulation asynchronously on the second core.

---

### PhysicsMaterial

```cpp
struct PhysicsMaterial
{
    float friction;
    float restitution;

    PhysicsMaterial();
    PhysicsMaterial(float f, float r);
};
```

- **Purpose**
  - Describes basic surface properties used during collision resolution.
  - Combined per-contact via `min(friction)` and `min(restitution)` of the two bodies.

- **friction**
  - Dimensionless coefficient used for Coulomb-like friction limits.
  - Typical values: `0.0f` (no friction) to `1.0f` (very sticky).

- **restitution**
  - Bounciness coefficient used in the normal impulse computation.
  - `0.0f` – inelastic collisions; `1.0f` – perfectly elastic.

- **Constructors**
  - `PhysicsMaterial()`
    - Initializes `friction = 0.5f`, `restitution = 0.5f`.
  - `PhysicsMaterial(f, r)`
    - Directly sets `friction` and `restitution`.

Use `PhysicsMaterial` to quickly define materials for floors, walls, props or characters
and apply them to rigid bodies via `RigidBody::setMaterial`.

---

### RigidBody

```cpp
enum BodyShape
{
    BODY_SHAPE_BOX   = 0,
    BODY_SHAPE_SPHERE = 1
};

struct RigidBody
{
    Vector3   position;
    Vector3   previousPosition;
    Vector3   velocity;
    Vector3   acceleration;
    Vector3   angularVelocity;
    Quaternion orientation;
    Vector3   size;
    float     mass;
    float     invMass;
    float     restitution;
    float     friction;
    bool      isStatic;
    BodyShape shape;
    float     radius;
    Vector3   invInertia;
    AABB      bounds;
    bool      canSleep;
    bool      isSleeping;
    float     sleepTimer;

    RigidBody();
    RigidBody(const Vector3& pos, const Vector3& size, float m = 1.0f);

    void setBox(const Vector3& newSize);
    void setSphere(float r);

    void applyForce(const Vector3& force);
    void applyGravity(float gravityValue = -9.81f);

    void update(float deltaTime);

    void setPosition(const Vector3& pos);
    void setStatic(bool s);
    void wakeUp();
    void setCanSleep(bool value);
    void setMaterial(const PhysicsMaterial& m);

    void updateBoundsFromTransform();
};
```

#### Purpose and representation

- **RigidBody**
  - Simulated rigid body with position, orientation, linear and angular velocity.
  - Supports two primitive body shapes:
    - `BODY_SHAPE_BOX` – oriented box defined by `size` and `orientation`.
    - `BODY_SHAPE_SPHERE` – sphere defined by `position` and `radius`.
  - Stores an axis-aligned bounding box `bounds` used for broad-phase overlap tests.

- **Mass and inertia**
  - `mass` / `invMass`
    - `mass > 0` enables dynamics; `mass <= 0` behaves as infinite mass.
    - Internal `invMass` is derived from `mass` and used in impulse calculations.
  - `invInertia`
    - Approximate inverse inertia tensor stored as diagonal components `(ix, iy, iz)`.
    - Computed automatically from `shape`, `size`, `mass` or `radius`.

- **Sleeping**
  - `canSleep`, `isSleeping`, `sleepTimer`
    - Used by `PhysicsWorld` to skip integration for nearly static bodies.
    - Sleeping bodies keep their transform but do not accumulate forces or move.

#### Construction and shape setup

- **RigidBody()**
  - Creates a unit box at the origin with `mass = 1.0f`, `size = (1,1,1)`.
  - Default material: `friction = 0.5f`, `restitution = 0.5f`.
  - `isStatic = false`, `canSleep = true`.

- **RigidBody(pos, size, m)**
  - Initializes a box at `pos` with extents `size` and mass `m`.
  - Automatically computes `invMass`, `invInertia` and `bounds`.

- **setBox(newSize)**
  - Configures the body as an oriented box with world-space size `newSize`.
  - Updates `shape`, `size`, `bounds` and inertia.

- **setSphere(r)**
  - Configures the body as a sphere with radius `r`.
  - Updates `shape`, `radius`, `size` (diameter on all axes), `bounds` and inertia.

#### Forces, integration and transforms

- **applyForce(force)**
  - Adds a world-space force for the next integration step.
  - Ignored for static bodies and for bodies with non-positive `mass`.

- **applyGravity(gravityValue)**
  - Adds a vertical acceleration `gravityValue` along the Y axis (default `-9.81f`).
  - Typically called by `PhysicsWorld` to apply global gravity.

- **update(deltaTime)**
  - Integrates linear and angular motion over `deltaTime` seconds.
  - Applies linear and angular damping, clamps maximum velocities and normalizes
    orientation.
  - Updates `position`, `orientation`, `bounds` and clears accumulated acceleration.
  - Does nothing for static or sleeping bodies.

- **setPosition(pos)**
  - Sets world-space position, resets `previousPosition` and updates `bounds`.
  - Wakes the body by clearing sleep state.

- **setStatic(s)**
  - Toggles `isStatic`.
  - When set to `true`, resets velocities and acceleration, recomputes inertia and
    disables dynamic motion.

#### Sleeping and material control

- **wakeUp()**
  - Clears `isSleeping` and `sleepTimer` if the body is currently sleeping.

- **setCanSleep(value)**
  - Enables or disables automatic sleeping.
  - When disabled, immediately wakes the body.

- **setMaterial(m)**
  - Copies `friction` and `restitution` from `m` into the body.

- **updateBoundsFromTransform()**
  - Rebuilds `bounds` from the current `position`, `orientation` and `size`.
  - Used internally after integration and when changing transforms.

Use `RigidBody` directly when you need fine-grained control over a few dynamic or
static objects (player, enemies, moving platforms, simple props).

---

### CollisionInfo

```cpp
struct CollisionInfo
{
    bool      hasCollision;
    Vector3   normal;
    float     penetration;
    Vector3   contactPoint;
    RigidBody* bodyA;
    RigidBody* bodyB;

    CollisionInfo();
};
```

- **Purpose**
  - Describes a single contact between two rigid bodies.
  - Used internally by `PhysicsWorld` and exposed for debugging or custom logic.

- **Fields**
  - `hasCollision`
    - `true` when a valid contact exists; `false` for the default-initialized state.
  - `normal`
    - Contact normal pointing from `bodyA` towards `bodyB`.
  - `penetration`
    - Overlap depth along `normal` in world units.
  - `contactPoint`
    - Approximate world-space contact point between the two shapes.
  - `bodyA`, `bodyB`
    - Pointers to the rigid bodies involved in the contact.

`CollisionInfo` instances are produced by the internal `detectCollision` method during
the solver loop and then passed to `resolveCollision`.

---

### PhysicsWorld

```cpp
class PhysicsWorld
{
public:
    PhysicsWorld();

    bool  addBody(RigidBody* body);
    void  removeBody(RigidBody* body);

    void  setGravity(const Vector3& g);

    void  setAsyncEnabled(bool enabled);
    bool  isAsyncEnabled() const;
    bool  isStepInProgress() const;

    void  setFixedTimeStep(float dt);
    float getFixedTimeStep() const;

    void  updateFixed(float frameDelta);
    void  stepAsync(float deltaTime);
};
```

#### Purpose and responsibilities

- **PhysicsWorld**
  - Owns a list of `RigidBody*` and advances the simulation in fixed or variable
    time steps.
  - Applies global gravity, detects collisions and resolves contacts via an
    impulse-based solver.
  - Optionally uses `JobSystem` to execute simulation steps asynchronously on
    the worker core.

#### Body management

- **addBody(body)**
  - Registers `body` for simulation.
  - Returns `false` if `body` is `nullptr`, `true` otherwise.
  - Does not take ownership: the caller is responsible for allocating and
    destroying the `RigidBody` instance and for removing it from the world
    before destruction.

- **removeBody(body)**
  - Unregisters `body` in O(1) time by swapping with the last entry.
  - Safe to call multiple times; does nothing if `body` is not found.

#### Global parameters

- **setGravity(g)**
  - Sets the world gravity vector used during the next simulation steps.
  - Default value is `(0, -9.81f, 0)`.

- **setFixedTimeStep(dt)** / **getFixedTimeStep()**
  - Controls the fixed update interval used by `updateFixed`.
  - `dt <= 0` disables fixed stepping and makes `updateFixed` perform a single
    variable-step update per call.

#### Fixed-step update and async stepping

- **updateFixed(frameDelta)**
  - Preferred entry point for time integration from the main loop.
  - Accumulates `frameDelta` and performs one or more simulation steps of duration
    `getFixedTimeStep()` while keeping the accumulator clamped to a small multiple
    of the step size.
  - When async mode is enabled, schedules steps via `stepAsync`; otherwise runs
    them synchronously on the calling thread.

- **setAsyncEnabled(enabled)** / **isAsyncEnabled()**
  - Controls whether physics steps may run on the job system.
  - `isAsyncEnabled()` also checks `JobSystem::isEnabled()` to ensure that the
    worker is available before taking the asynchronous path.

- **isStepInProgress()**
  - Returns `true` while an asynchronous step scheduled via `stepAsync` is still
    running.
  - Can be used by game code to avoid overlapping updates.

- **stepAsync(deltaTime)**
  - Schedules a single simulation step with duration `deltaTime`.
  - When async mode or the job system is disabled, falls back to an immediate
    synchronous call to the internal step function.
  - When async mode is enabled and no other step is in progress, submits a job
    to `JobSystem` and marks the world as busy until the job completes.

##### Notes on thread-safety and lifetime

- `PhysicsWorld` itself is not thread-safe:
  - Do not call `addBody`, `removeBody` or destroy bodies referenced by the
    world while an asynchronous step is in progress.
  - Ensure that the world outlives any in-flight jobs scheduled via
    `stepAsync` (for example by shutting down physics before application exit).

#### Contact generation and resolution (internal behavior)

Although not exposed as public methods, the following behaviors are important
to understand simulation results:

- **Collision detection**
  - Uses `AABB::intersects` as a broad-phase filter.
  - Supports sphere–sphere, sphere–box and box–box (OBB–OBB) contacts.
  - Sphere–sphere and sphere–box contacts also include a simple continuous
    collision detection path based on swept rays.

- **Solver iterations**
  - Uses a fixed number of solver iterations per step for resolving contacts.
  - Each iteration loops over all body pairs and applies impulses when a
    `CollisionInfo` reports an overlap.

- **Sleeping and stabilization**
  - Very slow-moving box bodies can be put to sleep to save CPU time.
  - Additional stabilization logic gently snaps resting boxes to an upright
    pose when they are already nearly aligned with the Y axis and close to the
    ground.

This physics system is designed for small to medium numbers of bodies (tens of
objects) and simple shapes, prioritizing predictability and low RAM usage over
complex features.

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

---

## Lighting and LightManager

```cpp
enum LightType
{
    LIGHT_DIRECTIONAL,
    LIGHT_POINT
};

struct Light
{
    LightType type;
    float     intensity;
    Vector3   direction;
    Vector3   position;
    Color     color;
    float     range;
    mutable float cachedR, cachedG, cachedB;
    mutable bool  colorCacheDirty;

    void getCachedRGB(float& r, float& g, float& b) const;
};

class LightManager
{
public:
    static void   setLight(std::vector<Light>& lights, int& activeLightCount, int index, const Light& light);
    static int    addLight(std::vector<Light>& lights, int& activeLightCount, const Light& light);
    static void   removeLight(std::vector<Light>& lights, int& activeLightCount, int index);
    static Light* getLight(std::vector<Light>& lights, int activeLightCount, int index);
    static void   clearLights(int& activeLightCount);
    static int    getLightCount(int activeLightCount);

    static void   setMainDirectionalLight(std::vector<Light>& lights, int& activeLightCount,
                                          const Vector3& direction, const Color& color, float intensity = 1.0f);

    static void   setMainPointLight(std::vector<Light>& lights, int& activeLightCount,
                                    const Vector3& position, const Color& color,
                                    float intensity = 1.0f, float range = 10.0f);

    static void   setLightColor(std::vector<Light>& lights, int activeLightCount, const Color& color);
    static void   setLightPosition(std::vector<Light>& lights, int activeLightCount, const Vector3& pos);
    static void   setLightDirection(std::vector<Light>& lights, int activeLightCount, const Vector3& dir);
    static void   setLightTemperature(std::vector<Light>& lights, int activeLightCount, float kelvin);
    static Color  getLightColor(const std::vector<Light>& lights, int activeLightCount);
    static void   setLightType(std::vector<Light>& lights, int activeLightCount, LightType type);
};

### Overview

- **Purpose**
  - Defines the runtime representation of lights (`Light`, `LightType`) and a
    minimal array manager (`LightManager`) used internally by `Renderer`.
  - Keeps a dense array of active lights with an explicit `activeLightCount`
    instead of reallocating on every add/remove.
  - Integrates with the shading pipeline (`Shading::calculateLighting`) and the
    shadow renderer, which always iterate only over
    `[0, activeLightCount)`.

- **Main light vs additional lights**
  - Slot `lights[0]` is treated as the **main light** (usually the sun).
  - Helper methods that do not take an explicit index operate only on this
    main light.
  - Additional lights are managed via indexed operations and contribute to the
    lighting of all shaded geometry.

- **Typical high‑level usage**
  - Application code usually talks to `Renderer`:
    - `setMainDirectionalLight(direction, color, intensity)`
    - `setMainPointLight(position, color, intensity, range)`
    - `setLightColor(color)`, `setLightPosition(pos)`, `setLightDirection(dir)`,
      `setLightTemperature(kelvin)`, `setLightType(type)`
    - `setLight(index, light)`, `addLight(light)`, `removeLight(index)`,
      `Light* getLight(index)`, `int getLightCount()`
  - Internally, these methods forward to `LightManager` with the renderer‑owned
    `std::vector<Light>` and `activeLightCount`.

### Light and LightType

- **LightType**
  - `LIGHT_DIRECTIONAL`
    - Parallel light from an infinite source (sun‑like).
    - Direction taken from `Light::direction`; normalized by the engine when
      configuring the main light.
  - `LIGHT_POINT`
    - Omnidirectional point light.
    - Origin in `Light::position`, optional falloff controlled by `Light::range`.

- **Light fields (runtime representation)**
  - `type`
    - One of `LightType` values; determines how the light is handled by
      shading and shadow systems.
  - `intensity`
    - Unitless brightness multiplier tuned for ESP32‑S3 HDR tone‑mapping in
      `Shading::calculateLighting`.
  - `direction`
    - Direction vector for directional lights.
  - `position`
    - World‑space origin for point lights.
  - `color`
    - Base color stored as `Color` (`rgb565` internally), consistent with the
      rest of the engine.
  - `range`
    - Effective radius for point lights. When zero, the light has no
      distance‑based falloff.
  - `cachedR`, `cachedG`, `cachedB`
    - Cached linearized RGB components in `[0, 1]` derived from `color.rgb565`.
    - Updated lazily by `Light::getCachedRGB` to avoid repeated bit‑unpacking
      inside the inner lighting loops.
  - `colorCacheDirty`
    - Marks whether the cached RGB components are out of date.
    - Set by `LightManager` and other helpers whenever `color` changes.

- **Light::getCachedRGB(r, g, b)**
  - Decodes `color.rgb565` into normalized floats `r`, `g`, `b` in `[0, 1]`.
  - Recomputes and stores the channels only when `colorCacheDirty` is `true`.
  - Used heavily by `Shading::calculateLighting` and is marked `always_inline`
    in the header to keep per‑pixel lighting as cheap as possible on ESP32‑S3.

### LightManager API

- **`setLight(lights, activeLightCount, index, light)`**
  - Writes `light` into `lights[index]`, resizing the underlying vector when
    `index` is beyond the current size.
  - Marks the written light color cache as dirty so that `Light::getCachedRGB`
    recomputes channels on next use.
  - Updates `activeLightCount` so that it always covers the highest written
    index.

- **`addLight(lights, activeLightCount, light)`**
  - Adds a new light at the end of the active range.
  - Reuses existing storage when `activeLightCount < lights.size()`, otherwise
    performs a single `push_back`.
  - Marks the new light cache as dirty and returns its index, then increments
    `activeLightCount`.

- **`removeLight(lights, activeLightCount, index)`**
  - Removes the light at `index` from the active range.
  - When removing the last active light, simply decrements `activeLightCount`.
  - Otherwise compacts lights by shifting `[index + 1, activeLightCount)` left
    by one to keep the active range dense.

- **`getLight(lights, activeLightCount, index)`**
  - Returns a pointer to `lights[index]` if `index` lies inside
    `[0, activeLightCount)`, otherwise returns `nullptr`.

- **`clearLights(activeLightCount)` / `getLightCount(activeLightCount)`**
  - `clearLights` sets `activeLightCount` to zero without touching the
    underlying `std::vector<Light>`; capacity is preserved for reuse.
  - `getLightCount` returns the number of currently active lights and is used
    as the upper bound for all lighting/shadow loops.

### Main light helpers

- **`setMainDirectionalLight(lights, activeLightCount, direction, color, intensity)`**
  - Ensures at least one active light, then configures `lights[0]` as a
    directional light.
  - Normalizes `direction` and sets `color` and `intensity`.
  - Marks the color cache as dirty to reflect the new color.

- **`setMainPointLight(lights, activeLightCount, position, color, intensity, range)`**
  - Ensures at least one active light, then configures `lights[0]` as a point
    light.
  - Sets `position`, `color`, `intensity` and `range`, invalidating the color
    cache.

- **`setLightColor(lights, activeLightCount, color)`**
  - Changes the color of the main light when at least one light is active and
    the array is non‑empty.

- **`setLightPosition(lights, activeLightCount, pos)` / `setLightDirection(lights, activeLightCount, dir)`**
  - Update position and direction of the main light.
  - For directions the vector is normalized to keep lighting stable.

- **`setLightTemperature(lights, activeLightCount, kelvin)`**
  - Converts color temperature in Kelvin to `Color` via `Color::fromTemperature`
    and applies it to the main light.
  - Used indirectly by `Renderer::setSkyboxWithLighting` to keep sky and sun
  - color consistent.

- **`getLightColor(lights, activeLightCount)`**
  - Returns the color of the main light, or `Color::WHITE` when there are no
    active lights or the light array is empty.

- **`setLightType(lights, activeLightCount, type)`**
  - Changes the `LightType` of the main light if at least one light is active
    and the array is non‑empty.

Together, `Light`, `LightType` and `LightManager` form the low‑level lighting API
for the engine. Typical user code works with higher‑level `Renderer` helpers,
while custom tools and advanced scenarios can access and manipulate light arrays
directly when fine‑grained control is required.

---

## ShadowProjector, ShadowSettings and ShadowRenderer

Planar shadows are implemented as a lightweight, GPU‑less system that projects
geometry onto a configurable plane and blends a darkened footprint into the
framebuffer. The public API is defined in
`lib/Pip3D/rendering/lighting/pip3D_shadow.h` and
`lib/Pip3D/rendering/lighting/pip3D_shadow_renderer.h`.

```cpp
class ShadowProjector
{
public:
    struct ShadowPlane
    {
        Vector3 normal;  // Normalized plane normal
        float   d;       // Plane offset in n.x*x + n.y*y + n.z*z + d = 0

        ShadowPlane();
        ShadowPlane(const Vector3& n, float distance);

        static ShadowPlane fromPointAndNormal(const Vector3& point,
                                              const Vector3& normal);
    };
};

struct ShadowSettings
{
    bool       enabled;
    Color      shadowColor;
    float      shadowOpacity;
    float      shadowOffset;
    bool       softEdges;
    ShadowProjector::ShadowPlane plane;

    ShadowSettings();
};

class ShadowRenderer
{
public:
    static void drawMeshShadow(Mesh* mesh,
                               bool shadowsEnabled,
                               const ShadowSettings& shadowSettings,
                               const Camera& camera,
                               const Light* lights,
                               int activeLightCount,
                               const Matrix4x4& viewProjMatrix,
                               const Viewport& viewport,
                               FrameBuffer& framebuffer,
                               ZBuffer<320, 240>* zBuffer,
                               bool& backfaceCullingEnabled);

    static void drawMeshInstanceShadow(MeshInstance* instance,
                                       bool shadowsEnabled,
                                       const ShadowSettings& shadowSettings,
                                       const Camera& camera,
                                       const Light* lights,
                                       int activeLightCount,
                                       const Matrix4x4& viewProjMatrix,
                                       const Viewport& viewport,
                                       FrameBuffer& framebuffer,
                                       ZBuffer<320, 240>* zBuffer,
                                       bool& backfaceCullingEnabled);
};

### ShadowProjector and ShadowPlane

- **Purpose**
  - `ShadowPlane` describes the receiver plane for planar shadows.
  - Used by `Renderer` and `ShadowRenderer` to determine where projected
    footprints are placed in world space.

- **ShadowPlane::normal, d**
  - `normal`
    - Plane normal in world space; normalized on construction.
    - The engine typically uses `(0, 1, 0)` for a horizontal ground plane.
  - `d`
    - Plane offset in the standard plane equation
      `normal.x * x + normal.y * y + normal.z * z + d = 0`.
    - For a horizontal plane at world Y = `y0`, the engine uses
      `normal = (0, 1, 0)` and `d = -y0`.

- **ShadowPlane::fromPointAndNormal(point, normal)**
  - Helper that constructs a normalized plane from any world‑space point lying
    on the plane and an (optionally unnormalized) normal.
  - Convenient for custom receivers such as platforms or tilted floors.

### ShadowSettings

- **Purpose**
  - Runtime configuration block for the shadow system, owned by `Renderer`.
  - Encapsulates enable flags, color/opacity and receiver plane in a single
    struct that can be stored, tweaked and reused.

- **Fields**
  - `enabled`
    - Master switch for rendering shadows. When `false`, shadow rendering code
      early‑outs without doing any work.
  - `shadowColor`
    - Base RGB565 color of the shadow in world space.
    - Typically a very dark blue‑gray (default: `Color::fromRGB888(20, 20, 30)`).
  - `shadowOpacity`
    - Opacity factor in `[0, 1]` used both for darkening `shadowColor` and for
      the alpha value passed into `Rasterizer::fillShadowTriangle`.
    - Values are clamped to `[0, 1]` internally by the shadow renderer.
  - `shadowOffset`
    - Small offset added along the plane normal (for the default horizontal
      plane this is +Y) to lift the shadow slightly above the receiver.
    - Prevents z‑fighting and ensures that the shadow remains visible even when
      geometry lies exactly on the plane.
  - `softEdges`
    - Enables simple edge softening inside `Rasterizer::fillShadowTriangle`.
    - When `true`, border pixels use a reduced alpha based on their distance to
      the triangle hull, producing a subtle penumbra.
  - `plane`
    - Receiver plane described by `ShadowProjector::ShadowPlane`.
    - By default set to a horizontal plane at `Y = 0`.

- **Construction**
  - `ShadowSettings()`
    - Initializes sensible defaults for ground‑plane shadows:
      - `enabled = true`
      - `shadowColor = Color::fromRGB888(20, 20, 30)`
      - `shadowOpacity = 0.7f`
      - `shadowOffset = 0.01f`
      - `softEdges = true`
      - `plane = ShadowPlane(Vector3(0, 1, 0), 0)` (Y = 0 ground)

In most applications you work with `ShadowSettings` indirectly via
high‑level `Renderer` methods:

- `setShadowsEnabled(bool enabled)`
- `setShadowOpacity(float opacity)`
- `setShadowColor(const Color& color)`
- `setShadowPlane(const Vector3& normal, float distance)`
- `setShadowPlaneY(float y)`
- `ShadowSettings& getShadowSettings()` to tweak `shadowOffset` / `softEdges`
  directly when needed.

### ShadowRenderer

- **Purpose**
  - Implements the low‑level planar shadow pass for both raw meshes and
    `MeshInstance`s.
  - Consumes `ShadowSettings`, the main light, camera matrices, viewport,
    framebuffer and z‑buffer to draw projected “blob” shadows.
  - Designed specifically for ESP32‑S3 without PSRAM: no dynamic allocations in
    the hot path, all work is done with stack locals and existing engine
    buffers.

- **Light handling**
  - Only the first light in the renderer’s light array (`lights[0]`) is used
    for shadows.
  - Supported `LightType` values:
    - `LIGHT_DIRECTIONAL` — casts parallel shadows using the light direction.
    - `LIGHT_POINT` — projects geometry from the point light position onto the
      receiver plane.
  - If there are no active lights or the main light is of another type,
    shadow rendering is skipped.

- **Shadow projection model**
  - For **directional lights**:
    - Each triangle vertex is projected along the (normalized) light direction
      onto the plane defined by `ShadowSettings::plane`.
    - The implementation clamps extremely shallow light angles to avoid
      numerical issues, preserving stable shadows even when the sun is near
      the horizon.
  - For **point lights**:
    - Each vertex is projected along the ray from the light position to the
      vertex.
    - When the ray is nearly parallel to the plane normal (very small Y
      component for the default plane), a safe fallback places the projected
      point directly above/below the original vertex on the receiver plane.

- **Color and opacity**
  - Internally `ShadowRenderer` derives a darkened shadow color and an 8‑bit
    alpha from `ShadowSettings::shadowColor` and `shadowOpacity`:
    - The RGB565 channels are scaled by `shadowOpacity`.
    - The same factor is converted to `[0, 255]` and passed as the base alpha
      to `Rasterizer::fillShadowTriangle`.
  - This computation is shared between mesh and instance paths for maximum
    consistency and minimal overhead.

- **Z‑buffer and overdraw avoidance**
  - Uses `ZBuffer<320, 240>::hasGeometry(x, y)` to skip pixels that have no
    rendered geometry yet.
  - Uses `ZBuffer<320, 240>::hasShadow(x, y)` / `markShadow(x, y)` to ensure
    that each pixel receives at most one shadow contribution per frame.
  - This keeps the shadow pass cheap even for scenes with many overlapping
    projected triangles.

- **Integration with the renderer**
  - You normally do not call `ShadowRenderer` directly. Instead use
    high‑level helpers:
    - `Renderer::setShadowsEnabled(bool)`
    - `Renderer::setShadowPlaneY(float)` and related configuration
    - `Renderer::drawMeshShadow(Mesh*)`
    - `Renderer::drawMeshInstanceShadow(MeshInstance*)`
  - Utility helpers such as `ObjectHelper::renderWithShadow` can draw both the
    shadow and the lit mesh in one call.

This planar shadow system is deliberately simple and deterministic, making it a
good fit for real‑time 3D on microcontrollers while still following
industry‑standard concepts familiar from larger engines like Unreal Engine.
