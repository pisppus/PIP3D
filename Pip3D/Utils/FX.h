#ifndef FX_H
#define FX_H

#include "../Core/Core.h"
#include "../Core/Debug/Logging.h"
#include "../Math/Math.h"
#include "../Rendering/Renderer.h"
#include <vector>

namespace pip3D
{

    struct Particle
    {
        Vector3 position;
        Vector3 velocity;
        float lifetime;
        float age;
        Color startColor;
        Color endColor;
        float startSize;
        float endSize;
        bool alive;

        Particle()
            : position(0, 0, 0), velocity(0, 0, 0), lifetime(1.0f), age(0.0f),
              startColor(Color::WHITE), endColor(Color::WHITE),
              startSize(4.0f), endSize(0.0f),
              alive(false) {}
    };

    struct ParticleEmitterConfig
    {
        uint16_t maxParticles;
        float emitRate;
        float minLifetime;
        float maxLifetime;
        float initialSpeed;
        float spread;
        Vector3 acceleration;
        Color startColor;
        Color endColor;
        float startSize;
        float endSize;
        bool looping;
        bool additive;

        ParticleEmitterConfig()
            : maxParticles(64), emitRate(30.0f),
              minLifetime(0.4f), maxLifetime(0.8f),
              initialSpeed(1.0f), spread(0.4f),
              acceleration(0.0f, 0.0f, 0.0f),
              startColor(Color::fromRGB888(255, 255, 255)),
              endColor(Color::fromRGB888(0, 0, 0)),
              startSize(4.0f), endSize(0.0f),
              looping(true), additive(false) {}
    };

    class ParticleEmitter
    {
    private:
        Vector3 position;
        Vector3 velocityOffset;
        ParticleEmitterConfig config;
        std::vector<Particle> particles;
        float emitAccumulator;
        bool enabled;

    public:
        ParticleEmitter(const ParticleEmitterConfig &cfg, const Vector3 &pos = Vector3())
            : position(pos), velocityOffset(0, 0, 0), config(cfg),
              emitAccumulator(0.0f), enabled(true)
        {
            particles.resize(config.maxParticles);
        }

        void setPosition(const Vector3 &pos) { position = pos; }
        const Vector3 &getPosition() const { return position; }

        void setVelocityOffset(const Vector3 &v) { velocityOffset = v; }

        void setEnabled(bool e) { enabled = e; }
        bool isEnabled() const { return enabled; }

        void triggerBurst(int count)
        {
            if (count <= 0)
                return;
            for (int i = 0; i < count; ++i)
            {
                spawnParticle();
            }
        }

        void update(float dt)
        {
            if (dt <= 0.0f)
            {
                if (dt < 0.0f)
                {
                    LOGW(::pip3D::Debug::LOG_MODULE_SCENE,
                         "ParticleEmitter::update called with negative dt=%.6f",
                         static_cast<double>(dt));
                }
                return;
            }

            if (enabled && config.emitRate > 0.0f && config.looping)
            {
                emitAccumulator += config.emitRate * dt;
                int toEmit = (int)emitAccumulator;
                emitAccumulator -= toEmit;
                for (int i = 0; i < toEmit; ++i)
                {
                    spawnParticle();
                }
            }

            for (size_t i = 0; i < particles.size(); ++i)
            {
                Particle &p = particles[i];
                if (!p.alive)
                    continue;

                p.age += dt;
                if (p.age >= p.lifetime)
                {
                    p.alive = false;
                    continue;
                }

                p.velocity += config.acceleration * dt;
                p.position += p.velocity * dt;
            }
        }

