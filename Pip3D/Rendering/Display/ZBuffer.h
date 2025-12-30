#ifndef ZBUFFER_H
#define ZBUFFER_H

#include <Arduino.h>
#include "../../Core/Core.h"
#include "../../Core/Debug/Logging.h"

namespace pip3D
{

    static constexpr float BAYER_MATRIX_4X4[4][4] = {
        {0.0f / 16.0f, 8.0f / 16.0f, 2.0f / 16.0f, 10.0f / 16.0f},
        {12.0f / 16.0f, 4.0f / 16.0f, 14.0f / 16.0f, 6.0f / 16.0f},
        {3.0f / 16.0f, 11.0f / 16.0f, 1.0f / 16.0f, 9.0f / 16.0f},
        {15.0f / 16.0f, 7.0f / 16.0f, 13.0f / 16.0f, 5.0f / 16.0f}};

    template <uint16_t WIDTH, uint16_t HEIGHT>
    class ZBuffer
    {
    private:
        static constexpr size_t BUFFER_SIZE = WIDTH * HEIGHT;
        int16_t *buffer;
        static constexpr int16_t MAX_DEPTH = 32767;
        static constexpr int16_t CLEAR_DEPTH = static_cast<int16_t>(0x7F7F);
        static constexpr int16_t SHADOW_FLAG = static_cast<int16_t>(0x8000);
        static constexpr float INV_MAX_DEPTH = 1.0f / static_cast<float>(MAX_DEPTH);

    public:
        ZBuffer() : buffer(nullptr) {}
        ZBuffer(const ZBuffer &) = delete;
        ZBuffer &operator=(const ZBuffer &) = delete;

        __attribute__((warn_unused_result)) bool init()
        {
            if (buffer)
            {
                ::pip3D::MemUtils::freeData(buffer);
                buffer = nullptr;
            }

            buffer = static_cast<int16_t *>(::pip3D::MemUtils::allocData(BUFFER_SIZE * sizeof(int16_t)));

            if (!buffer)
            {
                LOGE(::pip3D::Debug::LOG_MODULE_RENDER,
                     "ZBuffer::init failed: could not allocate %u bytes for %ux%u buffer",
                     static_cast<unsigned int>(BUFFER_SIZE * sizeof(int16_t)),
                     static_cast<unsigned int>(WIDTH),
                     static_cast<unsigned int>(HEIGHT));
                return false;
            }

            clear();

            LOGI(::pip3D::Debug::LOG_MODULE_RENDER,
                 "ZBuffer::init OK: %ux%u (buffer=%p)",
                 static_cast<unsigned int>(WIDTH),
                 static_cast<unsigned int>(HEIGHT),
                 static_cast<void *>(buffer));
            return true;
        }

        void clear()
        {
            if (buffer)
            {
                memset(buffer, 0x7F, BUFFER_SIZE * sizeof(int16_t));
            }
        }

        __attribute__((always_inline, hot)) inline bool testAndSet(uint16_t x, uint16_t y, int32_t depth)
        {
            if (unlikely(x >= WIDTH || y >= HEIGHT))
            {
                LOGE(::pip3D::Debug::LOG_MODULE_RENDER,
                     "ZBuffer::testAndSet out of bounds (x=%u, y=%u, WIDTH=%u, HEIGHT=%u)",
                     static_cast<unsigned int>(x),
                     static_cast<unsigned int>(y),
                     static_cast<unsigned int>(WIDTH),
                     static_cast<unsigned int>(HEIGHT));
                return false;
            }

            const int16_t d = static_cast<int16_t>(depth);
            int16_t *__restrict__ row = buffer + static_cast<size_t>(y) * WIDTH;
            const int16_t stored = row[x];
            const int16_t currentDepth = static_cast<int16_t>(stored & ~SHADOW_FLAG);

            if (d < currentDepth)
            {
                row[x] = static_cast<int16_t>((stored & SHADOW_FLAG) | d);
                return true;
            }
            return false;
        }

