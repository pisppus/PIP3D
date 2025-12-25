#ifndef CORE_H
#define CORE_H

#include "../Math/Math.h"
#include <Arduino.h>
#include <SPI.h>
#include <cstdlib>
#include <cstring>
#include "Debug/Logging.h"

namespace pip3D
{

  template <typename T>
  __attribute__((always_inline)) inline constexpr T clamp(T value, T min_val, T max_val)
  {
    return value < min_val ? min_val : (value > max_val ? max_val : value);
  }

#ifndef likely
#define likely(x) __builtin_expect(!!(x), 1)
#endif
#ifndef unlikely
#define unlikely(x) __builtin_expect(!!(x), 0)
#endif

  struct alignas(16) Display
  {
    uint16_t width = 240, height = 320;
    int8_t cs = 10, dc = 9, rst = 8, bl = -1;
    uint32_t spi_freq = 80000000;

    Display() = default;
    Display(uint16_t w, uint16_t h) : width(w), height(h) {}
    Display(uint16_t w, uint16_t h, int8_t cs_, int8_t dc_, int8_t rst_)
        : width(w), height(h), cs(cs_), dc(dc_), rst(rst_) {}
  };

  struct alignas(2) Color
  {
    uint16_t rgb565;

    constexpr Color() : rgb565(0) {}
    constexpr Color(uint16_t c) : rgb565(c) {}
    constexpr Color(uint8_t r, uint8_t g, uint8_t b) : rgb565(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)) {}

