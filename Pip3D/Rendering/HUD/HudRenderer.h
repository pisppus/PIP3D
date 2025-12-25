#ifndef HUDRENDERER_H
#define HUDRENDERER_H

#include "../../Core/Core.h"
#include "../../Graphics/Font.h"
#include "../Display/FrameBuffer.h"

namespace pip3D
{
    class HudRenderer
    {
    public:
        static void drawText(FrameBuffer &framebuffer,
                             int16_t x, int16_t y,
                             const char *text,
                             uint16_t color);

        static uint16_t getAdaptiveTextColor(FrameBuffer &framebuffer,
                                             const Viewport &viewport,
                                             int16_t x, int16_t y,
                                             int16_t width = 40,
                                             int16_t height = 8);

        static int16_t getTextWidth(const char *text);
    };
}

namespace pip3D
{
    inline __attribute__((always_inline)) void HudRenderer::drawText(FrameBuffer &framebuffer,
                                                                     int16_t x, int16_t y,
                                                                     const char *text,
                                                                     uint16_t color)
    {
        uint16_t *fb = framebuffer.getBuffer();
        if (!fb || !text || !*text)
            return;

        const DisplayConfig &cfg = framebuffer.getConfig();
        BitmapFont::drawString(fb, x, y, text, color,
                               cfg.width, cfg.height);
    }

    inline __attribute__((always_inline)) uint16_t HudRenderer::getAdaptiveTextColor(FrameBuffer &framebuffer,
                                                                                     const Viewport &viewport,
                                                                                     int16_t x, int16_t y,
                                                                                     int16_t width,
                                                                                     int16_t height)
    {
        uint16_t *fb = framebuffer.getBuffer();
        if (!fb)
            return 0xFFFF;

        const DisplayConfig &cfg = framebuffer.getConfig();
        if (cfg.width == 0 || cfg.height == 0 || viewport.width == 0 || viewport.height == 0 ||
            width <= 0 || height <= 0)
            return 0xFFFF;

        uint32_t totalBrightness = 0;
        int samples = 0;

        for (int16_t dy = 0; dy < height && dy < 8; dy += 2)
        {
            int16_t sy = y + dy;
            if (sy < 0 || sy >= cfg.height)
                continue;

            for (int16_t dx = 0; dx < width && dx < 40; dx += 8)
            {
                int16_t sx = x + dx;
                if (sx < 0 || sx >= cfg.width)
                    continue;

                if (!viewport.contains(sx, sy))
                    continue;

                uint16_t pixel = fb[sy * cfg.width + sx];
                uint8_t r = ((pixel >> 11) & 0x1F) << 3;
                uint8_t g = ((pixel >> 5) & 0x3F) << 2;
                uint8_t b = (pixel & 0x1F) << 3;
                uint32_t brightness = (r * 299 + g * 587 + b * 114) / 1000;
                totalBrightness += brightness;
                samples++;
            }
        }

        if (samples == 0)
            return 0xFFFF;
        uint8_t avgBrightness = totalBrightness / samples;
        return (avgBrightness > 128) ? 0x0000 : 0xFFFF;
    }

    inline __attribute__((always_inline)) int16_t HudRenderer::getTextWidth(const char *text)
    {
        if (!text || !*text)
            return 0;
        return BitmapFont::getStringWidth(text);
    }
}

#endif
