#ifndef DISPLAYDRIVERBASE_H
#define DISPLAYDRIVERBASE_H

#include "DisplayConfig.h"

namespace pip3D
{

    class DisplayDriverBase
    {
    public:
        virtual ~DisplayDriverBase() = default;

        virtual bool init(const LCD &config) = 0;

        virtual void pushImage(int16_t x,
                               int16_t y,
                               int16_t w,
                               int16_t h,
                               uint16_t *buffer) = 0;

        virtual uint16_t getWidth() const = 0;
        virtual uint16_t getHeight() const = 0;
    };

}

#endif