    static constexpr Color rgb(uint8_t r, uint8_t g, uint8_t b)
    {
      return Color(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
    }

    static constexpr Color fromRGB888(uint8_t r, uint8_t g, uint8_t b) { return rgb(r, g, b); }
    static Color fromTemperature(float k) { return temp(k); }

    static Color hsv(float h, float s, float v)
    {
      h -= floorf(h);
      const float h6 = h * 6.0f;
      const int i = static_cast<int>(h6);
      const float f = h6 - static_cast<float>(i);
      const float p = v * (1.0f - s);
      const float q = v * (1.0f - f * s);
      const float t = v * (1.0f - (1.0f - f) * s);

      switch (i % 6)
      {
      case 0:
        return rgb(static_cast<uint8_t>(v * 255.0f),
                   static_cast<uint8_t>(t * 255.0f),
                   static_cast<uint8_t>(p * 255.0f));
      case 1:
        return rgb(static_cast<uint8_t>(q * 255.0f),
                   static_cast<uint8_t>(v * 255.0f),
                   static_cast<uint8_t>(p * 255.0f));
      case 2:
        return rgb(static_cast<uint8_t>(p * 255.0f),
                   static_cast<uint8_t>(v * 255.0f),
                   static_cast<uint8_t>(t * 255.0f));
      case 3:
        return rgb(static_cast<uint8_t>(p * 255.0f),
                   static_cast<uint8_t>(q * 255.0f),
                   static_cast<uint8_t>(v * 255.0f));
      case 4:
        return rgb(static_cast<uint8_t>(t * 255.0f),
                   static_cast<uint8_t>(p * 255.0f),
                   static_cast<uint8_t>(v * 255.0f));
      case 5:
        return rgb(static_cast<uint8_t>(v * 255.0f),
                   static_cast<uint8_t>(p * 255.0f),
                   static_cast<uint8_t>(q * 255.0f));
      default:
        return Color(0);
      }
    }

    static Color temp(float k)
    {
      static float lastK = -1.0f;
      static Color lastColor(0);

      // Quantize temperature to reduce unique values and reuse cached result.
      // Step 50K даёт достаточно плавный переход и сильно снижает число расчётов.
      const float step = 50.0f;
      const float invStep = 1.0f / step;
      const int bucket = static_cast<int>(k * invStep + 0.5f);
      const float qk = bucket * step;

      if (likely(qk == lastK))
      {
        return lastColor;
      }

      const float t = qk * 0.01f;
      float r, g, b;

      const bool t_le_66 = (t <= 66.0f);

      if (likely(t_le_66))
      {
        r = 255.0f;
      }
      else
      {
        r = clamp(329.7f * powf(t - 60.0f, -0.133f), 0.0f, 255.0f);
      }

      if (likely(t_le_66))
      {
        g = clamp(99.5f * logf(t) - 161.1f, 0.0f, 255.0f);
      }
      else
      {
        g = clamp(288.1f * powf(t - 60.0f, -0.076f), 0.0f, 255.0f);
      }

      if (unlikely(t >= 66.0f))
      {
        b = 255.0f;
      }
      else if (unlikely(t <= 19.0f))
      {
        b = 0.0f;
      }
      else
      {
        b = clamp(138.5f * logf(t - 10.0f) - 305.0f, 0.0f, 255.0f);
      }

      Color result = rgb(static_cast<uint8_t>(r), static_cast<uint8_t>(g), static_cast<uint8_t>(b));
      lastK = qk;
      lastColor = result;
      return result;
    }

    static constexpr uint16_t BLACK = 0, WHITE = 0xFFFF, RED = 0xF800, GREEN = 0x07E0, BLUE = 0x001F, CYAN = 0x07FF, MAGENTA = 0xF81F, YELLOW = 0xFFE0;
    static constexpr uint16_t GRAY = 0x8410, DARK_GRAY = 0x4208, LIGHT_GRAY = 0xC618, ORANGE = 0xFD20, PINK = 0xF81F, PURPLE = 0x780F, BROWN = 0xA145, LIME = 0x07E0;

    __attribute__((always_inline)) inline Color blend(const Color &c, uint8_t a) const
    {
      if (unlikely(a == 0))
        return *this;
      if (unlikely(a == 255))
        return c;

      const uint32_t ia = 255u - a;
      const uint32_t c1 = rgb565, c2 = c.rgb565;

      const uint32_t rb1 = c1 & 0xF81Fu, rb2 = c2 & 0xF81Fu;
      const uint32_t g1 = c1 & 0x07E0u, g2 = c2 & 0x07E0u;

      const uint32_t rb = ((rb1 * ia + rb2 * a) >> 8) & 0xF81Fu;
      const uint32_t g = ((g1 * ia + g2 * a) >> 8) & 0x07E0u;

      return Color(rb | g);
    }

    __attribute__((always_inline)) inline Color darken(uint8_t amt) const
    {
      if (unlikely(amt == 0))
        return *this;
      if (unlikely(amt == 255))
        return Color(0);

      const uint32_t f = 255u - amt;
      const uint32_t rb = ((rgb565 & 0xF81Fu) * f) >> 8;
      const uint32_t g = ((rgb565 & 0x07E0u) * f) >> 8;

      return Color(static_cast<uint16_t>((rb & 0xF81Fu) | (g & 0x07E0u)));
    }

    __attribute__((always_inline)) inline Color lighten(uint8_t amt) const
    {
      if (unlikely(amt == 0))
        return *this;

      const uint32_t r = ((rgb565 >> 11) & 0x1F) + ((31 * amt) >> 8);
      const uint32_t g = ((rgb565 >> 5) & 0x3F) + ((63 * amt) >> 8);
      const uint32_t b = (rgb565 & 0x1F) + ((31 * amt) >> 8);

      const uint32_t r_clamped = r > 31 ? 31 : r;
      const uint32_t g_clamped = g > 63 ? 63 : g;
      const uint32_t b_clamped = b > 31 ? 31 : b;

      return Color((r_clamped << 11) | (g_clamped << 5) | b_clamped);
    }

    __attribute__((always_inline)) inline uint8_t brightness() const
    {
      const uint32_t r = (rgb565 >> 11) & 0x1F;
      const uint32_t g = (rgb565 >> 5) & 0x3F;
      const uint32_t b = rgb565 & 0x1F;
      return (r * 77 + g * 151 + b * 28) >> 8;
    }
  };

  enum SkyType
  {
    DAY,
    SUNSET,
    NIGHT,
    DAWN,
    OVERCAST,
    CUSTOM
  };

#define SKYBOX_DAY DAY
#define SKYBOX_SUNSET SUNSET
#define SKYBOX_NIGHT NIGHT
#define SKYBOX_DAWN DAWN
#define SKYBOX_OVERCAST OVERCAST
#define SKYBOX_CUSTOM CUSTOM

  struct alignas(8) Sky
  {
    SkyType type = DAY;
    Color top, horizon, ground;
    bool enabled = true;

