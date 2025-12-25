#ifndef SCREEN_H
#define SCREEN_H

#include "../Rendering/Display/Drivers/ST7789Driver.h"
#include "../Rendering/Display/Drivers/DisplayConfig.h"

namespace pip3D
{

    class Screen
    {
    private:
        ST7789Driver display;

    public:
        Screen() = default;

        bool begin()
        {
            return display.init(S3());
        }

        bool begin(int8_t cs, int8_t dc, int8_t rst = 8)
        {
            return display.init(S3(cs, dc, rst));
        }

        bool begin(const LCD &config)
        {
            return display.init(config);
        }

        void pixel(int16_t x, int16_t y, uint16_t color)
        {
            display.drawPixel(x, y, color);
        }

        void rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color)
        {
            display.fillRect(x, y, w, h, color);
        }

        void clear(uint16_t color = Color::BLACK)
        {
            display.fillScreen(color);
        }

        void show(uint16_t *buffer)
        {
            display.pushImage(0, 0, display.getWidth(), display.getHeight(), buffer);
        }

        void show(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t *buffer)
        {
            display.pushImage(x, y, w, h, buffer);
        }

        uint16_t width() const { return display.getWidth(); }
        uint16_t height() const { return display.getHeight(); }

        ST7789Driver &raw() { return display; }
    };

}

#endif
