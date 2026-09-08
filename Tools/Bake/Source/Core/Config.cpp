#include <charconv>
#include <cstdlib>
#include <cstring>
#include <string_view>

#include "Core/Config.hpp"

namespace pip3D
{
    namespace Bake
    {
        void configureFromEnv(BakeConfig &cfg)
        {
            auto env = [](const char *name) -> const char *
            { return std::getenv(name); };

            if (const char *v = env("PIP3D_BAKE_MODE"))
            {
                cfg.modeAuto = false;
                if (!std::strcmp(v, "final") || !std::strcmp(v, "FINAL"))
                    cfg.mode = BakedLightMode::FINAL;
                else if (!std::strcmp(v, "off") || !std::strcmp(v, "OFF"))
                    cfg.mode = BakedLightMode::OFF;
                else
                    cfg.mode = BakedLightMode::FACTORED;
            }

            auto parseFloat = [](const char *s, float &out) -> bool
            {
                if (!s || !*s)
                    return false;
                std::string_view sv(s);
                float v = 0;
                auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), v);
                if (ec != std::errc{} || ptr == sv.data())
                    return false;
                if (!std::isfinite(v))
                    return false;
                out = v;
                return true;
            };
            auto floatEnv = [&](const char *name, float &dst)
            {
                if (const char *v = env(name))
                {
                    float f = 0;
                    if (parseFloat(v, f) && f > 0.0f)
                        dst = f;
                }
            };
            auto floatEnvAllowNeg = [&](const char *name, float &dst)
            {
                if (const char *v = env(name))
                {
                    float f = 0;
                    if (parseFloat(v, f))
                        dst = f;
                }
            };
            auto parseUint = [](const char *s, uint32_t &out) -> bool
            {
                if (!s || !*s)
                    return false;
                std::string_view sv(s);
                uint32_t v = 0;
                auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), v);
                if (ec != std::errc{} || ptr == sv.data())
                    return false;
                out = v;
                return true;
            };
            auto uintEnv = [&](const char *name, uint32_t &dst)
            {
                if (const char *v = env(name))
                {
                    uint32_t u = 0;
                    if (parseUint(v, u) && u >= 1)
                        dst = u;
                }
            };
            auto uintEnv0 = [&](const char *name, uint32_t &dst)
            {
                if (const char *v = env(name))
                {
                    if (v[0] == '0' && v[1] == '\0')
                    {
                        dst = 0;
                        return;
                    }
                    uint32_t u = 0;
                    if (parseUint(v, u))
                        dst = u;
                }
            };

            uintEnv("PIP3D_BAKE_LM_MIN", cfg.lmMin);
            uintEnv("PIP3D_BAKE_LM_MAX", cfg.lmMax);
            floatEnv("PIP3D_BAKE_TEXELS", cfg.texelsPerMeter);
            floatEnv("PIP3D_BAKE_MIN_TEXELS", cfg.minTexelsPerMeter);
            if (const char *v = env("PIP3D_BAKE_ADAPT_TEXELS"))
                cfg.adaptiveTexels = std::strcmp(v, "0") != 0;
            uintEnv("PIP3D_BAKE_AO_RAYS", cfg.aoRays);
            uintEnv("PIP3D_BAKE_SUN_RAYS", cfg.sunRays);
            uintEnv0("PIP3D_BAKE_GI_RAYS", cfg.giRays);
            uintEnv("PIP3D_BAKE_GI_NEE", cfg.giSkyNeeRays);
            floatEnv("PIP3D_BAKE_GI_GAIN", cfg.giGain);
            floatEnv("PIP3D_BAKE_GI_MAXALBEDO", cfg.giMaxAlbedo);
            floatEnvAllowNeg("PIP3D_BAKE_GI_DESAT", cfg.giDesat);
            uintEnv0("PIP3D_BAKE_DENOISE", cfg.denoisePasses);
            uintEnv0("PIP3D_BAKE_BLUR", cfg.lightmapBlurPasses);
            floatEnv("PIP3D_BAKE_BLUR_SIGMA", cfg.lightmapBlurSigma);
            floatEnv("PIP3D_BAKE_FLOOR_BOOST", cfg.floorTexelBoost);
            uintEnv0("PIP3D_BAKE_EMISSIVE_SAMPLES", cfg.emissiveSamples);
            floatEnv("PIP3D_BAKE_EMISSIVE_GAIN", cfg.emissiveGain);
            floatEnv("PIP3D_BAKE_AO_DIST", cfg.aoMaxDist);
            floatEnv("PIP3D_BAKE_PROBE_CELL", cfg.probeCell);
            floatEnv("PIP3D_BAKE_SUN_RADIUS", cfg.sunAngularRadius);

            if (const char *v = env("PIP3D_BAKE_DITHER"))
                cfg.dither = std::strcmp(v, "0") != 0;
            floatEnvAllowNeg("PIP3D_BAKE_SAT", cfg.saturation);
            floatEnvAllowNeg("PIP3D_BAKE_SKYNEUTRAL", cfg.skyNeutralize);

            if (const char *v = env("PIP3D_BAKE_SCENE"))
                cfg.sceneName = v;
            if (const char *v = env("PIP3D_BAKE_OUT"))
                cfg.outDir = v;
            if (const char *v = env("PIP3D_BAKE_APPLY"))
                cfg.applyToInstances = std::strcmp(v, "0") != 0;

            if (cfg.lmMin < 4)
                cfg.lmMin = 4;
            if (cfg.lmMax < cfg.lmMin)
                cfg.lmMax = cfg.lmMin;
            if (cfg.lmMax > 4096)
                cfg.lmMax = 4096;
            if (cfg.texelsPerMeter < 1.0f)
                cfg.texelsPerMeter = 1.0f;
            if (cfg.minTexelsPerMeter < 1.0f)
                cfg.minTexelsPerMeter = 1.0f;
            if (cfg.minTexelsPerMeter > cfg.texelsPerMeter)
                cfg.minTexelsPerMeter = cfg.texelsPerMeter;
            if (cfg.sunAngularRadius <= 0.0f || cfg.sunAngularRadius > 0.5f)
                cfg.sunAngularRadius = 0.025f;
            if (cfg.aoMaxDist < 1.0f)
                cfg.aoMaxDist = 1.0f;
            if (cfg.bias < 0.001f)
                cfg.bias = 0.001f;
            if (cfg.probeCell < 0.5f)
                cfg.probeCell = 0.5f;
        }
    }
}