    Sky() { setPreset(DAY); }
    Sky(SkyType t) : type(t) { setPreset(t); }
    Sky(Color t, Color h, Color g) : type(CUSTOM), top(t), horizon(h), ground(g) {}

    void setPreset(SkyType t)
    {
      static constexpr Color presets[][3] = {
          {Color::rgb(60, 140, 255), Color::rgb(210, 230, 255), Color::rgb(110, 120, 140)},
          {Color::rgb(250, 130, 90), Color::rgb(255, 210, 140), Color::rgb(80, 55, 100)},
          {Color::rgb(15, 40, 100), Color::rgb(40, 90, 160), Color::rgb(10, 25, 60)},
          {Color::rgb(120, 155, 230), Color::rgb(255, 195, 170), Color::rgb(90, 95, 120)},
          {Color::rgb(140, 160, 175), Color::rgb(195, 205, 215), Color::rgb(95, 106, 106)}};

      type = t;
      if (t != CUSTOM && t < 5)
      {
        top = presets[t][0];
        horizon = presets[t][1];
        ground = presets[t][2];
      }
    }

    void setCustom(Color t, Color h, Color g)
    {
      type = CUSTOM;
      top = t;
      horizon = h;
      ground = g;
    }

    float getLightTemp() const
    {
      static constexpr float temps[] = {5500, 2500, 8000, 4000, 6500};
      return (type < 5) ? temps[type] : 5500;
    }

    float getRecommendedLightTemperature() const { return getLightTemp(); }

    Color getLightColor() const { return Color::temp(getLightTemp()); }

    __attribute__((always_inline)) inline Color getColorAtY(int16_t y, int16_t h) const
    {
      if (unlikely(!enabled))
        return Color::BLACK;
      if (unlikely(y <= 0))
        return top;
      if (unlikely(y >= h))
        return ground;

      const float t = (float)y / h;

      if (likely(t < 0.65f))
      {
        const float st = t * 1.538f;
        const float s = st * st * (3 - 2 * st);
        return lerp(top, horizon, s);
      }
      else
      {
        const float gt = clamp((t - 0.65f) * 3.43f, 0.0f, 1.0f);
        const float s = gt * gt * (3 - 2 * gt);
        return lerp(horizon, ground, s);
      }
    }

  private:
    __attribute__((always_inline)) inline Color lerp(Color c1, Color c2, float t) const
    {
      if (unlikely(t <= 0))
        return c1;
      if (unlikely(t >= 1))
        return c2;

      const int r1 = (c1.rgb565 >> 11) & 0x1F, g1 = (c1.rgb565 >> 5) & 0x3F, b1 = c1.rgb565 & 0x1F;
      const int r2 = (c2.rgb565 >> 11) & 0x1F, g2 = (c2.rgb565 >> 5) & 0x3F, b2 = c2.rgb565 & 0x1F;

      const uint16_t r = r1 + (r2 - r1) * t;
      const uint16_t g = g1 + (g2 - g1) * t;
      const uint16_t b = b1 + (b2 - b1) * t;

      return Color((r << 11) | (g << 5) | b);
    }
  };

  class alignas(16) PerfCounter
  {
  private:
    uint32_t frameCount = 0, lastTime = 0, frameTime = 0;
    float currentFPS = 0, avgFPS = 0, sum = 0;

    static constexpr int SAMPLES = 60;
    float history[SAMPLES];
    int idx = 0;
    bool firstReceived = false;
    uint32_t fpsLastTime = 0;
    bool fpsInitialized = false;

  public:
    PerfCounter() : history{} {}

    void begin() { lastTime = micros(); }

    void endFrame()
    {
      const uint32_t now = micros();
      frameTime = now - lastTime;
      frameCount++;

      if (frameTime > 0u)
      {
        const float fps = 1000000.0f / static_cast<float>(frameTime);
        currentFPS = fps;
      }

      if (!fpsInitialized)
      {
        fpsLastTime = now;
        fpsInitialized = true;
      }

      const uint32_t dt = now - fpsLastTime;
      if (dt >= 1000000u)
      {
        const float accurateFPS = frameCount * 1000000.0f / static_cast<float>(dt);
        frameCount = 0;
        fpsLastTime = now;

        if (!firstReceived)
        {
          for (int i = 0; i < SAMPLES; ++i)
            history[i] = accurateFPS;
          sum = accurateFPS * static_cast<float>(SAMPLES);
          avgFPS = accurateFPS;
          firstReceived = true;
        }
        else
        {
          sum -= history[idx];
          history[idx] = accurateFPS;
          sum += accurateFPS;
          idx = (idx + 1) % SAMPLES;
          avgFPS = sum / static_cast<float>(SAMPLES);
        }
      }
      else if (!firstReceived)
      {
        avgFPS = currentFPS;
      }

      lastTime = now;
    }

