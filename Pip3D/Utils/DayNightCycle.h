#ifndef DAYNIGHTCYCLE_H
#define DAYNIGHTCYCLE_H

#include "../Core/Core.h"
#include "../Core/Jobs.h"
#include "../Rendering/Renderer.h"

namespace pip3D
{

    struct TimeOfDayConfig
    {
        float dayLengthSeconds;
        float startHour;
        float baseIntensity;
        float nightIntensity;
        bool autoAdvance;

        TimeOfDayConfig()
            : dayLengthSeconds(120.0f), startHour(10.0f), baseIntensity(1.0f), nightIntensity(0.05f), autoAdvance(true) {}
    };

    class TimeOfDayController
    {
    public:
        TimeOfDayController(Renderer *r = nullptr)
            : renderer(r), timeMinutes(600.0f), dayLengthSeconds(120.0f), baseIntensity(1.0f), nightIntensity(0.05f), autoAdvance(true),
              pendingTime01(0.0f), cachedValid(false), jobInProgress(false) {}

        void init(Renderer *r, const TimeOfDayConfig &cfg)
        {
            renderer = r;
            dayLengthSeconds = cfg.dayLengthSeconds;
            baseIntensity = cfg.baseIntensity;
            nightIntensity = cfg.nightIntensity;
            autoAdvance = cfg.autoAdvance;
            setTime(cfg.startHour, 0.0f);
        }

        void setRenderer(Renderer *r) { renderer = r; }

        void setDayLengthSeconds(float seconds)
        {
            dayLengthSeconds = seconds;
        }

        void setAutoAdvance(bool enabled) { autoAdvance = enabled; }

        void setBaseIntensity(float intensity)
        {
            baseIntensity = intensity;
        }

        void setNightIntensity(float intensity)
        {
            nightIntensity = intensity;
        }

        void setTime(float hours, float minutes = 0.0f)
        {
            float h = hours;
            while (h < 0.0f)
                h += 24.0f;
            while (h >= 24.0f)
                h -= 24.0f;
            float m = clamp(minutes, 0.0f, 59.999f);
            timeMinutes = h * 60.0f + m;
            cachedValid = false;
            jobInProgress = false;
            applyToRenderer();
        }

        float getTimeHours() const
        {
            return timeMinutes / 60.0f;
        }

        float getTime01() const
        {
            return timeMinutes / 1440.0f;
        }

        __attribute__((hot)) void update(float deltaSeconds)
        {
            if (!renderer)
                return;

            if (autoAdvance && dayLengthSeconds > 0.0f && deltaSeconds > 0.0f)
            {
                float dayFrac = deltaSeconds / dayLengthSeconds;
                timeMinutes += 1440.0f * dayFrac;
                if (timeMinutes >= 1440.0f || timeMinutes < 0.0f)
                {
                    timeMinutes = fmodf(timeMinutes, 1440.0f);
                    if (timeMinutes < 0.0f)
                        timeMinutes += 1440.0f;
                }
            }
            if (JobSystem::isEnabled())
            {
                float t = timeMinutes / 1440.0f;
                if (!jobInProgress)
                {
                    pendingTime01 = t;
                    if (JobSystem::submit(&TimeOfDayController::skyJobFunc, this))
                    {
                        jobInProgress = true;
                    }
                }

                if (cachedValid)
                {
                    applySkyStateToRenderer(cachedState);
                }
            }
            else
            {
                applyToRenderer();
            }
        }

    private:
        struct SkyState
        {
            Color top;
            Color horizon;
            Color ground;
            Color sunColor;
            Vector3 sunDir;
            float intensity;
        };

        Renderer *renderer;
        float timeMinutes;
        float dayLengthSeconds;
        float baseIntensity;
        float nightIntensity;
        bool autoAdvance;
        SkyState cachedState;
        float pendingTime01;
        bool cachedValid;
        bool jobInProgress;