        void render(Renderer &renderer) const
        {
            const Viewport &vp = renderer.getViewport();
            const int16_t width = vp.width;
            const int16_t height = vp.height;

#if PIP3D_TILED_RENDERING
            // В тайловом режиме рисуем частицы в текущий тайл через tileColorBuffer.
            // Предполагается, что beginTileRender уже вызван, tileActive=true.
            if (!renderer.isTileActive() || !renderer.getTileColorBuffer())
            {
                return;
            }

            uint16_t *tileBuffer = renderer.getTileColorBuffer();
            int16_t tileX = renderer.getCurrentTileX();
            int16_t tileY = renderer.getCurrentTileY();
            int16_t tileW = renderer.getCurrentTileW();
            int16_t tileH = renderer.getCurrentTileH();

            for (size_t i = 0; i < particles.size(); ++i)
            {
                const Particle &p = particles[i];
                if (!p.alive)
                    continue;

                float t = p.age / p.lifetime;
                if (t < 0.0f)
                    t = 0.0f;
                if (t > 1.0f)
                    t = 1.0f;

                uint8_t alpha = (uint8_t)((1.0f - t) * 255.0f);
                if (alpha == 0)
                    continue;

                Color col = p.startColor.blend(p.endColor, (uint8_t)(t * 255.0f));
                float size = p.startSize + (p.endSize - p.startSize) * t;
                if (size <= 0.25f)
                    size = 0.25f;

                Vector3 screen = renderer.project(p.position);
                if (screen.z <= 0.0f)
                    continue;

                int cx = (int)screen.x;
                int cy = (int)screen.y;
                int radius = (int)size;
                if (radius <= 0)
                    radius = 1;

                int r2 = radius * radius;

                int y0 = clamp(cy - radius, 0, (int)height - 1);
                int y1 = clamp(cy + radius, 0, (int)height - 1);

                // Ограничиваем по текущему тайлу
                if (y1 < tileY || y0 >= tileY + tileH)
                    continue;

                for (int y = y0; y <= y1; ++y)
                {
                    if (y < tileY || y >= tileY + tileH)
                        continue;

                    int dy = y - cy;
                    int dy2 = dy * dy;

                    int x0 = clamp(cx - radius, 0, (int)width - 1);
                    int x1 = clamp(cx + radius, 0, (int)width - 1);

                    if (x1 < tileX || x0 >= tileX + tileW)
                        continue;

                    for (int x = x0; x <= x1; ++x)
                    {
                        if (x < tileX || x >= tileX + tileW)
                            continue;

                        int dx = x - cx;
                        int dist2 = dx * dx + dy2;
                        if (dist2 > r2)
                            continue;

                        float k = 1.0f - (float)dist2 / (float)r2;
                        uint8_t a = (uint8_t)(alpha * k);
                        if (a == 0)
                            continue;

                        int localX = x - tileX;
                        int localY = y - tileY;
                        size_t idx = (size_t)localY * (size_t)tileW + (size_t)localX;

                        if (config.additive)
                        {
                            const uint16_t dst = tileBuffer[idx];
                            const uint16_t src = col.rgb565;

                            uint32_t rDst = (dst >> 11) & 0x1F;
                            uint32_t gDst = (dst >> 5) & 0x3F;
                            uint32_t bDst = dst & 0x1F;

                            const uint32_t rSrc = (src >> 11) & 0x1F;
                            const uint32_t gSrc = (src >> 5) & 0x3F;
                            const uint32_t bSrc = src & 0x1F;

                            rDst += (rSrc * a) >> 8;
                            gDst += (gSrc * a) >> 8;
                            bDst += (bSrc * a) >> 8;

                            if (rDst > 31u)
                                rDst = 31u;
                            if (gDst > 63u)
                                gDst = 63u;
                            if (bDst > 31u)
                                bDst = 31u;

                            tileBuffer[idx] = (uint16_t)((rDst << 11) | (gDst << 5) | bDst);
                        }
                        else
                        {
                            Color base(tileBuffer[idx]);
                            Color blended = base.blend(col, a);
                            tileBuffer[idx] = blended.rgb565;
                        }
                    }
                }
            }
#else
            uint16_t *fb = renderer.getFrameBuffer();
            if (!fb)
            {
                LOGE(::pip3D::Debug::LOG_MODULE_RENDER,
                     "ParticleEmitter::render: framebuffer is null");
                return;
            }

            for (size_t i = 0; i < particles.size(); ++i)
            {
                const Particle &p = particles[i];
                if (!p.alive)
                    continue;

                float t = p.age / p.lifetime;
                if (t < 0.0f)
                    t = 0.0f;
                if (t > 1.0f)
                    t = 1.0f;

                uint8_t alpha = (uint8_t)((1.0f - t) * 255.0f);
                if (alpha == 0)
                    continue;

                Color col = p.startColor.blend(p.endColor, (uint8_t)(t * 255.0f));
                float size = p.startSize + (p.endSize - p.startSize) * t;
                if (size <= 0.25f)
                    size = 0.25f;

                Vector3 screen = renderer.project(p.position);
                if (screen.z <= 0.0f)
                    continue;

                int cx = (int)screen.x;
                int cy = (int)screen.y;
                int radius = (int)size;
                if (radius <= 0)
                    radius = 1;

                int r2 = radius * radius;

                int y0 = clamp(cy - radius, 0, (int)height - 1);
                int y1 = clamp(cy + radius, 0, (int)height - 1);

                for (int y = y0; y <= y1; ++y)
                {
                    int dy = y - cy;
                    int dy2 = dy * dy;
                    int x0 = clamp(cx - radius, 0, (int)width - 1);
                    int x1 = clamp(cx + radius, 0, (int)width - 1);
                    size_t idx = (size_t)y * width + x0;
                    for (int x = x0; x <= x1; ++x, ++idx)
                    {
                        int dx = x - cx;
                        int dist2 = dx * dx + dy2;
                        if (dist2 > r2)
                            continue;
                        float k = 1.0f - (float)dist2 / (float)r2;
                        uint8_t a = (uint8_t)(alpha * k);
                        if (a == 0)
                            continue;

                        if (config.additive)
                        {
                            const uint16_t dst = fb[idx];
                            const uint16_t src = col.rgb565;

                            uint32_t rDst = (dst >> 11) & 0x1F;
                            uint32_t gDst = (dst >> 5) & 0x3F;
                            uint32_t bDst = dst & 0x1F;

                            const uint32_t rSrc = (src >> 11) & 0x1F;
                            const uint32_t gSrc = (src >> 5) & 0x3F;
                            const uint32_t bSrc = src & 0x1F;

                            rDst += (rSrc * a) >> 8;
                            gDst += (gSrc * a) >> 8;
                            bDst += (bSrc * a) >> 8;

                            if (rDst > 31u)
                                rDst = 31u;
                            if (gDst > 63u)
                                gDst = 63u;
                            if (bDst > 31u)
                                bDst = 31u;

                            fb[idx] = (uint16_t)((rDst << 11) | (gDst << 5) | bDst);
                        }
                        else
                        {
                            Color base(fb[idx]);
                            Color blended = base.blend(col, a);
                            fb[idx] = blended.rgb565;
                        }
                    }
                }
            }
#endif
        }