    float getFPS() const { return currentFPS; }
    float getAvgFPS() const { return avgFPS; }
    float getAverageFPS() const { return avgFPS; }
    uint32_t getFrameTime() const { return frameTime; }
    uint32_t getFrameTimeMs() const { return frameTime / 1000; }

    bool isStable() const { return firstReceived && (avgFPS > 5); }
    float getEfficiency() const { return clamp(avgFPS / 120.0f, 0.0f, 1.0f); }

    void reset()
    {
      frameCount = 0;
      currentFPS = 0.0f;
      avgFPS = 0.0f;
      firstReceived = false;
      sum = 0.0f;
      idx = 0;
      frameTime = 0;
      fpsLastTime = 0;
      fpsInitialized = false;
    }
  };

  struct alignas(8) Viewport
  {
    int16_t x = 0, y = 0;
    uint16_t width = 240, height = 320;

    Viewport() = default;
    Viewport(int16_t x_, int16_t y_, uint16_t w_, uint16_t h_) : x(x_), y(y_), width(w_), height(h_) {}
    Viewport(uint16_t w_, uint16_t h_) : width(w_), height(h_) {}

    __attribute__((always_inline)) inline bool contains(int16_t px, int16_t py) const
    {
      return px >= x && px < x + width && py >= y && py < y + height;
    }

    __attribute__((always_inline)) inline float aspect() const { return (float)width / height; }
    __attribute__((always_inline)) inline uint32_t area() const { return width * height; }
  };

  struct MemUtils
  {
    static size_t getFreeHeap() { return ESP.getFreeHeap(); }
    static size_t getFreePSRAM() { return ESP.getFreePsram(); }
    static size_t getLargestFreeBlock() { return ESP.getMaxAllocHeap(); }

    static void *allocAligned(size_t size, size_t align = 4)
    {
      (void)align;

      if (size > 1024)
      {
        if (psramFound())
        {
          return ps_malloc(size);
        }
      }
      return malloc(size);
    }

    static void freeAligned(void *ptr)
    {
      if (ptr)
        free(ptr);
    }

    static bool isInPSRAM(void *ptr)
    {
      return ((uint32_t)ptr >= 0x3F800000 && (uint32_t)ptr < 0x3FC00000);
    }
  };

  struct CoreConfig
  {
    static constexpr uint32_t CORE_FREQ = 240000000;
    static constexpr uint32_t SPI_FREQ = 80000000;
    static constexpr uint16_t DEFAULT_WIDTH = 240;
    static constexpr uint16_t DEFAULT_HEIGHT = 320;
    static constexpr float DEFAULT_FOV = 60.0f;
    static constexpr float EPSILON = 1e-6f;

    struct Performance
    {
      static constexpr int FPS_SAMPLES = 60;
      static constexpr int MAX_FPS = 120;
      static constexpr int MIN_FPS = 5;
      static constexpr uint32_t FRAME_TIME_US = 16667;
    };

    struct Rendering
    {
      static constexpr int MAX_VERTICES = 10000;
      static constexpr int MAX_TRIANGLES = 20000;
      static constexpr float Z_NEAR = 0.1f;
      static constexpr float Z_FAR = 1000.0f;
    };
  };

  struct Palette
  {
    static Color get(const Color *palette, int size, float t)
    {
      if (size <= 0 || !palette)
      {
        return Color(0);
      }
      if (size == 1)
      {
        return palette[0];
      }

      t = clamp(t, 0.0f, 1.0f);
      const float scaled = t * (size - 1);
      const int i = static_cast<int>(scaled);
      if (i >= size - 1)
        return palette[size - 1];

      const float f = scaled - static_cast<float>(i);
      const uint8_t alpha = static_cast<uint8_t>(f * 255.0f);
      return palette[i].blend(palette[i + 1], alpha);
    }
  };