        void computeSkyState(float t, SkyState &out) const
        {
            if (t < 0.0f)
                t = 0.0f;
            if (t > 1.0f)
                t = 1.0f;

            static Skybox skyNight(SKYBOX_NIGHT);
            static Skybox skyDawn(SKYBOX_DAWN);
            static Skybox skyDay(SKYBOX_DAY);
            static Skybox skySunset(SKYBOX_SUNSET);

            const float TEMP_DAY = 5500.0f;
            const float TEMP_SUNSET = 2500.0f;
            const float TEMP_NIGHT = 8000.0f;
            const float TEMP_DAWN = 4000.0f;

            auto lerpColor = [](Color c1, Color c2, float k) -> Color
            {
                if (k <= 0.0f)
                    return c1;
                if (k >= 1.0f)
                    return c2;
                int r1 = (c1.rgb565 >> 11) & 0x1F;
                int g1 = (c1.rgb565 >> 5) & 0x3F;
                int b1 = c1.rgb565 & 0x1F;
                int r2 = (c2.rgb565 >> 11) & 0x1F;
                int g2 = (c2.rgb565 >> 5) & 0x3F;
                int b2 = c2.rgb565 & 0x1F;
                uint16_t r = (uint16_t)(r1 + (r2 - r1) * k);
                uint16_t g = (uint16_t)(g1 + (g2 - g1) * k);
                uint16_t b = (uint16_t)(b1 + (b2 - b1) * k);
                return Color((uint16_t)((r << 11) | (g << 5) | b));
            };

            Color top, horizon, ground;
            float lightTemp = TEMP_DAY;

            if (t < 0.25f)
            {
                float k = t / 0.25f;
                top = lerpColor(skyNight.top, skyDawn.top, k);
                horizon = lerpColor(skyNight.horizon, skyDawn.horizon, k);
                ground = lerpColor(skyNight.ground, skyDawn.ground, k);
                lightTemp = TEMP_NIGHT + (TEMP_DAWN - TEMP_NIGHT) * k;
            }
            else if (t < 0.5f)
            {
                float k = (t - 0.25f) / 0.25f;
                top = lerpColor(skyDawn.top, skyDay.top, k);
                horizon = lerpColor(skyDawn.horizon, skyDay.horizon, k);
                ground = lerpColor(skyDawn.ground, skyDay.ground, k);
                lightTemp = TEMP_DAWN + (TEMP_DAY - TEMP_DAWN) * k;
            }
            else if (t < 0.75f)
            {
                float k = (t - 0.5f) / 0.25f;
                top = lerpColor(skyDay.top, skySunset.top, k);
                horizon = lerpColor(skyDay.horizon, skySunset.horizon, k);
                ground = lerpColor(skyDay.ground, skySunset.ground, k);
                lightTemp = TEMP_DAY + (TEMP_SUNSET - TEMP_DAY) * k;
            }
            else
            {
                float k = (t - 0.75f) / 0.25f;
                top = lerpColor(skySunset.top, skyNight.top, k);
                horizon = lerpColor(skySunset.horizon, skyNight.horizon, k);
                ground = lerpColor(skySunset.ground, skyNight.ground, k);
                lightTemp = TEMP_SUNSET + (TEMP_NIGHT - TEMP_SUNSET) * k;
            }

            float dayAngle = (t - 0.25f) * TWO_PI;
            float elevation = sinf(dayAngle);

            float dayFactor = elevation > 0.0f ? elevation : 0.0f;
            dayFactor = clamp(dayFactor, 0.0f, 1.0f);

            float intensity = nightIntensity + (baseIntensity - nightIntensity) * dayFactor;

            float azimuth = t * TWO_PI;
            float sunX = cosf(azimuth) * 0.6f;
            float sunZ = sinf(azimuth) * 0.6f;
            Vector3 sunDir(sunX, -elevation, sunZ);
            sunDir.normalize();

            Color sunColor = Color::fromTemperature(lightTemp);

            out.top = top;
            out.horizon = horizon;
            out.ground = ground;
            out.sunColor = sunColor;
            out.sunDir = sunDir;
            out.intensity = intensity;
        }

        void applySkyStateToRenderer(const SkyState &state)
        {
            if (!renderer)
                return;

            Skybox &sky = renderer->getSkybox();
            sky.setCustom(state.top, state.horizon, state.ground);
            renderer->setMainDirectionalLight(state.sunDir, state.sunColor, state.intensity);
        }

        static void skyJobFunc(void *userData)
        {
            TimeOfDayController *self = static_cast<TimeOfDayController *>(userData);
            if (self)
                self->runSkyJob();
        }

        void runSkyJob()
        {
            SkyState local;
            float t = pendingTime01;
            computeSkyState(t, local);
            cachedState = local;
            cachedValid = true;
            jobInProgress = false;
        }

        __attribute__((hot)) void applyToRenderer()
        {
            if (!renderer)
                return;

            float t = timeMinutes / 1440.0f;
            SkyState state;
            computeSkyState(t, state);
            applySkyStateToRenderer(state);
        }
    };

}

#endif