    private:
        void spawnParticle()
        {
            for (size_t i = 0; i < particles.size(); ++i)
            {
                Particle &p = particles[i];
                if (p.alive)
                    continue;

                float life = randomRange(config.minLifetime, config.maxLifetime);
                if (life <= 0.0f)
                    life = 0.1f;

                float rx = random01() - 0.5f;
                float rz = random01() - 0.5f;
                Vector3 dir(rx * config.spread, 1.0f, rz * config.spread);
                dir.normalize();
                Vector3 vel = dir * config.initialSpeed + velocityOffset;

                p.position = position;
                p.velocity = vel;
                p.lifetime = life;
                p.age = 0.0f;
                p.startColor = config.startColor;
                p.endColor = config.endColor;
                p.startSize = config.startSize;
                p.endSize = config.endSize;
                p.alive = true;
                return;
            }

            LOGW(::pip3D::Debug::LOG_MODULE_SCENE,
                 "ParticleEmitter::spawnParticle: no free particles (maxParticles=%u)",
                 static_cast<unsigned int>(particles.size()));
        }

        static float random01()
        {
            return (float)random(0L, 32767L) / 32767.0f;
        }

        static float randomRange(float a, float b)
        {
            float t = random01();
            return a + (b - a) * t;
        }
    };

    class FXSystem
    {
    private:
        std::vector<ParticleEmitter *> emitters;

    public:
        FXSystem() {}

        FXSystem(const FXSystem &) = delete;
        FXSystem &operator=(const FXSystem &) = delete;
        FXSystem(FXSystem &&) = delete;
        FXSystem &operator=(FXSystem &&) = delete;

        ~FXSystem()
        {
            clear();
        }

        ParticleEmitter *createEmitter(const ParticleEmitterConfig &cfg, const Vector3 &pos = Vector3())
        {
            ParticleEmitter *e = new ParticleEmitter(cfg, pos);
            emitters.push_back(e);
            return e;
        }

        void destroyEmitter(ParticleEmitter *emitter)
        {
            if (!emitter)
            {
                LOGW(::pip3D::Debug::LOG_MODULE_SCENE,
                     "FXSystem::destroyEmitter called with null emitter");
                return;
            }
            for (size_t i = 0; i < emitters.size(); ++i)
            {
                if (emitters[i] == emitter)
                {
                    delete emitters[i];
                    emitters[i] = emitters.back();
                    emitters.pop_back();
                    return;
                }
            }

            LOGW(::pip3D::Debug::LOG_MODULE_SCENE,
                 "FXSystem::destroyEmitter: emitter not found in list (count=%u)",
                 static_cast<unsigned int>(emitters.size()));
        }