  enum EventType
  {
    EVENT_FRAME_START = 0,
    EVENT_FRAME_END = 1,
    EVENT_MESH_LOADED = 2,
    EVENT_TEXTURE_LOADED = 3,
    EVENT_CAMERA_CHANGED = 4,
    EVENT_SCENE_CHANGED = 5,
    EVENT_MEMORY_LOW = 6,
    EVENT_FPS_CHANGED = 7,
    EVENT_USER_CUSTOM = 100
  };

  struct EventSystem
  {
  private:
    struct Listener
    {
      void (*callback)(EventType, void *);
      void *userData;
      bool active;
    };

    static constexpr int MAX_LISTENERS = 32;
    static Listener listeners[MAX_LISTENERS];
    static int listenerCount;

  public:
    static bool subscribe(EventType type, void (*callback)(EventType, void *), void *userData = nullptr)
    {
      if (listenerCount >= MAX_LISTENERS)
        return false;

      listeners[listenerCount] = {callback, userData, true};
      listenerCount++;
      return true;
    }

    static void unsubscribe(void (*callback)(EventType, void *))
    {
      for (int i = 0; i < listenerCount; i++)
      {
        if (listeners[i].callback == callback)
        {
          listeners[i].active = false;
        }
      }
    }

    static void emit(EventType type, void *data = nullptr)
    {
      for (int i = 0; i < listenerCount; i++)
      {
        if (listeners[i].active && listeners[i].callback)
        {
          listeners[i].callback(type, data ? data : listeners[i].userData);
        }
      }
    }

    static void cleanup()
    {
      int writeIdx = 0;
      for (int readIdx = 0; readIdx < listenerCount; readIdx++)
      {
        if (listeners[readIdx].active)
        {
          if (writeIdx != readIdx)
          {
            listeners[writeIdx] = listeners[readIdx];
          }
          writeIdx++;
        }
      }
      listenerCount = writeIdx;
    }

    static int getListenerCount() { return listenerCount; }
  };

  struct Profiler
  {
  private:
    struct Section
    {
      const char *name;
      uint32_t startTime;
      uint32_t totalTime;
      uint32_t callCount;
      bool active;
    };

    static constexpr int MAX_SECTIONS = 16;
    static Section sections[MAX_SECTIONS];
    static int sectionCount;
    static int currentSection;

  public:
    static void beginSection(const char *name)
    {
      if (!name)
        return;
      int idx = -1;

      for (int i = 0; i < sectionCount; i++)
      {
        if (strcmp(sections[i].name, name) == 0)
        {
          idx = i;
          break;
        }
      }

      if (idx == -1 && sectionCount < MAX_SECTIONS)
      {
        idx = sectionCount++;
        sections[idx] = {name, 0, 0, 0, false};
      }

      if (idx != -1)
      {
        sections[idx].startTime = micros();
        sections[idx].active = true;
        currentSection = idx;
      }
    }

    static void endSection()
    {
      if (currentSection >= 0 && currentSection < sectionCount)
      {
        Section &sec = sections[currentSection];
        if (sec.active)
        {
          uint32_t elapsed = micros() - sec.startTime;
          sec.totalTime += elapsed;
          sec.callCount++;
          sec.active = false;
        }
      }
      currentSection = -1;
    }

    static void printReport()
    {
      uint32_t totalFrameTime = 0;

      for (int i = 0; i < sectionCount; i++)
      {
        totalFrameTime += sections[i].totalTime;
      }

      for (int i = 0; i < sectionCount; i++)
      {
        Section &sec = sections[i];
        if (sec.callCount > 0)
        {
          float avgTime = sec.totalTime / (float)sec.callCount;
          float percentage = totalFrameTime > 0 ? (sec.totalTime * 100.0f / totalFrameTime) : 0;

          LOGI(::pip3D::Debug::LOG_MODULE_PERFORMANCE,
               "%s: %.2fms avg, %d calls, %.1f%%",
               sec.name,
               avgTime / 1000.0f,
               sec.callCount,
               percentage);
        }
      }

      LOGI(::pip3D::Debug::LOG_MODULE_PERFORMANCE,
           "Total: %.2fms", totalFrameTime / 1000.0f);
    }

