#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H

#include "../../Core/Core.h"
#include "Drivers/ST7789Driver.h"

namespace pip3D
{

    class __attribute__((aligned(16))) FrameBuffer
    {
    private:
        uint16_t *buffer;
        ST7789Driver *display;
        DisplayConfig config;
        Skybox skybox;
        bool useSkybox;
        Color clearColor;

        static constexpr size_t DMA_ALIGNMENT = 64;

    public:
        FrameBuffer() : buffer(nullptr), display(nullptr), useSkybox(true),
                        clearColor(Color::BLACK)
        {
            skybox.setPreset(SKYBOX_DAY);
        }

        bool init(const DisplayConfig &cfg, ST7789Driver *disp)
        {
            if (buffer)
            {
                LOGW(::pip3D::Debug::LOG_MODULE_RENDER,
                     "FrameBuffer::init called more than once (buffer already allocated)");
                return false;
            }

            config = cfg;
            display = disp;

            size_t bufferSize = config.width * config.height * sizeof(uint16_t);
            bufferSize = (bufferSize + DMA_ALIGNMENT - 1) & ~(DMA_ALIGNMENT - 1);

            if (psramFound())
            {
                buffer = (uint16_t *)heap_caps_aligned_alloc(DMA_ALIGNMENT, bufferSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
            }
            else
            {
                buffer = (uint16_t *)heap_caps_aligned_alloc(DMA_ALIGNMENT, bufferSize, MALLOC_CAP_DMA);
            }

            if (!buffer)
            {
                LOGE(::pip3D::Debug::LOG_MODULE_RENDER,
                     "FrameBuffer::init failed: could not allocate %u bytes for %dx%d framebuffer",
                     static_cast<unsigned int>(bufferSize),
                     static_cast<int>(config.width),
                     static_cast<int>(config.height));
                return false;
            }
            memset(buffer, 0, bufferSize);

            LOGI(::pip3D::Debug::LOG_MODULE_RENDER,
                 "FrameBuffer::init OK: %dx%d, bufferSize=%u bytes",
                 static_cast<int>(config.width),
                 static_cast<int>(config.height),
                 static_cast<unsigned int>(bufferSize));
            return true;
        }

        void beginFrame()
        {
            if (unlikely(!buffer))
            {
                LOGE(::pip3D::Debug::LOG_MODULE_RENDER,
                     "FrameBuffer::beginFrame called with null buffer");
                return;
            }

            if (useSkybox && skybox.enabled)
            {
                renderSkybox();
            }
            else
            {
                fastClear();
            }
        }

    private:
        __attribute__((always_inline)) inline void renderSkybox()
        {
            uint16_t *row = buffer;
            const uint32_t pixels32 = static_cast<uint32_t>(config.width) >> 1;
            const bool oddWidth = (config.width & 1) != 0;

            for (int16_t y = 0; y < config.height; y++)
            {
                Color lineColor = skybox.getColorAtY(y, config.height);
                uint16_t color1 = lineColor.rgb565;

                uint16_t r = (color1 >> 11) & 0x1F;
                uint16_t g = (color1 >> 5) & 0x3F;
                uint16_t b = color1 & 0x1F;
                uint16_t darker = ((r > 0 ? r - 1 : 0) << 11) |
                                  ((g > 0 ? g - 1 : 0) << 5) |
                                  (b > 0 ? b - 1 : 0);

                uint16_t color2 = (y & 1) ? darker : color1;
                uint32_t color32 = (color1 << 16) | color2;

                uint32_t *linePtr = (uint32_t *)row;

                for (uint32_t x = 0; x < pixels32; x++)
                {
                    linePtr[x] = color32;
                }

                if (oddWidth)
                {
                    row[config.width - 1] = color1;
                }

                row += config.width;
            }
        }

        __attribute__((always_inline)) inline void fastClear()
        {
            const uint16_t clearCol = clearColor.rgb565;
            const uint32_t clearColor32 = (clearCol << 16) | clearCol;
            uint32_t *fb32 = (uint32_t *)buffer;
            const uint32_t totalPixels = static_cast<uint32_t>(config.width) * static_cast<uint32_t>(config.height);
            const uint32_t pixels32 = totalPixels >> 1;

            for (uint32_t i = 0; i < pixels32; i++)
            {
                fb32[i] = clearColor32;
            }

            if (totalPixels & 1u)
            {
                buffer[totalPixels - 1] = clearCol;
            }
        }

    public:
        __attribute__((always_inline)) inline void endFrame()
        {
            if (unlikely(!buffer || !display))
            {
                LOGE(::pip3D::Debug::LOG_MODULE_RENDER,
                     "FrameBuffer::endFrame called with invalid state (buffer=%p, display=%p)",
                     (void *)buffer,
                     (void *)display);
                return;
            }
            display->pushImage(0, 0, config.width, config.height, buffer);
        }

        __attribute__((always_inline)) inline void endFrameRegion(int16_t x, int16_t y, int16_t w, int16_t h)
        {
            if (unlikely(!buffer || !display))
            {
                LOGE(::pip3D::Debug::LOG_MODULE_RENDER,
                     "FrameBuffer::endFrameRegion called with invalid state (buffer=%p, display=%p)",
                     (void *)buffer,
                     (void *)display);
                return;
            }
            display->pushImage(x, y, w, h, buffer);
        }

        __attribute__((always_inline)) inline uint16_t *getBuffer() { return buffer; }
        __attribute__((always_inline)) inline const uint16_t *getBuffer() const { return buffer; }
        __attribute__((always_inline)) inline const DisplayConfig &getConfig() const { return config; }

        __attribute__((always_inline)) inline void setSkyboxEnabled(bool enabled) { useSkybox = enabled; }
        __attribute__((always_inline)) inline void setSkyboxType(SkyboxType type) { skybox.setPreset(type); }
        __attribute__((always_inline)) inline void setClearColor(Color color) { clearColor = color; }

        __attribute__((always_inline)) inline Skybox &getSkybox() { return skybox; }
        __attribute__((always_inline)) inline const Skybox &getSkybox() const { return skybox; }
        __attribute__((always_inline)) inline bool isSkyboxEnabled() const { return useSkybox; }

        ~FrameBuffer()
        {
            if (buffer)
            {
                heap_caps_free(buffer);
                buffer = nullptr;
            }
        }
    };

}

#endif
