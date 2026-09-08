#pragma once

#include <cmath>
#include <cstdint>
#include <vector>

#include <Pip3D.hpp>
#include "Trace/Bvh.hpp"
#include "Core/Noise.hpp"
#include "Core/Data.hpp"

namespace pip3D
{
    namespace Bake
    {
        inline uint32_t randU32(uint32_t &state) noexcept { return Noise::pcg(state = state * 747796405u + 2891336453u); }
        inline float rand01(uint32_t &state) noexcept { return Noise::u01(randU32(state)); }

        inline uint64_t fnv1a64(const void *data, size_t bytes) noexcept
        {
            const uint8_t *p = static_cast<const uint8_t *>(data);
            uint64_t h = 1469598103934665603ull;
            for (size_t i = 0; i < bytes; ++i)
            {
                h ^= p[i];
                h *= 1099511628211ull;
            }
            return h;
        }

        inline Vector3 transformDir(const Matrix4x4 &m, const Vector3 &v)
        {
            return Vector3(m.m[0] * v.x + m.m[4] * v.y + m.m[8] * v.z,
                           m.m[1] * v.x + m.m[5] * v.y + m.m[9] * v.z,
                           m.m[2] * v.x + m.m[6] * v.y + m.m[10] * v.z);
        }

        inline Vector3 cross3(const Vector3 &a, const Vector3 &b)
        {
            return Vector3(a.y * b.z - a.z * b.y,
                           a.z * b.x - a.x * b.z,
                           a.x * b.y - a.y * b.x);
        }

        inline void tangentBasis(const Vector3 &n, Vector3 &t1, Vector3 &t2)
        {
            const Vector3 ref = (std::fabs(n.y) < 0.9f) ? Vector3(0.0f, 1.0f, 0.0f)
                                                        : Vector3(1.0f, 0.0f, 0.0f);
            t1 = cross3(ref, n);
            const float len = std::sqrt(t1.x * t1.x + t1.y * t1.y + t1.z * t1.z);
            if (len > 1e-6f)
                t1 = t1 * (1.0f / len);
            else
                t1 = Vector3(1.0f, 0.0f, 0.0f);
            t2 = cross3(n, t1);
        }

        inline Vector3 skyColorForDir(const Vector3 &dir, const Color &top,
                                      const Color &horizon, const Color &ground,
                                      float neutralize)
        {
            float tr, tg, tb, hr, hg, hb, gr, gg, gb;
            top.toFloat(tr, tg, tb);
            horizon.toFloat(hr, hg, hb);
            ground.toFloat(gr, gg, gb);

            const float y = dir.y;
            Vector3 c;
            if (y >= 0.0f)
            {
                const float t = std::fmin(1.0f, y * 1.6f);
                const float s = t * t * (3.0f - 2.0f * t);
                c = Vector3(hr + (tr - hr) * s, hg + (tg - hg) * s, hb + (tb - hb) * s);
            }
            else
            {
                const float t = std::fmin(1.0f, -y * 1.6f);
                const float s = t * t * (3.0f - 2.0f * t);
                c = Vector3(hr + (gr - hr) * s, hg + (gg - hg) * s, hb + (gb - hb) * s);
            }

            if (neutralize > 0.0f)
            {
                const float l = Noise::lum709(c.x, c.y, c.z);
                const float k = std::fmin(1.0f, neutralize);
                c = Vector3(c.x + (l - c.x) * k,
                            c.y + (l - c.y) * k,
                            c.z + (l - c.z) * k);
            }
            return c;
        }

        inline float smoothPositiveRT(float x, float eps)
        {
            if (x <= 0.0f)
                return 0.0f;
            if (x < eps)
            {
                const float u = x / eps;
                return u * u * (2.0f - u) * eps;
            }
            return x;
        }

        inline void giAlbedoControl(float &r, float &g, float &b,
                                    float maxLuma, float desat)
        {
            if (maxLuma > 0.0f)
            {
                const float l = Noise::lum709(r, g, b);
                if (l > maxLuma)
                {
                    const float k = maxLuma / l;
                    r *= k;
                    g *= k;
                    b *= k;
                }
            }
            if (desat > 0.0f)
            {
                const float l = Noise::lum709(r, g, b);
                const float k = std::fmin(1.0f, desat);
                r += (l - r) * k;
                g += (l - g) * k;
                b += (l - b) * k;
            }
        }

        struct ToneParams
        {
            float exposure = 1.05f;
            float knee = 0.30f;
            float saturation = 1.0f;
        };

        inline void toneMapRT(float &r, float &g, float &b, const ToneParams &tp)
        {
            const float inLum = Noise::lum709(r, g, b);
            if (inLum <= 1e-6f)
            {
                r = g = b = 0.0f;
                return;
            }

            const float outLum = (inLum * tp.exposure) / (tp.knee + inLum);
            const float lumScale = outLum / inLum;

            r *= lumScale;
            g *= lumScale;
            b *= lumScale;

            if (std::fabs(tp.saturation - 1.0f) > 1e-4f)
            {
                r = outLum + (r - outLum) * tp.saturation;
                g = outLum + (g - outLum) * tp.saturation;
                b = outLum + (b - outLum) * tp.saturation;
            }

            r = std::fmax(0.0f, std::fmin(1.0f, r));
            g = std::fmax(0.0f, std::fmin(1.0f, g));
            b = std::fmax(0.0f, std::fmin(1.0f, b));
        }