    static void reset()
    {
      for (int i = 0; i < sectionCount; i++)
      {
        sections[i].totalTime = 0;
        sections[i].callCount = 0;
        sections[i].active = false;
      }
      currentSection = -1;
    }

    static float getSectionTime(const char *name)
    {
      for (int i = 0; i < sectionCount; i++)
      {
        if (strcmp(sections[i].name, name) == 0 && sections[i].callCount > 0)
        {
          return sections[i].totalTime / (float)sections[i].callCount / 1000.0f;
        }
      }
      return 0.0f;
    }
  };

  enum ResourceType
  {
    RES_TEXTURE = 0,
    RES_MESH = 1,
    RES_SOUND = 2,
    RES_DATA = 3
  };

  struct ResourceManager
  {
  private:
    struct Resource
    {
      const char *path;
      void *data;
      size_t size;
      ResourceType type;
      uint32_t refCount;
      uint32_t lastAccess;
      bool loaded;
    };

    static constexpr int MAX_RESOURCES = 64;
    static Resource resources[MAX_RESOURCES];
    static int resourceCount;
    static size_t totalMemory;
    static size_t maxMemory;

  public:
    static void init(size_t maxMem = 1024 * 1024)
    {
      maxMemory = maxMem;
      totalMemory = 0;
      resourceCount = 0;

      for (int i = 0; i < MAX_RESOURCES; i++)
      {
        resources[i] = {nullptr, nullptr, 0, RES_DATA, 0, 0, false};
      }

      LOGI(::pip3D::Debug::LOG_MODULE_RESOURCES,
           "ResourceManager initialized, maxMem=%u bytes, maxResources=%d",
           static_cast<unsigned int>(maxMemory),
           MAX_RESOURCES);
    }

    static void *load(const char *path, ResourceType type, size_t size)
    {
      if (!path)
      {
        LOGE(::pip3D::Debug::LOG_MODULE_RESOURCES,
             "ResourceManager::load called with null path (type=%d, size=%u)",
             static_cast<int>(type),
             static_cast<unsigned int>(size));
        return nullptr;
      }
      int idx = findResource(path);

      if (idx != -1)
      {
        resources[idx].refCount++;
        resources[idx].lastAccess = millis();
        LOGI(::pip3D::Debug::LOG_MODULE_RESOURCES,
             "Resource '%s' already loaded, refCount=%u", path, resources[idx].refCount);
        return resources[idx].data;
      }

      if (resourceCount >= MAX_RESOURCES)
      {
        cleanup();
        if (resourceCount >= MAX_RESOURCES)
        {
          LOGE(::pip3D::Debug::LOG_MODULE_RESOURCES,
               "ResourceManager capacity exceeded while loading '%s' (MAX_RESOURCES=%d)",
               path,
               MAX_RESOURCES);
          return nullptr;
        }
      }

      if (totalMemory + size > maxMemory)
      {
        freeOldest();
        if (totalMemory + size > maxMemory)
        {
          LOGE(::pip3D::Debug::LOG_MODULE_RESOURCES,
               "Not enough memory to load resource '%s' (size=%u, used=%u, max=%u)",
               path,
               static_cast<unsigned int>(size),
               static_cast<unsigned int>(totalMemory),
               static_cast<unsigned int>(maxMemory));
          return nullptr;
        }
      }

      void *data = MemUtils::allocAligned(size);
      if (!data)
      {
        LOGE(::pip3D::Debug::LOG_MODULE_RESOURCES,
             "MemUtils::allocAligned failed for resource '%s' (size=%u)",
             path,
             static_cast<unsigned int>(size));
        return nullptr;
      }

      idx = resourceCount++;
      resources[idx] = {path, data, size, type, 1, millis(), true};
      totalMemory += size;

      LOGI(::pip3D::Debug::LOG_MODULE_RESOURCES,
           "Loaded resource '%s' (type=%d, size=%u bytes, totalMemory=%u/%u)",
           path,
           static_cast<int>(type),
           static_cast<unsigned int>(size),
           static_cast<unsigned int>(totalMemory),
           static_cast<unsigned int>(maxMemory));

      EventType ev = EVENT_USER_CUSTOM;
      if (type == RES_TEXTURE)
        ev = EVENT_TEXTURE_LOADED;
      else if (type == RES_MESH)
        ev = EVENT_MESH_LOADED;
      EventSystem::emit(ev, (void *)path);
      return data;
    }

