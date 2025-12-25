#ifndef PIP3D_INPUT_H
#define PIP3D_INPUT_H

#include <Arduino.h>
#include <math.h>

namespace pip3D
{
    namespace input
    {
        struct ButtonConfig
        {
            uint8_t pin;
            bool activeLow;
            uint16_t debounceMs;

            constexpr ButtonConfig(uint8_t p = 0xFF, bool low = true, uint16_t db = 30)
                : pin(p), activeLow(low), debounceMs(db) {}
        };

        class Button
        {
        private:
            ButtonConfig cfg;
            bool stableState;
            bool lastStableState;
            bool justPressedFlag;
            bool justReleasedFlag;
            uint32_t lastChangeTime;
            bool initialized;

        public:
            constexpr Button(const ButtonConfig &c = ButtonConfig())
                : cfg(c),
                  stableState(false),
                  lastStableState(false),
                  justPressedFlag(false),
                  justReleasedFlag(false),
                  lastChangeTime(0),
                  initialized(false)
            {
            }

            void begin()
            {
                if (cfg.pin == 0xFF)
                    return;
                pinMode(cfg.pin, cfg.activeLow ? INPUT_PULLUP : INPUT);
                bool raw = readRaw();
                stableState = raw;
                lastStableState = raw;
                lastChangeTime = millis();
                justPressedFlag = false;
                justReleasedFlag = false;
                initialized = true;
            }

        private:
            bool readRaw() const
            {
                int v = digitalRead(cfg.pin);
                bool pressed = cfg.activeLow ? (v == LOW) : (v == HIGH);
                return pressed;
            }

        public:
            __attribute__((always_inline)) inline void update()
            {
                if (!initialized)
                    return;

                bool raw = readRaw();
                uint32_t now = millis();
                justPressedFlag = false;
                justReleasedFlag = false;

                if (raw != stableState)
                {
                    if (now - lastChangeTime >= cfg.debounceMs)
                    {
                        lastStableState = stableState;
                        stableState = raw;
                        lastChangeTime = now;

                        if (!lastStableState && stableState)
                            justPressedFlag = true;
                        else if (lastStableState && !stableState)
                            justReleasedFlag = true;
                    }
                }
                else
                {
                    lastChangeTime = now;
                }
            }

            bool isPressed() const { return stableState; }
            bool wasPressed() const { return justPressedFlag; }
            bool wasReleased() const { return justReleasedFlag; }
        };

        struct AnalogAxisConfig
        {
            uint8_t pin;
            int16_t minValue;
            int16_t maxValue;
            float deadZone;
            bool inverted;

            constexpr AnalogAxisConfig(uint8_t p = 0xFF,
                                       int16_t minV = 0,
                                       int16_t maxV = 4095,
                                       float dz = 0.12f,
                                       bool inv = false)
                : pin(p),
                  minValue(minV),
                  maxValue(maxV),
                  deadZone(dz),
                  inverted(inv)
            {
            }
        };

        class AnalogAxis
        {
        private:
            AnalogAxisConfig cfg;
            float filtered;
            bool initialized;
            float rangeInv;
            float deadZone;
            float invDeadSpan;

        public:
            constexpr AnalogAxis(const AnalogAxisConfig &c = AnalogAxisConfig())
                : cfg(c), filtered(0.0f), initialized(false), rangeInv(0.0f), deadZone(c.deadZone), invDeadSpan(1.0f)
            {
            }

            void begin()
            {
                if (cfg.pin == 0xFF)
                    return;
                filtered = 0.0f;
                initialized = true;

                if (cfg.maxValue > cfg.minValue)
                {
                    rangeInv = 1.0f / (float)(cfg.maxValue - cfg.minValue);
                }
                else
                {
                    rangeInv = 0.0f;
                }

                deadZone = cfg.deadZone;
                if (deadZone < 0.0f)
                    deadZone = 0.0f;
                if (deadZone > 0.999f)
                    deadZone = 0.999f;
                float span = 1.0f - deadZone;
                invDeadSpan = (span > 1e-6f) ? (1.0f / span) : 1.0f;
            }

            __attribute__((always_inline)) inline float update(float deltaTime)
            {
                if (!initialized)
                    return filtered;

                int raw = analogRead(cfg.pin);
                float v = 0.0f;
                if (rangeInv > 0.0f)
                {
                    float n = (float)(raw - cfg.minValue) * rangeInv;
                    if (n < 0.0f)
                        n = 0.0f;
                    if (n > 1.0f)
                        n = 1.0f;
                    v = n * 2.0f - 1.0f;
                }

                if (cfg.inverted)
                    v = -v;

                float av = fabsf(v);
                if (av < deadZone)
                {
                    v = 0.0f;
                }
                else
                {
                    float k = (av - deadZone) * invDeadSpan;
                    if (k > 1.0f)
                        k = 1.0f;
                    v = (v > 0.0f ? 1.0f : -1.0f) * k;
                }

                float alpha;
                if (deltaTime > 0.0f)
                {
                    float cutoff = 16.0f;
                    alpha = cutoff * deltaTime;
                    if (alpha > 1.0f)
                        alpha = 1.0f;
                }
                else
                {
                    alpha = 0.25f;
                }

                filtered += (v - filtered) * alpha;
                return filtered;
            }

            float value() const { return filtered; }
        };

        struct JoystickConfig
        {
            AnalogAxisConfig axisX;
            AnalogAxisConfig axisY;
            ButtonConfig button;

            constexpr JoystickConfig() {}
            constexpr JoystickConfig(const AnalogAxisConfig &x,
                                     const AnalogAxisConfig &y,
                                     const ButtonConfig &b)
                : axisX(x), axisY(y), button(b) {}
        };

        class Joystick
        {
        private:
            AnalogAxis ax;
            AnalogAxis ay;
            Button btn;

        public:
            constexpr Joystick(const JoystickConfig &cfg = JoystickConfig())
                : ax(cfg.axisX), ay(cfg.axisY), btn(cfg.button)
            {
            }

            void begin()
            {
                ax.begin();
                ay.begin();
                btn.begin();
            }

            void update(float deltaTime)
            {
                ax.update(deltaTime);
                ay.update(deltaTime);
                btn.update();
            }

            float x() const { return ax.value(); }
            float y() const { return ay.value(); }

            bool isPressed() const { return btn.isPressed(); }
            bool wasPressed() const { return btn.wasPressed(); }
            bool wasReleased() const { return btn.wasReleased(); }
        };
    }
}

#endif
