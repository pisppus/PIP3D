#ifndef FONT_H
#define FONT_H

#include "../Core/Core.h"
#include <Arduino.h>

namespace pip3D
{

    class BitmapFont
    {
    private:
        static constexpr uint8_t FONT_WIDTH = 5;
        static constexpr uint8_t FONT_HEIGHT = 7;
        static constexpr uint8_t CHAR_SPACING = 1;

        static const uint8_t font5x7[95][5];

    public:
        static __attribute__((always_inline)) inline void drawChar(uint16_t *framebuffer, int16_t x, int16_t y,
                                                                   char c, uint16_t color, int16_t screenWidth, int16_t screenHeight)
        {
            if (c < 32 || c > 126)
                c = '?';

            if (x >= screenWidth || y >= screenHeight ||
                x + FONT_WIDTH <= 0 || y + FONT_HEIGHT <= 0)
                return;

            const uint8_t *glyph = font5x7[c - 32];

            for (uint8_t col = 0; col < FONT_WIDTH; col++)
            {
                uint8_t line = glyph[col];
                if (!line)
                    continue;
                for (uint8_t row = 0; row < FONT_HEIGHT; row++)
                {
                    if (line & (1 << row))
                    {
                        int16_t px = x + col;
                        int16_t py = y + row;
                        if (px >= 0 && px < screenWidth && py >= 0 && py < screenHeight)
                        {
                            framebuffer[py * screenWidth + px] = color;
                        }
                    }
                }
            }
        }

        static __attribute__((always_inline)) inline void drawString(uint16_t *framebuffer, int16_t x, int16_t y,
                                                                     const char *text, uint16_t color,
                                                                     int16_t screenWidth, int16_t screenHeight)
        {
            if (!text || !*text)
                return;

            int16_t cursorX = x;
            while (*text)
            {
                drawChar(framebuffer, cursorX, y, *text, color, screenWidth, screenHeight);
                cursorX += FONT_WIDTH + CHAR_SPACING;
                text++;
            }
        }

        static __attribute__((always_inline)) inline int16_t getStringWidth(const char *text)
        {
            if (!text || !*text)
                return 0;

            int len = strlen(text);
            return len * (FONT_WIDTH + CHAR_SPACING) - CHAR_SPACING;
        }

        static constexpr uint8_t getCharWidth() { return FONT_WIDTH + CHAR_SPACING; }
        static constexpr uint8_t getCharHeight() { return FONT_HEIGHT; }
    };

}

#endif
