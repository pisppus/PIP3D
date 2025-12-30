#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H

#include "../../Core/Core.h"
#include "Drivers/DisplayDriverBase.h"
#include "ZBuffer.h"

namespace pip3D
{
    class __attribute__((aligned(16))) FrameBuffer
    {
    private:
        uint16_t *buffer;
        DisplayDriverBase *display;
        DisplayConfig config;
        Skybox skybox;
        bool useSkybox;
        Color clearColor;

        static constexpr size_t DMA_ALIGNMENT = 64;

        uint32_t totalPixels;
        uint32_t pixels32;
        bool oddPixels;
        
        uint16_t *skyboxColorCache;
        int16_t cachedScreenHeight;
        bool cacheValid;
        
        uint16_t colorLUT[2];

    public:
        FrameBuffer() : buffer(nullptr), display(nullptr), useSkybox(true),
                        clearColor(Color::BLACK), totalPixels(0), pixels32(0), oddPixels(false),
                        skyboxColorCache(nullptr), cachedScreenHeight(0), cacheValid(false)
        {
            skybox.setPreset(SKYBOX_DAY);
            colorLUT[0] = colorLUT[1] = 0;
        }

        bool init(const DisplayConfig &cfg, DisplayDriverBase *disp)
        {
            if (buffer)
            {
                LOGW(::pip3D::Debug::LOG_MODULE_RENDER,
                     "FrameBuffer::init called more than once (buffer already allocated)");
                return false;
            }

            config = cfg;
            display = disp;

            totalPixels = static_cast<uint32_t>(config.width) * static_cast<uint32_t>(config.height);
            pixels32 = totalPixels >> 1;
            oddPixels = (totalPixels & 1u) != 0;

            size_t bufferSize = totalPixels * sizeof(uint16_t);
            bufferSize = (bufferSize + DMA_ALIGNMENT - 1) & ~(DMA_ALIGNMENT - 1);

            buffer = (uint16_t *)heap_caps_aligned_alloc(DMA_ALIGNMENT, bufferSize, 
                                                         MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);

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
            
            skyboxColorCache = (uint16_t *)heap_caps_malloc(SCREEN_HEIGHT * 2 * sizeof(uint16_t), 
                                                            MALLOC_CAP_INTERNAL);
            if (!skyboxColorCache)
            {
                LOGW(::pip3D::Debug::LOG_MODULE_RENDER,
                     "FrameBuffer::init warning: could not allocate skybox cache, performance will be reduced");
            }

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
        }

    private:
        __attribute__((always_inline)) inline void rebuildSkyboxCache()
        {
            if (!skyboxColorCache || !useSkybox || !skybox.enabled) return;
            
            for (int16_t y = 0; y < SCREEN_HEIGHT; ++y)
            {
                Color lineColor = skybox.getColorAtY(y, SCREEN_HEIGHT);
                uint16_t color1 = lineColor.rgb565;
                
                uint16_t darker = color1;
                if (color1 != 0)
                {
                    const uint16_t r = (color1 >> 11);
                    const uint16_t g = (color1 >> 5) & 0x3F;
                    const uint16_t b = color1 & 0x1F;
                    darker = ((r ? r - 1 : 0) << 11) |
                            ((g ? g - 1 : 0) << 5) |
                            (b ? b - 1 : 0);
                }
                
                skyboxColorCache[y * 2] = color1;
                skyboxColorCache[y * 2 + 1] = darker;
            }
            
            cachedScreenHeight = SCREEN_HEIGHT;
            cacheValid = true;
        }
        
