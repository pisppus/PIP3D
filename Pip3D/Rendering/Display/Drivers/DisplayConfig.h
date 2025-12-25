#ifndef DISPLAYCONFIG_H
#define DISPLAYCONFIG_H

#include "../../../Core/Core.h"

namespace pip3D
{

    struct LCD
    {
        uint16_t w = 320, h = 240;
        int8_t cs = 10, dc = 9, rst = 8, bl = -1;
        uint32_t freq = 80000000;

        LCD() = default;
        LCD(int8_t cs_, int8_t dc_, int8_t rst_ = 8) : cs(cs_), dc(dc_), rst(rst_) {}

        static LCD pins(int8_t cs, int8_t dc, int8_t rst = 8)
        {
            return LCD(cs, dc, rst);
        }

        LCD &size(uint16_t width, uint16_t height)
        {
            w = width;
            h = height;
            return *this;
        }
        LCD &backlight(int8_t pin)
        {
            bl = pin;
            return *this;
        }
        LCD &speed(uint32_t mhz)
        {
            freq = mhz * 1000000;
            return *this;
        }
    };

    static LCD S3() { return LCD(); }
    static LCD S3(int8_t cs, int8_t dc, int8_t rst = 8) { return LCD::pins(cs, dc, rst); }

}

#endif