        void clear()
        {
            for (size_t i = 0; i < emitters.size(); ++i)
            {
                delete emitters[i];
            }
            emitters.clear();
        }

        void update(float dt)
        {
            for (size_t i = 0; i < emitters.size(); ++i)
            {
                emitters[i]->update(dt);
            }
        }

        void render(Renderer &renderer) const
        {
            for (size_t i = 0; i < emitters.size(); ++i)
            {
                emitters[i]->render(renderer);
            }
        }

        ParticleEmitter *createFire(const Vector3 &pos)
        {
            ParticleEmitterConfig cfg;
            cfg.maxParticles = 72;
            cfg.emitRate = 50.0f;
            cfg.minLifetime = 0.4f;
            cfg.maxLifetime = 0.8f;
            cfg.initialSpeed = 1.2f;
            cfg.spread = 0.6f;
            cfg.acceleration = Vector3(0.0f, 2.0f, 0.0f);
            cfg.startColor = Color::fromRGB888(255, 230, 180);
            cfg.endColor = Color::fromRGB888(120, 30, 0);
            cfg.startSize = 6.0f;
            cfg.endSize = 3.0f;
            cfg.looping = true;
            cfg.additive = true;
            return createEmitter(cfg, pos);
        }

        ParticleEmitter *createSmoke(const Vector3 &pos)
        {
            ParticleEmitterConfig cfg;
            cfg.maxParticles = 56;
            cfg.emitRate = 28.0f;
            cfg.minLifetime = 1.2f;
            cfg.maxLifetime = 2.0f;
            cfg.initialSpeed = 0.5f;
            cfg.spread = 0.4f;
            cfg.acceleration = Vector3(0.0f, 0.4f, 0.0f);
            cfg.startColor = Color::fromRGB888(200, 200, 200);
            cfg.endColor = Color::fromRGB888(70, 70, 70);
            cfg.startSize = 9.0f;
            cfg.endSize = 16.0f;
            cfg.looping = true;
            cfg.additive = false;
            return createEmitter(cfg, pos);
        }

        ParticleEmitter *createExplosion(const Vector3 &pos)
        {
            ParticleEmitterConfig cfg;
            cfg.maxParticles = 64;
            cfg.emitRate = 0.0f;
            cfg.minLifetime = 0.4f;
            cfg.maxLifetime = 0.9f;
            cfg.initialSpeed = 4.0f;
            cfg.spread = 1.0f;
            cfg.acceleration = Vector3(0.0f, -3.0f, 0.0f);
            cfg.startColor = Color::fromRGB888(255, 230, 160);
            cfg.endColor = Color::fromRGB888(90, 20, 0);
            cfg.startSize = 6.0f;
            cfg.endSize = 10.0f;
            cfg.looping = false;
            cfg.additive = true;

            ParticleEmitter *e = createEmitter(cfg, pos);
            e->triggerBurst(cfg.maxParticles);
            return e;
        }

        ParticleEmitter *createSparks(const Vector3 &pos)
        {
            ParticleEmitterConfig cfg;
            cfg.maxParticles = 40;
            cfg.emitRate = 0.0f;
            cfg.minLifetime = 0.3f;
            cfg.maxLifetime = 0.6f;
            cfg.initialSpeed = 5.0f;
            cfg.spread = 1.0f;
            cfg.acceleration = Vector3(0.0f, -4.0f, 0.0f);
            cfg.startColor = Color::fromRGB888(255, 255, 200);
            cfg.endColor = Color::fromRGB888(255, 120, 60);
            cfg.startSize = 3.0f;
            cfg.endSize = 1.0f;
            cfg.looping = false;
            cfg.additive = true;

            ParticleEmitter *e = createEmitter(cfg, pos);
            e->triggerBurst(cfg.maxParticles / 2);
            return e;
        }

        ParticleEmitter *createTrail(const Vector3 &pos)
        {
            ParticleEmitterConfig cfg;
            cfg.maxParticles = 64;
            cfg.emitRate = 40.0f;
            cfg.minLifetime = 0.4f;
            cfg.maxLifetime = 0.8f;
            cfg.initialSpeed = 0.3f;
            cfg.spread = 0.4f;
            cfg.acceleration = Vector3(0.0f, 0.0f, 0.0f);
            cfg.startColor = Color::fromRGB888(200, 220, 255);
            cfg.endColor = Color::fromRGB888(80, 120, 220);
            cfg.startSize = 3.0f;
            cfg.endSize = 1.0f;
            cfg.looping = true;
            cfg.additive = true;
            return createEmitter(cfg, pos);
        }
    };

}

#endif