        __attribute__((always_inline)) inline bool hasGeometry(uint16_t x, uint16_t y) const
        {
            if (unlikely(x >= WIDTH || y >= HEIGHT || !buffer))
            {
                LOGW(::pip3D::Debug::LOG_MODULE_RENDER,
                     "ZBuffer::hasGeometry invalid call (x=%u, y=%u, WIDTH=%u, HEIGHT=%u, buffer=%p)",
                     static_cast<unsigned int>(x),
                     static_cast<unsigned int>(y),
                     static_cast<unsigned int>(WIDTH),
                     static_cast<unsigned int>(HEIGHT),
                     static_cast<const void *>(buffer));
                return false;
            }

            const int16_t *row = buffer + static_cast<size_t>(y) * WIDTH;
            const int16_t d = row[x];
            return (d & ~SHADOW_FLAG) != CLEAR_DEPTH;
        }

        __attribute__((always_inline)) inline bool hasShadow(uint16_t x, uint16_t y) const
        {
            if (unlikely(x >= WIDTH || y >= HEIGHT || !buffer))
            {
                LOGW(::pip3D::Debug::LOG_MODULE_RENDER,
                     "ZBuffer::hasShadow invalid call (x=%u, y=%u, WIDTH=%u, HEIGHT=%u, buffer=%p)",
                     static_cast<unsigned int>(x),
                     static_cast<unsigned int>(y),
                     static_cast<unsigned int>(WIDTH),
                     static_cast<unsigned int>(HEIGHT),
                     static_cast<const void *>(buffer));
                return false;
            }

            const int16_t *row = buffer + static_cast<size_t>(y) * WIDTH;
            const int16_t d = row[x];
            return (d & SHADOW_FLAG) != 0;
        }

        __attribute__((always_inline)) inline float getDepth01(uint16_t x, uint16_t y) const
        {
            if (unlikely(x >= WIDTH || y >= HEIGHT || !buffer))
            {
                return 1.0f;
            }

            const int16_t *row = buffer + static_cast<size_t>(y) * WIDTH;
            const int16_t stored = row[x];
            const int16_t d = static_cast<int16_t>(stored & ~SHADOW_FLAG);

            if (d == CLEAR_DEPTH)
            {
                return 1.0f;
            }

            return static_cast<float>(d) * INV_MAX_DEPTH;
        }

        __attribute__((always_inline)) inline int16_t getRawDepth(uint16_t x, uint16_t y) const
        {
            if (unlikely(x >= WIDTH || y >= HEIGHT || !buffer))
            {
                return CLEAR_DEPTH;
            }

            const int16_t *row = buffer + static_cast<size_t>(y) * WIDTH;
            const int16_t stored = row[x];
            return static_cast<int16_t>(stored & ~SHADOW_FLAG);
        }

        // Accessors used by optimized skybox rendering pass.
        __attribute__((always_inline)) inline const int16_t *getBufferPtr() const
        {
            return buffer;
        }

        static __attribute__((always_inline)) inline int16_t clearDepthValue()
        {
            return CLEAR_DEPTH;
        }

        static __attribute__((always_inline)) inline int16_t shadowFlagMask()
        {
            return SHADOW_FLAG;
        }

        __attribute__((always_inline)) inline void markShadow(uint16_t x, uint16_t y)
        {
            if (unlikely(x >= WIDTH || y >= HEIGHT || !buffer))
            {
                LOGW(::pip3D::Debug::LOG_MODULE_RENDER,
                     "ZBuffer::markShadow invalid call (x=%u, y=%u, WIDTH=%u, HEIGHT=%u, buffer=%p)",
                     static_cast<unsigned int>(x),
                     static_cast<unsigned int>(y),
                     static_cast<unsigned int>(WIDTH),
                     static_cast<unsigned int>(HEIGHT),
                     static_cast<void *>(buffer));
                return;
            }

            int16_t *row = buffer + static_cast<size_t>(y) * WIDTH;
            row[x] |= SHADOW_FLAG;
        }