    static void unload(const char *path)
    {
      int idx = findResource(path);
      if (idx != -1)
      {
        Resource &res = resources[idx];
        if (--res.refCount == 0)
        {
          MemUtils::freeAligned(res.data);
          totalMemory -= res.size;
          res.loaded = false;
          res.data = nullptr;

          LOGI(::pip3D::Debug::LOG_MODULE_RESOURCES,
               "Unloaded resource '%s' (size=%u bytes, totalMemory=%u/%u)",
               res.path ? res.path : "<null>",
               static_cast<unsigned int>(res.size),
               static_cast<unsigned int>(totalMemory),
               static_cast<unsigned int>(maxMemory));
        }
        else
        {
          LOGI(::pip3D::Debug::LOG_MODULE_RESOURCES,
               "Decreased refCount for resource '%s' to %u",
               res.path ? res.path : "<null>",
               static_cast<unsigned int>(res.refCount));
        }
      }
      else
      {
        LOGW(::pip3D::Debug::LOG_MODULE_RESOURCES,
             "Attempt to unload unknown resource '%s'", path ? path : "<null>");
      }
    }

    static void unloadAll()
    {
      for (int i = 0; i < resourceCount; i++)
      {
        if (resources[i].loaded && resources[i].data)
        {
          MemUtils::freeAligned(resources[i].data);
        }
      }
      resourceCount = 0;
      totalMemory = 0;

      LOGI(::pip3D::Debug::LOG_MODULE_RESOURCES,
           "All resources unloaded, memory usage reset to 0");
    }

    static size_t getMemoryUsage() { return totalMemory; }
    static size_t getMaxMemory() { return maxMemory; }
    static int getResourceCount() { return resourceCount; }

    static void printStatus()
    {
      LOGI(::pip3D::Debug::LOG_MODULE_RESOURCES,
           "Resources: %d/%d, Memory: %u/%u KB",
           resourceCount,
           MAX_RESOURCES,
           static_cast<unsigned int>(totalMemory / 1024u),
           static_cast<unsigned int>(maxMemory / 1024u));

      for (int i = 0; i < resourceCount; i++)
      {
        if (resources[i].loaded)
        {
          LOGI(::pip3D::Debug::LOG_MODULE_RESOURCES,
               "  %s: %u bytes, refs=%u",
               resources[i].path ? resources[i].path : "<null>",
               static_cast<unsigned int>(resources[i].size),
               static_cast<unsigned int>(resources[i].refCount));
        }
      }
    }

  private:
    static int findResource(const char *path)
    {
      for (int i = 0; i < resourceCount; i++)
      {
        if (resources[i].loaded && strcmp(resources[i].path, path) == 0)
        {
          return i;
        }
      }
      return -1;
    }

    static void cleanup()
    {
      int writeIdx = 0;
      for (int readIdx = 0; readIdx < resourceCount; readIdx++)
      {
        if (resources[readIdx].loaded && resources[readIdx].refCount > 0)
        {
          if (writeIdx != readIdx)
          {
            resources[writeIdx] = resources[readIdx];
          }
          writeIdx++;
        }
        else if (resources[readIdx].data)
        {
          MemUtils::freeAligned(resources[readIdx].data);
          totalMemory -= resources[readIdx].size;
        }
      }
      resourceCount = writeIdx;
    }

    static void freeOldest()
    {
      uint32_t oldestTime = UINT32_MAX;
      int oldestIdx = -1;

      for (int i = 0; i < resourceCount; i++)
      {
        if (resources[i].loaded && resources[i].refCount == 0 &&
            resources[i].lastAccess < oldestTime)
        {
          oldestTime = resources[i].lastAccess;
          oldestIdx = i;
        }
      }

      if (oldestIdx != -1)
      {
        Resource &res = resources[oldestIdx];
        MemUtils::freeAligned(res.data);
        totalMemory -= res.size;
        res.loaded = false;
        res.data = nullptr;
      }
    }
  };

  using DisplayConfig = Display;
  using PerformanceCounter = PerfCounter;
  using Skybox = Sky;
  using SkyboxType = SkyType;

}

#endif