        inline uint16_t pack565Q(float r, float g, float b, int32_t x, int32_t y, bool dither)
        {
            if (dither)
                return Noise::pack565Dither(r, g, b, x, y);
            const uint32_t r5 = Noise::quantDither(std::fmin(1.0f, std::fmax(0.0f, r)), 32.0f, 0.5f);
            const uint32_t g6 = Noise::quantDither(std::fmin(1.0f, std::fmax(0.0f, g)), 64.0f, 0.5f);
            const uint32_t b5 = Noise::quantDither(std::fmin(1.0f, std::fmax(0.0f, b)), 32.0f, 0.5f);
            return static_cast<uint16_t>((r5 << 11) | (g6 << 5) | b5);
        }

        constexpr float kGoldenAngle = 2.39996323f;

        inline Vector3 sunDiskDir(const Vector3 &sunDir, const Vector3 &t1,
                                  const Vector3 &t2, uint32_t i, uint32_t n,
                                  float rot, float angRadius)
        {
            const float a = rot + kGoldenAngle * static_cast<float>(i);
            const float r = std::sqrt((static_cast<float>(i) + 0.5f) / static_cast<float>(n)) * angRadius;
            Vector3 d = sunDir + t1 * (std::cos(a) * r) + t2 * (std::sin(a) * r);
            const float l = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
            return d * (1.0f / l);
        }

        inline Vector3 cosineHemiDir(const Vector3 &nrm, const Vector3 &t1,
                                     const Vector3 &t2, uint32_t i, uint32_t cnt,
                                     float rot)
        {
            const float a = rot + kGoldenAngle * static_cast<float>(i);
            const float r = std::sqrt((static_cast<float>(i) + 0.5f) / static_cast<float>(cnt));
            const float z = std::sqrt(std::fmax(0.0f, 1.0f - r * r));
            return t1 * (std::cos(a) * r) + t2 * (std::sin(a) * r) + nrm * z;
        }

        inline Vector3 sunJitterDir(const Vector3 &sunDir, const Vector3 &t1,
                                    const Vector3 &t2, uint32_t &rng,
                                    float angRadius)
        {
            const float a = Noise::halton01(rng, 2) * 6.2831853f;
            const float r = std::sqrt(Noise::halton01(rng, 3)) * angRadius;
            Vector3 d = sunDir + t1 * (std::cos(a) * r) + t2 * (std::sin(a) * r);
            const float l = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
            return d * (1.0f / l);
        }

        inline Vector3 sampleEmissiveTris(const Vector3 &sp, const Vector3 *nrmOrNull,
                                          const std::vector<EmissiveTriLight> &em,
                                          float totalArea, uint32_t samples,
                                          float bias, uint32_t &rng, const BakeBVH &bvh)
        {
            if (em.empty() || totalArea <= 1e-8f || samples == 0)
                return Vector3(0.0f, 0.0f, 0.0f);

            Vector3 acc(0.0f, 0.0f, 0.0f);
            const float invSamples = 1.0f / static_cast<float>(samples);

            std::vector<float> triWeight(em.size());
            float totalLumArea = 0.0f;
            for (size_t i = 0; i < em.size(); ++i)
            {
                triWeight[i] = em[i].area * Noise::lum709(em[i].color.x, em[i].color.y, em[i].color.z);
                totalLumArea += triWeight[i];
            }
            if (totalLumArea <= 1e-6f)
                totalLumArea = totalArea;
            for (uint32_t s = 0; s < samples; ++s)
            {
                float pick = Noise::halton01(rng, 2) * totalLumArea;
                size_t ei = 0;
                for (; ei < em.size() - 1; ++ei)
                {
                    pick -= triWeight[ei];
                    if (pick <= 0.0f)
                        break;
                }
                const EmissiveTriLight &t = em[ei];

                const float r1 = Noise::halton01(rng, 2), r2 = Noise::halton01(rng, 3);
                const float su = 1.0f - std::sqrt(r1);
                const float sv = std::sqrt(r1) * (1.0f - r2);
                const float sw = std::sqrt(r1) * r2;
                const Vector3 p = t.v0 + t.e1 * sv + t.e2 * sw;

                Vector3 d = p - sp;
                const float distSq = d.x * d.x + d.y * d.y + d.z * d.z;
                const float dist = std::sqrt(std::fmax(distSq, 1e-6f));
                if (dist <= bias + 1e-3f)
                    continue;
                d = d * (1.0f / dist);

                float cosR = 1.0f;
                if (nrmOrNull)
                {
                    cosR = nrmOrNull->x * d.x + nrmOrNull->y * d.y + nrmOrNull->z * d.z;
                    if (cosR <= 0.0f)
                        continue;
                }
                const float cosE = std::fabs(t.n.x * d.x + t.n.y * d.y + t.n.z * d.z);
                if (cosE <= 0.0f)
                    continue;

                if (bvh.occluded(sp, d, dist - bias))
                    continue;

                const float effectiveDistSq = std::fmax(distSq, 0.04f);
                const float lum = Noise::lum709(t.color.x, t.color.y, t.color.z);
                const float effLum = lum > 1e-6f ? lum : 1e-6f;
                const float k = cosR * cosE * totalLumArea * invSamples / (effLum * effectiveDistSq);
                acc = acc + t.color * k;
            }
            return acc;
        }
    }
}