        __attribute__((always_inline)) inline void fastClear()
        {
            const uint16_t clearCol = clearColor.rgb565;
            const uint32_t clearColor32 = (clearCol << 16) | clearCol;
            uint32_t *fb32 = (uint32_t *)buffer;

            uint32_t i = 0;
            const uint32_t blocks8 = pixels32 >> 3;
            const uint32_t limit8 = blocks8 << 3;
            
            for (; i < limit8; i += 8)
            {
                fb32[i] = clearColor32;
                fb32[i + 1] = clearColor32;
                fb32[i + 2] = clearColor32;
                fb32[i + 3] = clearColor32;
                fb32[i + 4] = clearColor32;
                fb32[i + 5] = clearColor32;
                fb32[i + 6] = clearColor32;
                fb32[i + 7] = clearColor32;
            }
            
            for (; i < pixels32; i++)
            {
                fb32[i] = clearColor32;
            }

            if (oddPixels)
            {
                buffer[totalPixels - 1] = clearCol;
            }
        }

    public:
        template <uint16_t WIDTH, uint16_t HEIGHT>
        __attribute__((always_inline)) inline void drawSkyboxWhereEmpty(const ZBuffer<WIDTH, HEIGHT> &zbuf)
        {
            if (unlikely(!buffer))
            {
                LOGE(::pip3D::Debug::LOG_MODULE_RENDER,
                     "FrameBuffer::drawSkyboxWhereEmpty called with null buffer");
                return;
            }

            const int16_t *__restrict__ zb = zbuf.getBufferPtr();
            if (unlikely(!zb))
            {
                LOGE(::pip3D::Debug::LOG_MODULE_RENDER,
                     "FrameBuffer::drawSkyboxWhereEmpty called with null z-buffer");
                return;
            }

            const uint16_t fbWidth = config.width;
            const uint16_t fbHeight = config.height;

            if (unlikely(fbWidth != WIDTH || fbHeight != HEIGHT))
            {
                LOGW(::pip3D::Debug::LOG_MODULE_RENDER,
                     "FrameBuffer::drawSkyboxWhereEmpty size mismatch (fb=%ux%u, zb=%ux%u)",
                     static_cast<unsigned int>(fbWidth),
                     static_cast<unsigned int>(fbHeight),
                     static_cast<unsigned int>(WIDTH),
                     static_cast<unsigned int>(HEIGHT));
                return;
            }

            const int16_t clearDepth = ZBuffer<WIDTH, HEIGHT>::clearDepthValue();
            const int16_t invShadowMask = ~ZBuffer<WIDTH, HEIGHT>::shadowFlagMask();

            const bool shouldUseSkybox = useSkybox && skybox.enabled;
            
            if (shouldUseSkybox && skyboxColorCache && !cacheValid)
            {
                rebuildSkyboxCache();
            }

            const uint16_t baseClearColor = clearColor.rgb565;

            for (uint16_t y = 0; y < fbHeight; ++y)
            {
                const int16_t globalY = currentBandOffsetY() + static_cast<int16_t>(y);

                uint16_t *__restrict__ row = buffer + (static_cast<size_t>(y) * fbWidth);
                const int16_t *__restrict__ zbRow = zb + (static_cast<size_t>(y) * WIDTH);

                if (shouldUseSkybox && skyboxColorCache && cacheValid && globalY < SCREEN_HEIGHT)
                {
                    const uint16_t cacheIdx = globalY * 2;
                    const uint16_t yOdd = y & 1;
                    colorLUT[0] = skyboxColorCache[cacheIdx];
                    colorLUT[1] = skyboxColorCache[cacheIdx + (yOdd ? 1 : 0)];
                }
                else if (shouldUseSkybox && globalY < SCREEN_HEIGHT)
                {
                    Color lineColor = skybox.getColorAtY(globalY, SCREEN_HEIGHT);
                    const uint16_t color1 = lineColor.rgb565;
                    
                    uint16_t darker = color1;
                    if (color1 != 0)
                    {
                        const uint16_t r = (color1 >> 11);
                        const uint16_t g = (color1 >> 5) & 0x3F;
                        const uint16_t b = color1 & 0x1F;
                        darker = ((r ? r - 1 : 0) << 11) |
                                ((g ? g - 1 : 0) << 5) |
                                (b ? b - 1 : 0);
                    }
                    
                    const uint16_t yOdd = y & 1;
                    colorLUT[0] = color1;
                    colorLUT[1] = yOdd ? darker : color1;
                }
                else
                {
                    colorLUT[0] = colorLUT[1] = baseClearColor;
                }

                uint16_t x = 0;
                const uint16_t width16 = fbWidth & ~15u;

                for (; x < width16; x += 16)
                {
                    __builtin_prefetch(&zbRow[x + 16], 0, 0);
                    
                    const int16_t d0 = zbRow[x] & invShadowMask;
                    const int16_t d1 = zbRow[x + 1] & invShadowMask;
                    const int16_t d2 = zbRow[x + 2] & invShadowMask;
                    const int16_t d3 = zbRow[x + 3] & invShadowMask;
                    const int16_t d4 = zbRow[x + 4] & invShadowMask;
                    const int16_t d5 = zbRow[x + 5] & invShadowMask;
                    const int16_t d6 = zbRow[x + 6] & invShadowMask;
                    const int16_t d7 = zbRow[x + 7] & invShadowMask;
                    const int16_t d8 = zbRow[x + 8] & invShadowMask;
                    const int16_t d9 = zbRow[x + 9] & invShadowMask;
                    const int16_t d10 = zbRow[x + 10] & invShadowMask;
                    const int16_t d11 = zbRow[x + 11] & invShadowMask;
                    const int16_t d12 = zbRow[x + 12] & invShadowMask;
                    const int16_t d13 = zbRow[x + 13] & invShadowMask;
                    const int16_t d14 = zbRow[x + 14] & invShadowMask;
                    const int16_t d15 = zbRow[x + 15] & invShadowMask;

                    if (d0 == clearDepth) row[x] = colorLUT[x & 1u];
                    if (d1 == clearDepth) row[x + 1] = colorLUT[(x + 1) & 1u];
                    if (d2 == clearDepth) row[x + 2] = colorLUT[(x + 2) & 1u];
                    if (d3 == clearDepth) row[x + 3] = colorLUT[(x + 3) & 1u];
                    if (d4 == clearDepth) row[x + 4] = colorLUT[(x + 4) & 1u];
                    if (d5 == clearDepth) row[x + 5] = colorLUT[(x + 5) & 1u];
                    if (d6 == clearDepth) row[x + 6] = colorLUT[(x + 6) & 1u];
                    if (d7 == clearDepth) row[x + 7] = colorLUT[(x + 7) & 1u];
                    if (d8 == clearDepth) row[x + 8] = colorLUT[(x + 8) & 1u];
                    if (d9 == clearDepth) row[x + 9] = colorLUT[(x + 9) & 1u];
                    if (d10 == clearDepth) row[x + 10] = colorLUT[(x + 10) & 1u];
                    if (d11 == clearDepth) row[x + 11] = colorLUT[(x + 11) & 1u];
                    if (d12 == clearDepth) row[x + 12] = colorLUT[(x + 12) & 1u];
                    if (d13 == clearDepth) row[x + 13] = colorLUT[(x + 13) & 1u];
                    if (d14 == clearDepth) row[x + 14] = colorLUT[(x + 14) & 1u];
                    if (d15 == clearDepth) row[x + 15] = colorLUT[(x + 15) & 1u];
                }

                for (; x < fbWidth; ++x)
                {
                    const int16_t depthNoShadow = zbRow[x] & invShadowMask;
                    if (depthNoShadow == clearDepth)
                    {
                        row[x] = colorLUT[x & 1u];
                    }
                }
            }
        }

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

        __attribute__((always_inline)) inline void setSkyboxEnabled(bool enabled) 
        { 
            if (useSkybox != enabled)
            {
                useSkybox = enabled;
                cacheValid = false;
            }
        }
        
        __attribute__((always_inline)) inline void setSkyboxType(SkyboxType type) 
        { 
            skybox.setPreset(type);
            cacheValid = false;
        }
        
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
            if (skyboxColorCache)
            {
                heap_caps_free(skyboxColorCache);
                skyboxColorCache = nullptr;
            }
        }
    };
}

#endif