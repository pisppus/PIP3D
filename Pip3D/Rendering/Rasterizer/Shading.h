#ifndef SHADING_H
#define SHADING_H

#include "../../Core/Core.h"
#include "../../Math/Math.h"
#include "../Lighting/Lighting.h"
#include "../Display/ZBuffer.h"

#ifndef IRAM_ATTR
#define IRAM_ATTR
#endif

namespace pip3D
{

    class Shading
    {
    public:
        static constexpr float AMBIENT_LIGHT = 0.06f;
        static constexpr float DIFFUSE_STRENGTH = 1.20f;
        static constexpr float SPECULAR_STRENGTH = 0.35f;
        static constexpr float RIM_STRENGTH = 0.25f;
        static constexpr float HDR_EXPOSURE = 1.8f;
        static constexpr float DIFFUSE_WRAP = 0.18f;
        static constexpr float CONTRAST = 1.35f;
        static constexpr float SATURATION = 1.30f;

        // Предвычисленные константы
        static constexpr float INV_DIFFUSE_WRAP = 1.0f / (1.0f + DIFFUSE_WRAP);
        static constexpr float HEMI_SCALE = 0.25f; // 0.5f * 0.5f оптимизировано
        static constexpr float AMBIENT_BASE = AMBIENT_LIGHT * 0.5f;
        static constexpr float CONTRAST_OFFSET = (0.5f - 0.5f * CONTRAST);
        static constexpr float INV_HDR_EXPOSURE = 1.0f / HDR_EXPOSURE;
        static constexpr float SATURATION_LUM_FACTOR = SATURATION - 1.0f;

        // LUT для гамма-коррекции (sqrt) - 256 значений
        static float GAMMA_LUT[256];
        static bool lutInitialized;

        static void initLUT()
        {
            if (lutInitialized)
                return;
            for (int i = 0; i < 256; i++)
            {
                float val = i / 255.0f;
                GAMMA_LUT[i] = sqrtf(val);
            }
            lutInitialized = true;
        }

        __attribute__((always_inline)) static inline float fastSqrt01(float x)
        {
            if (x <= 0.0f)
                return 0.0f;
            if (x >= 1.0f)
                return 1.0f;

            // Используем LUT для быстрого sqrt
            int idx = (int)(x * 255.0f);
            return GAMMA_LUT[idx];
        }