        __attribute__((always_inline, hot)) inline void testAndSetScanline(uint16_t y, uint16_t x_start, uint16_t x_end,
                                                                           int32_t depthStart, int32_t depthStep,
                                                                           uint16_t *frameBuffer, uint16_t color)
        {
            if (unlikely(y >= HEIGHT))
            {
                LOGE(::pip3D::Debug::LOG_MODULE_RENDER,
                     "ZBuffer::testAndSetScanline y out of bounds (y=%u, HEIGHT=%u)",
                     static_cast<unsigned int>(y),
                     static_cast<unsigned int>(HEIGHT));
                return;
            }
            if (unlikely(x_end >= WIDTH))
            {
                LOGW(::pip3D::Debug::LOG_MODULE_RENDER,
                     "ZBuffer::testAndSetScanline x_end clamped (x_end=%u, WIDTH=%u)",
                     static_cast<unsigned int>(x_end),
                     static_cast<unsigned int>(WIDTH));
                x_end = WIDTH - 1;
            }
            if (unlikely(x_start >= WIDTH))
            {
                LOGE(::pip3D::Debug::LOG_MODULE_RENDER,
                     "ZBuffer::testAndSetScanline x_start out of bounds (x_start=%u, WIDTH=%u)",
                     static_cast<unsigned int>(x_start),
                     static_cast<unsigned int>(WIDTH));
                return;
            }

            const uint16_t countTotal = x_end - x_start + 1;
            if (unlikely(countTotal == 0))
            {
                LOGW(::pip3D::Debug::LOG_MODULE_RENDER,
                     "ZBuffer::testAndSetScanline empty span (x_start=%u, x_end=%u)",
                     static_cast<unsigned int>(x_start),
                     static_cast<unsigned int>(x_end));
                return;
            }

            size_t index = y * WIDTH + x_start;
            int16_t *__restrict__ buf = buffer + index;
            uint16_t *__restrict__ fb = frameBuffer + index;

            int32_t depth = depthStart;

            uint16_t count = countTotal;

            while (count >= 4)
            {
                __builtin_prefetch(buf + 16, 1, 0);
                __builtin_prefetch(fb + 16, 1, 0);

                int16_t depth0 = static_cast<int16_t>(depth);
                int16_t stored0 = buf[0];
                int16_t currentDepth0 = stored0 & ~SHADOW_FLAG;
                if (depth0 < currentDepth0)
                {
                    buf[0] = static_cast<int16_t>((stored0 & SHADOW_FLAG) | depth0);
                    fb[0] = color;
                }
                depth += depthStep;

                int16_t depth1 = static_cast<int16_t>(depth);
                int16_t stored1 = buf[1];
                int16_t currentDepth1 = stored1 & ~SHADOW_FLAG;
                if (depth1 < currentDepth1)
                {
                    buf[1] = static_cast<int16_t>((stored1 & SHADOW_FLAG) | depth1);
                    fb[1] = color;
                }
                depth += depthStep;

                int16_t depth2 = static_cast<int16_t>(depth);
                int16_t stored2 = buf[2];
                int16_t currentDepth2 = stored2 & ~SHADOW_FLAG;
                if (depth2 < currentDepth2)
                {
                    buf[2] = static_cast<int16_t>((stored2 & SHADOW_FLAG) | depth2);
                    fb[2] = color;
                }
                depth += depthStep;

                int16_t depth3 = static_cast<int16_t>(depth);
                int16_t stored3 = buf[3];
                int16_t currentDepth3 = stored3 & ~SHADOW_FLAG;
                if (depth3 < currentDepth3)
                {
                    buf[3] = static_cast<int16_t>((stored3 & SHADOW_FLAG) | depth3);
                    fb[3] = color;
                }
                depth += depthStep;

                buf += 4;
                fb += 4;
                count -= 4;
            }

            while (count > 0)
            {
                int16_t d = static_cast<int16_t>(depth);
                int16_t stored = *buf;
                int16_t currentDepth = stored & ~SHADOW_FLAG;
                if (d < currentDepth)
                {
                    *buf = static_cast<int16_t>((stored & SHADOW_FLAG) | d);
                    *fb = color;
                }
                depth += depthStep;
                ++buf;
                ++fb;
                --count;
            }
        }

        ~ZBuffer()
        {
            if (buffer)
                ::pip3D::MemUtils::freeData(buffer);
        }
    };

}

#endif
