#ifndef TIMEUTILS_H
#define TIMEUTILS_H

#include <Arduino.h>
#include <stdint.h>

namespace pip3D
{

    inline float getDeltaTime()
    {
        static uint32_t lastMicros = 0;
        uint32_t now = micros();
        if (lastMicros == 0)
        {
            lastMicros = now;
            return 0.0f;
        }
        uint32_t diff = now - lastMicros;
        lastMicros = now;
        float dt = diff * 1e-6f;
        if (dt < 0.0f)
        {
            dt = 0.0f;
        }
        if (dt > 0.1f)
        {
            dt = 0.1f;
        }
        return dt;
    }

} // namespace pip3D

#endif