        __attribute__((always_inline, hot)) static inline void IRAM_ATTR calculateLighting(
            const Vector3 &fragPos,
            const Vector3 &normal,
            const Vector3 &viewDir,
            const Light *lights,
            int lightCount,
            float baseR, float baseG, float baseB,
            float &outR, float &outG, float &outB)
        {
            // Ambient с оптимизированным расчетом
            float hemi = normal.y * HEMI_SCALE + AMBIENT_BASE;
            float ambientTerm = AMBIENT_BASE + hemi;

            outR = baseR * ambientTerm;
            outG = baseG * ambientTerm;
            outB = baseB * ambientTerm;

            // Предвычисляем для rim lighting
            float NdotV = normal.dot(viewDir);
            NdotV = (NdotV < 0.0f) ? 0.0f : NdotV;
            float rim = 1.0f - NdotV;
            rim *= rim;
            float rimAmount = rim * RIM_STRENGTH;

            // Обработка источников света
            for (int i = 0; i < lightCount; i++)
            {
                const Light &light = lights[i];
                Vector3 lightDir;
                float attenuation = 1.0f;

                if (likely(light.type == LIGHT_DIRECTIONAL))
                {
                    lightDir.x = -light.direction.x;
                    lightDir.y = -light.direction.y;
                    lightDir.z = -light.direction.z;
                }
                else if (light.type == LIGHT_POINT)
                {
                    lightDir.x = light.position.x - fragPos.x;
                    lightDir.y = light.position.y - fragPos.y;
                    lightDir.z = light.position.z - fragPos.z;

                    const float range = light.range;
                    if (range > 0.0f)
                    {
                        float distSq = lightDir.x * lightDir.x +
                                       lightDir.y * lightDir.y +
                                       lightDir.z * lightDir.z;
                        float rangeSq = light.rangeSq;

                        if (distSq > rangeSq)
                            continue;

                        if (distSq > 1e-8f)
                        {
                            float invDist = FastMath::fastInvSqrt(distSq);
                            lightDir.x *= invDist;
                            lightDir.y *= invDist;
                            lightDir.z *= invDist;

                            float ratio = distSq * light.invRangeSq;
                            attenuation = 1.0f / (1.0f + ratio);
                        }
                    }
                    else
                    {
                        lightDir.normalize();
                    }
                }
                else
                {
                    continue;
                }

                // Diffuse с wrapped lighting
                float NdotL = normal.x * lightDir.x +
                              normal.y * lightDir.y +
                              normal.z * lightDir.z;

                float wrappedNdotL = (NdotL + DIFFUSE_WRAP) * INV_DIFFUSE_WRAP;
                wrappedNdotL = (wrappedNdotL < 0.0f) ? 0.0f : wrappedNdotL;

                float lightIntensityAtten = light.intensity * attenuation;
                float diffuse = wrappedNdotL * DIFFUSE_STRENGTH * lightIntensityAtten;

                // Specular - только если есть смысл считать
                float specular = 0.0f;
                if (NdotL > 0.0f && SPECULAR_STRENGTH > 0.0f)
                {
                    // Blinn-Phong halfway vector
                    float hx = lightDir.x + viewDir.x;
                    float hy = lightDir.y + viewDir.y;
                    float hz = lightDir.z + viewDir.z;

                    float hLenSq = hx * hx + hy * hy + hz * hz;
                    if (hLenSq > 1e-8f)
                    {
                        float invHLen = FastMath::fastInvSqrt(hLenSq);
                        hx *= invHLen;
                        hy *= invHLen;
                        hz *= invHLen;

                        float NdotH = normal.x * hx + normal.y * hy + normal.z * hz;
                        if (NdotH > 0.0f)
                        {
                            // Оптимизированное возведение в степень (power 16)
                            float spec = NdotH * NdotH; // ^2
                            spec *= spec;               // ^4
                            spec *= spec;               // ^8
                            spec *= spec;               // ^16
                            specular = spec * SPECULAR_STRENGTH * lightIntensityAtten;
                        }
                    }
                }

                // Накопление освещения
                float lightR, lightG, lightB;
                light.getCachedRGB(lightR, lightG, lightB);

                float diffuseR = baseR * diffuse;
                float diffuseG = baseG * diffuse;
                float diffuseB = baseB * diffuse;

                outR += (diffuseR + specular) * lightR;
                outG += (diffuseG + specular) * lightG;
                outB += (diffuseB + specular) * lightB;
            }

            // Rim lighting
            outR += baseR * rimAmount;
            outG += baseG * rimAmount;
            outB += baseB * rimAmount;

            // HDR tone mapping (Reinhard)
            outR = outR / (INV_HDR_EXPOSURE + outR);
            outG = outG / (INV_HDR_EXPOSURE + outG);
            outB = outB / (INV_HDR_EXPOSURE + outB);

            // Gamma correction через LUT
            outR = fastSqrt01(outR);
            outG = fastSqrt01(outG);
            outB = fastSqrt01(outB);

            // Saturation
            float lum = outR * 0.299f + outG * 0.587f + outB * 0.114f;
            outR += (outR - lum) * SATURATION_LUM_FACTOR;
            outG += (outG - lum) * SATURATION_LUM_FACTOR;
            outB += (outB - lum) * SATURATION_LUM_FACTOR;

            // Contrast
            outR = outR * CONTRAST + CONTRAST_OFFSET;
            outG = outG * CONTRAST + CONTRAST_OFFSET;
            outB = outB * CONTRAST + CONTRAST_OFFSET;

            // Clamp
            outR = (outR < 0.0f) ? 0.0f : ((outR > 1.0f) ? 1.0f : outR);
            outG = (outG < 0.0f) ? 0.0f : ((outG > 1.0f) ? 1.0f : outG);
            outB = (outB < 0.0f) ? 0.0f : ((outB > 1.0f) ? 1.0f : outB);
        }

        __attribute__((always_inline, hot)) static inline uint16_t IRAM_ATTR applyDithering(float r, float g, float b, int16_t x, int16_t y)
        {
            // Масштабирование к целевому диапазону
            r *= 31.0f;
            g *= 63.0f;
            b *= 31.0f;

            // Bayer dithering
            float bayerValue = BAYER_MATRIX_4X4[y & 3][x & 3];

            float ditherRB = bayerValue * 0.5f;
            float ditherG = bayerValue * 0.25f;

            int ir = (int)(r + ditherRB);
            int ig = (int)(g + ditherG);
            int ib = (int)(b + ditherRB);

            // Clamp через conditional move
            uint16_t rc = (ir > 31) ? 31 : ((ir < 0) ? 0 : ir);
            uint16_t gc = (ig > 63) ? 63 : ((ig < 0) ? 0 : ig);
            uint16_t bc = (ib > 31) ? 31 : ((ib < 0) ? 0 : ib);

            return (rc << 11) | (gc << 5) | bc;
        }
    };

}

#endif