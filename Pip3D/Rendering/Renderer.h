#ifndef RENDERER_H
#define RENDERER_H

#include "../Core/Core.h"
#include "../Core/Debug/DebugDraw.h"
#include "../Core/Camera.h"
#include "../Core/Frustum.h"
#include "../Core/Instance.h"
#include "../Core/Jobs.h"
#include "../Math/Math.h"
#include "../Geometry/Mesh.h"
#include "../Graphics/Font.h"
#include "Display/ZBuffer.h"
#include "Display/DirtyRegions.h"
#include "Lighting/Lighting.h"
#include "Lighting/LightManager.h"
#include "Lighting/Shadow.h"
#include "Lighting/ShadowRenderer.h"
#include "Rasterizer/Rasterizer.h"
#include "Rasterizer/Shading.h"
#include "Display/FrameBuffer.h"
#include "Display/Drivers/DisplayDriverBase.h"
#include "Display/Drivers/ST7789Driver.h"
#include "Display/Drivers/ILI9488Driver.h"
#include "HUD/HudRenderer.h"
#include "SceneRendering/Culling.h"
#include "SceneRendering/MeshRenderer.h"
#include "SceneRendering/CameraController.h"
#include <vector>

namespace pip3D
{

    class Renderer
    {
    public:
        enum ShadingMode
        {
            SHADING_FLAT = 0
        };

    private:
        static constexpr int TILE_COLS = 4;
        static constexpr int TILE_ROWS = 4;
        static constexpr int TILE_WIDTH = 80;
        static constexpr int TILE_HEIGHT = 60;

        // Banded rendering: horizontal bands for the physical 320x240 screen.
        static constexpr int BAND_COUNT = SCREEN_BAND_COUNT;
        static constexpr int BAND_HEIGHT = SCREEN_BAND_HEIGHT;

        FrameBuffer framebuffer;
        ZBuffer<SCREEN_WIDTH, SCREEN_BAND_HEIGHT> *zBuffer;
        DisplayDriverBase *display;

        // Full screen configuration (320x240) separate from banded framebuffer config
        DisplayConfig screenConfig;

        std::vector<Camera> cameras;
        int activeCameraIndex;

        Matrix4x4 viewMatrix;
        Matrix4x4 projMatrix;
        Matrix4x4 viewProjMatrix;
        bool viewProjMatrixDirty;

        Viewport viewport;
        Frustum frustum;

        std::vector<Light> lights;
        int activeLightCount;

        bool shadowsEnabled;
        bool backfaceCullingEnabled;
        bool occlusionCullingEnabled;

        ShadowSettings shadowSettings;

        PerformanceCounter perfCounter;

        ShadingMode shadingMode;

        uint32_t statsTrianglesTotal;
        uint32_t statsTrianglesBackfaceCulled;
        uint32_t statsInstancesTotal;
        uint32_t statsInstancesFrustumCulled;
        uint32_t statsInstancesOcclusionCulled;
        WorldInstanceDirtySlot worldInstanceDirty[MAX_WORLD_DIRTY_INSTANCES];

        int16_t worldDirtyMinX;
        int16_t worldDirtyMinY;
        int16_t worldDirtyMaxX;
        int16_t worldDirtyMaxY;
        int16_t lastWorldDirtyMinX;
        int16_t lastWorldDirtyMinY;
        int16_t lastWorldDirtyMaxX;
        int16_t lastWorldDirtyMaxY;
        bool hasWorldDirtyRegion;
        bool hasLastWorldDirtyRegion;

        int16_t hudDirtyMinX;
        int16_t hudDirtyMinY;
        int16_t hudDirtyMaxX;
        int16_t hudDirtyMaxY;
        bool hasHudDirtyRegion;

        bool cameraChangedThisFrame;

        bool debugShowDirtyRegions;

        // Current band index for banded rendering (0..BAND_COUNT-1)
        int currentBandIndex;

    public:
        Renderer() : zBuffer(nullptr),
                     display(nullptr),
                     cameras(1),
                     activeCameraIndex(0),
                     viewProjMatrixDirty(true),
                     lights(1),
                     activeLightCount(1),
                     shadowsEnabled(true),
                     backfaceCullingEnabled(true),
                     // Occlusion culling disabled by default in banded mode
                     occlusionCullingEnabled(false),
                     shadingMode(SHADING_FLAT),
                     statsTrianglesTotal(0),
                     statsTrianglesBackfaceCulled(0),
                     statsInstancesTotal(0),
                     statsInstancesFrustumCulled(0),
                     statsInstancesOcclusionCulled(0)
        {
            lights[0].type = LIGHT_DIRECTIONAL;
            lights[0].direction = Vector3(-0.5f, -1.0f, -0.5f);
            lights[0].direction.normalize();
            lights[0].color = Color::WHITE;
            lights[0].intensity = 1.0f;

            hasWorldDirtyRegion = false;
            hasLastWorldDirtyRegion = false;
            hasHudDirtyRegion = false;
            cameraChangedThisFrame = false;
            debugShowDirtyRegions = false;
            worldDirtyMinX = 0;
            worldDirtyMinY = 0;
            worldDirtyMaxX = 0;
            worldDirtyMaxY = 0;
            lastWorldDirtyMinX = 0;
            lastWorldDirtyMinY = 0;
            lastWorldDirtyMaxX = 0;
            lastWorldDirtyMaxY = 0;
            hudDirtyMinX = 0;
            hudDirtyMinY = 0;
            hudDirtyMaxX = 0;
            hudDirtyMaxY = 0;

            for (int i = 0; i < MAX_WORLD_DIRTY_INSTANCES; ++i)
            {
                worldInstanceDirty[i].instance = nullptr;
                worldInstanceDirty[i].hasCurrent = false;
                worldInstanceDirty[i].hasLast = false;
            }
        }

        bool init(const DisplayConfig &cfg)
        {
            Shading::initLUT();
            useDualCore(true);

            LOGI(::pip3D::Debug::LOG_MODULE_RENDER,
                 "Renderer::init: display %dx%d @ %dMHz (cs=%d, dc=%d, rst=%d, bl=%d)",
                 cfg.width,
                 cfg.height,
                 (int)(cfg.spi_freq / 1000000),
                 cfg.cs,
                 cfg.dc,
                 cfg.rst,
                 cfg.bl);

            if (!display)
            {
                display = new ILI9488Driver();
                if (!display)
                {
                    LOGE(::pip3D::Debug::LOG_MODULE_RENDER,
                         "Renderer::init: failed to allocate ILI9488Driver");
                    return false;
                }
            }

            LCD displayCfg;
            displayCfg.w = cfg.width;
            displayCfg.h = cfg.height;
            displayCfg.cs = cfg.cs;
            displayCfg.dc = cfg.dc;
            displayCfg.rst = cfg.rst;
            displayCfg.bl = cfg.bl;
            displayCfg.freq = cfg.spi_freq;

            if (!display || !display->init(displayCfg))
            {
                if (!display)
                {
                    LOGE(::pip3D::Debug::LOG_MODULE_RENDER,
                         "Renderer::init: display pointer is null before init");
                }
                else
                {
                    LOGE(::pip3D::Debug::LOG_MODULE_RENDER,
                         "Renderer::init: display->init failed");
                }
                if (display)
                {
                    delete display;
                    display = nullptr;
                }
                return false;
            }

            // Store full-screen configuration (e.g., 320x240)
            screenConfig = cfg;

            // Framebuffer only keeps a single band in memory.
            DisplayConfig fbCfg = cfg;
            fbCfg.height = cfg.height / BAND_COUNT;

            if (!framebuffer.init(fbCfg, display))
            {
                LOGE(::pip3D::Debug::LOG_MODULE_RENDER,
                     "Renderer::init: FrameBuffer::init failed for %dx%d",
                     cfg.width,
                     cfg.height);
                delete display;
                display = nullptr;
                return false;
            }

            // Z-buffer also allocated per-band (same dimensions as framebuffer)
            zBuffer = new ZBuffer<SCREEN_WIDTH, SCREEN_BAND_HEIGHT>();
            if (!zBuffer || !zBuffer->init())
            {
                if (!zBuffer)
                {
                    LOGE(::pip3D::Debug::LOG_MODULE_RENDER,
                         "Renderer::init: failed to allocate ZBuffer");
                }
                else
                {
                    LOGE(::pip3D::Debug::LOG_MODULE_RENDER,
                         "Renderer::init: ZBuffer::init failed");
                }
                if (zBuffer)
                {
                    delete zBuffer;
                    zBuffer = nullptr;
                }
                delete display;
                display = nullptr;
                return false;
            }

            // Viewport still covers the full screen; projection stays unchanged.
            viewport = Viewport(0, 0, cfg.width, cfg.height);

            LOGI(::pip3D::Debug::LOG_MODULE_RENDER,
                 "Renderer::init OK: viewport %dx%d",
                 cfg.width,
                 cfg.height);
            return true;
        }

        void beginFrame()
        {
            // Legacy single-band entry point: render only the first band.
            beginFrameBand(0);
        }

        void endFrame()
        {
        #if ENABLE_DEBUG_DRAW
            ::pip3D::Debug::DebugDraw::render(*this);
        #endif

            // Legacy single-band exit: flush only current band to the top of the screen.
            framebuffer.endFrameRegion(0, 0,
                                       framebuffer.getConfig().width,
                                       framebuffer.getConfig().height);
            perfCounter.endFrame();
        }

        void endFrameRegion(int16_t x, int16_t y, int16_t w, int16_t h)
        {
            framebuffer.endFrameRegion(x, y, w, h);
            perfCounter.endFrame();
        }

        // Banded rendering API: render a specific horizontal band (0..BAND_COUNT-1).
        void beginFrameBand(int bandIndex)
        {
            if (bandIndex < 0)
                bandIndex = 0;
            if (bandIndex >= BAND_COUNT)
                bandIndex = BAND_COUNT - 1;

            currentBandIndex = bandIndex;

            // Update global band state (used by rasterizer, mesh renderer, shadows, etc.).
            int16_t bandTop = static_cast<int16_t>(bandIndex * BAND_HEIGHT);
            currentBandOffsetY() = bandTop;
            currentBandHeight() = BAND_HEIGHT;

            // Only once per full frame, on the first band
            if (bandIndex == 0)
            {
                perfCounter.begin();

                for (int i = 0; i < MAX_WORLD_DIRTY_INSTANCES; ++i)
                {
                    worldInstanceDirty[i].hasCurrent = false;
                    worldInstanceDirty[i].hasLast = false;
                    worldInstanceDirty[i].instance = nullptr;
                }

                hasWorldDirtyRegion = false;
                hasLastWorldDirtyRegion = false;
                hasHudDirtyRegion = false;
                cameraChangedThisFrame = false;

                CameraController::updateViewProjectionIfNeeded(cameras[activeCameraIndex],
                                                               viewport,
                                                               viewMatrix,
                                                               projMatrix,
                                                               viewProjMatrix,
                                                               frustum,
                                                               viewProjMatrixDirty,
                                                               cameraChangedThisFrame);

                statsTrianglesTotal = 0;
                statsTrianglesBackfaceCulled = 0;
                statsInstancesTotal = 0;
                statsInstancesFrustumCulled = 0;
                statsInstancesOcclusionCulled = 0;
            }

            framebuffer.beginFrame();
            if (zBuffer)
                zBuffer->clear();

        #if ENABLE_DEBUG_DRAW
            ::pip3D::Debug::DebugDraw::beginFrame();
        #endif
        }

        void endFrameBand(int bandIndex)
        {
            if (bandIndex < 0)
                bandIndex = 0;
            if (bandIndex >= BAND_COUNT)
                bandIndex = BAND_COUNT - 1;

            const DisplayConfig &fbCfg = framebuffer.getConfig();
            int16_t bandY = static_cast<int16_t>(bandIndex * fbCfg.height);

            framebuffer.endFrameRegion(0, bandY, fbCfg.width, fbCfg.height);

            // Finish performance counter after the last band is flushed
            if (bandIndex == BAND_COUNT - 1)
            {
                perfCounter.endFrame();
            }
        }

        // Explicit skybox/background pass: call this after opaque world
        // geometry has been rendered and ZBuffer filled, but before
        // transparent overlays (water, HUD) so they remain on top.
        void drawSkyboxBackground()
        {
            framebuffer.drawSkyboxWhereEmpty(*zBuffer);
        }

        Vector3 project(const Vector3 &v)
        {
            return CameraController::project(v, viewProjMatrix, viewport);
        }

        void drawSunSprite(const Vector3 &worldPos, const Color &color, float glow)
        {
            Vector3 p = project(worldPos);
            if (cameras[activeCameraIndex].projectionType == PERSPECTIVE && p.z <= 0.0f)
            {
                return;
            }

            auto cfg = framebuffer.getConfig();
            uint16_t *fb = framebuffer.getBuffer();
            if (!fb)
            {
                return;
            }

            int16_t minDim = cfg.width < cfg.height ? cfg.width : cfg.height;
            if (minDim <= 0)
            {
                return;
            }

            float baseRadius = minDim * 0.04f;
            if (baseRadius < 1.0f)
            {
                baseRadius = 1.0f;
            }

            float extra = glow;
            if (extra < 0.0f)
            {
                extra = 0.0f;
            }
            if (extra > 1.0f)
            {
                extra = 1.0f;
            }

            int16_t radius = (int16_t)(baseRadius * (1.0f + extra));
            if (radius <= 0)
            {
                return;
            }

            int16_t cx = (int16_t)p.x;
            int16_t cy = (int16_t)p.y;
            int r2 = radius * radius;

            for (int dy = -radius; dy <= radius; ++dy)
            {
                int yy = cy + dy;
                if (yy < 0 || yy >= cfg.height)
                {
                    continue;
                }

                for (int dx = -radius; dx <= radius; ++dx)
                {
                    int xx = cx + dx;
                    if (xx < 0 || xx >= cfg.width)
                    {
                        continue;
                    }

                    int d2 = dx * dx + dy * dy;
                    if (d2 <= r2)
                    {
                        fb[yy * cfg.width + xx] = color.rgb565;
                    }
                }
            }
        }

        void drawWater(float yLevel, float size, Color color, float alpha, float time)
        {
            uint16_t *fb = framebuffer.getBuffer();
            if (!fb || !zBuffer)
            {
                return;
            }

            if (alpha <= 0.0f)
            {
                return;
            }
            if (alpha > 1.0f)
            {
                alpha = 1.0f;
            }

            const uint8_t alphaByte = static_cast<uint8_t>(alpha * 255.0f);
            const DisplayConfig &cfg = framebuffer.getConfig();

            Camera &cam = cameras[activeCameraIndex];

            // Mark the full world region as dirty since water animates every frame
            addDirtyRect(nullptr, 0, 0, cfg.width, cfg.height);

            // Frustum cull: simple sphere around water patch
            const Vector3 center(0.0f, yLevel, 0.0f);
            const float radius = size * 0.75f;
            if (!frustum.sphere(center, radius))
            {
                return;
            }

            const int GRID = 32;
            const float half = size * 0.5f;
            const float step = size / static_cast<float>(GRID);

            const float freq = 0.6f;
            const float amp = size * 0.02f;

            for (int iz = 0; iz < GRID; ++iz)
            {
                float z0 = -half + step * static_cast<float>(iz);
                float z1 = z0 + step;

                for (int ix = 0; ix < GRID; ++ix)
                {
                    float x0 = -half + step * static_cast<float>(ix);
                    float x1 = x0 + step;

                    Vector3 v00(x0,
                                yLevel + FastMath::fastSin(x0 * freq + time) * amp +
                                    FastMath::fastCos(z0 * freq + time) * amp,
                                z0);
                    Vector3 v10(x1,
                                yLevel + FastMath::fastSin(x1 * freq + time) * amp +
                                    FastMath::fastCos(z0 * freq + time) * amp,
                                z0);
                    Vector3 v01(x0,
                                yLevel + FastMath::fastSin(x0 * freq + time) * amp +
                                    FastMath::fastCos(z1 * freq + time) * amp,
                                z1);
                    Vector3 v11(x1,
                                yLevel + FastMath::fastSin(x1 * freq + time) * amp +
                                    FastMath::fastCos(z1 * freq + time) * amp,
                                z1);

                    // Triangle 1
                    drawWaterTriangleInternal(v00, v10, v11, color, alphaByte, cam, cfg, fb);
                    // Triangle 2
                    drawWaterTriangleInternal(v00, v11, v01, color, alphaByte, cam, cfg, fb);
                }
            }
        }

        void drawTriangle3D(const Vector3 &v0, const Vector3 &v1, const Vector3 &v2, uint16_t color)
        {
            MeshRenderer::drawTriangle3D(v0, v1, v2, color,
                                         cameras[activeCameraIndex],
                                         viewport,
                                         viewProjMatrix,
                                         framebuffer,
                                         zBuffer,
                                         lights.data(),
                                         activeLightCount,
                                         backfaceCullingEnabled,
                                         statsTrianglesTotal,
                                         statsTrianglesBackfaceCulled);
        }

        Camera &getCamera() { return cameras[activeCameraIndex]; }
        Camera &getCamera(int index)
        {
            if (index >= 0 && index < (int)cameras.size())
                return cameras[index];
            return cameras[activeCameraIndex];
        }
        const Viewport &getViewport() const { return viewport; }
        float getFPS() const { return perfCounter.getFPS(); }
        float getAverageFPS() const { return perfCounter.getAverageFPS(); }
        uint32_t getFrameTime() const { return perfCounter.getFrameTime(); }
        int getActiveCameraIndex() const { return activeCameraIndex; }
        int getCameraCount() const { return cameras.size(); }
        uint16_t *getFrameBuffer() const { return const_cast<uint16_t *>(framebuffer.getBuffer()); }
        const Frustum &getFrustum() const { return frustum; }

        uint32_t getStatsTrianglesTotal() const { return statsTrianglesTotal; }
        uint32_t getStatsTrianglesBackfaceCulled() const { return statsTrianglesBackfaceCulled; }
        uint32_t getStatsInstancesTotal() const { return statsInstancesTotal; }
        uint32_t getStatsInstancesFrustumCulled() const { return statsInstancesFrustumCulled; }
        uint32_t getStatsInstancesOcclusionCulled() const { return statsInstancesOcclusionCulled; }

        int createCamera()
        {
            cameras.push_back(Camera());
            return cameras.size() - 1;
        }

        void setActiveCamera(int index)
        {
            if (index >= 0 && index < (int)cameras.size())
            {
                activeCameraIndex = index;
                viewProjMatrixDirty = true;
            }
        }

        void setLight(int index, const Light &light)
        {
            LightManager::setLight(lights, activeLightCount, index, light);
        }

        int addLight(const Light &light)
        {
            return LightManager::addLight(lights, activeLightCount, light);
        }

        void removeLight(int index)
        {
            LightManager::removeLight(lights, activeLightCount, index);
        }

        Light *getLight(int index)
        {
            return LightManager::getLight(lights, activeLightCount, index);
        }

        void clearLights() { LightManager::clearLights(activeLightCount); }
        int getLightCount() const { return LightManager::getLightCount(activeLightCount); }

        void setMainDirectionalLight(const Vector3 &direction, const Color &color, float intensity = 1.0f)
        {
            LightManager::setMainDirectionalLight(lights, activeLightCount, direction, color, intensity);
        }

        void setMainPointLight(const Vector3 &position, const Color &color, float intensity = 1.0f, float range = 10.0f)
        {
            LightManager::setMainPointLight(lights, activeLightCount, position, color, intensity, range);
        }

        void setLightColor(const Color &color)
        {
            LightManager::setLightColor(lights, activeLightCount, color);
        }

        void setLightPosition(const Vector3 &pos)
        {
            LightManager::setLightPosition(lights, activeLightCount, pos);
        }
        void setLightDirection(const Vector3 &dir)
        {
            LightManager::setLightDirection(lights, activeLightCount, dir);
        }

        void setLightTemperature(float kelvin)
        {
            LightManager::setLightTemperature(lights, activeLightCount, kelvin);
        }

        Color getLightColor() const { return LightManager::getLightColor(lights, activeLightCount); }

        void setShadowsEnabled(bool enabled) { shadowsEnabled = enabled; }
        bool getShadowsEnabled() const { return shadowsEnabled; }
        void setBackfaceCullingEnabled(bool enabled) { backfaceCullingEnabled = enabled; }
        bool getBackfaceCullingEnabled() const { return backfaceCullingEnabled; }

        void setOcclusionCullingEnabled(bool enabled) { occlusionCullingEnabled = enabled; }
        bool getOcclusionCullingEnabled() const { return occlusionCullingEnabled; }

        void setDebugShowDirtyRegions(bool enabled) { debugShowDirtyRegions = enabled; }
        bool getDebugShowDirtyRegions() const { return debugShowDirtyRegions; }

        void setShadingMode(ShadingMode mode)
        {
            shadingMode = mode;
        }

        ShadingMode getShadingMode() const { return shadingMode; }

        void setLightType(LightType type)
        {
            LightManager::setLightType(lights, activeLightCount, type);
        }

        void setSkyboxEnabled(bool enabled) { framebuffer.setSkyboxEnabled(enabled); }
        void setSkyboxType(SkyboxType type) { framebuffer.setSkyboxType(type); }
        void setSkyboxWithLighting(SkyboxType type)
        {
            framebuffer.setSkyboxType(type);
            float temp = framebuffer.getSkybox().getRecommendedLightTemperature();
            setLightTemperature(temp);
        }
        void setClearColor(Color color) { framebuffer.setClearColor(color); }
        Skybox &getSkybox() { return framebuffer.getSkybox(); }
        bool isSkyboxEnabled() const { return framebuffer.isSkyboxEnabled(); }

        void drawText(int16_t x, int16_t y, const char *text, uint16_t color = 0xFFFF)
        {
            HudRenderer::drawText(framebuffer, x, y, text, color);

            int16_t w = HudRenderer::getTextWidth(text);
            addHudDirtyRect(x, y, w, 8);
        }

        void drawText(int16_t x, int16_t y, const char *text, Color color)
        {
            drawText(x, y, text, color.rgb565);
        }

        void drawTextAdaptive(int16_t x, int16_t y, const char *text)
        {
            uint16_t color = getAdaptiveTextColor(x, y);
            drawText(x, y, text, color);
        }

        uint16_t getAdaptiveTextColor(int16_t x, int16_t y, int16_t width = 40, int16_t height = 8)
        {
            return HudRenderer::getAdaptiveTextColor(framebuffer, viewport, x, y, width, height);
        }

        int16_t getTextWidth(const char *text)
        {
            return HudRenderer::getTextWidth(text);
        }

        void drawMesh(Mesh *mesh)
        {
            MeshRenderer::drawMesh(mesh,
                                   cameras[activeCameraIndex],
                                   viewport,
                                   frustum,
                                   viewProjMatrix,
                                   framebuffer,
                                   zBuffer,
                                   lights.data(),
                                   activeLightCount,
                                   backfaceCullingEnabled,
                                   statsTrianglesTotal,
                                   statsTrianglesBackfaceCulled);
        }

        void drawMesh(Mesh *mesh, ShadingMode mode)
        {
            ShadingMode prev = shadingMode;
            shadingMode = mode;
            drawMesh(mesh);
            shadingMode = prev;
        }

    public:
        void drawMeshInstanceInternal(MeshInstance *instance, bool performFrustumCull, bool trackDirty)
        {
            if (!instance || !instance->isVisible())
                return;

            Mesh *mesh = instance->getMesh();
            if (!mesh)
                return;

            Vector3 center = instance->center();
            float radius = instance->radius();

            if (performFrustumCull)
            {
                if (!frustum.sphere(center, radius))
                {
                    statsInstancesFrustumCulled++;
                    return;
                }
            }

            // Contribution culling: skip instances that project to < 1 pixel
            // on screen for perspective cameras.
            const Camera &cam = cameras[activeCameraIndex];
            if (cam.projectionType == PERSPECTIVE)
            {
                Vector3 toCenter = center - cam.position;
                float distForward = toCenter.dot(cam.forward());
                if (distForward > cam.nearPlane)
                {
                    float fovRad = cam.fov * DEG2RAD;
                    float tanHalf = tanf(fovRad * 0.5f);
                    if (tanHalf > 1e-6f)
                    {
                        float projScale = 1.0f / tanHalf;
                        float radiusPixels = fabsf(radius * projScale / distForward) *
                                             (static_cast<float>(viewport.height) * 0.5f);
                        if (radiusPixels < 1.0f)
                        {
                            statsInstancesTotal++;
                            return;
                        }
                    }
                }
            }

            statsInstancesTotal++;

            if (occlusionCullingEnabled &&
                Culling::isInstanceOccluded(center, radius,
                                            cameras[activeCameraIndex],
                                            viewport,
                                            viewProjMatrix,
                                            zBuffer,
                                            framebuffer.getConfig()))
            {
                statsInstancesOcclusionCulled++;
                return;
            }

            if (trackDirty)
            {
                addDirtyFromSphere(instance, center, radius);
            }

            const Matrix4x4 &worldTransform = instance->transform();
            const uint16_t instColor565 = instance->color().rgb565;

            for (uint16_t i = 0; i < mesh->numFaces(); i++)
            {
                const Face &face = mesh->face(i);
                const Vertex &vert0 = mesh->vert(face.v0);
                const Vertex &vert1 = mesh->vert(face.v1);
                const Vertex &vert2 = mesh->vert(face.v2);

                Vector3 local0 = mesh->decodePosition(vert0);
                Vector3 local1 = mesh->decodePosition(vert1);
                Vector3 local2 = mesh->decodePosition(vert2);

                Vector3 v0 = worldTransform.transformNoDiv(local0);
                Vector3 v1 = worldTransform.transformNoDiv(local1);
                Vector3 v2 = worldTransform.transformNoDiv(local2);

                drawTriangle3D(v0, v1, v2, instColor565);
            }
        }

        void drawMeshInstance(MeshInstance *instance)
        {
            drawMeshInstanceInternal(instance, true, true);
        }

        void drawMeshInstance(MeshInstance *instance, ShadingMode mode)
        {
            ShadingMode prev = shadingMode;
            shadingMode = mode;
            drawMeshInstance(instance);
            shadingMode = prev;
        }

        void drawMeshInstanceStatic(MeshInstance *instance)
        {
            drawMeshInstanceInternal(instance, true, false);
        }

        void drawInstances(InstanceManager &manager)
        {
            static std::vector<MeshInstance *> visibleInstances;
            manager.cull(frustum, visibleInstances);

            manager.sort(cameras[activeCameraIndex].position, visibleInstances);

            for (auto *instance : visibleInstances)
            {
                drawMeshInstanceInternal(instance, false, true);
            }
        }

        void drawMeshShadow(Mesh *mesh)
        {
            ShadowRenderer::drawMeshShadow(mesh,
                                           shadowsEnabled,
                                           shadowSettings,
                                           cameras[activeCameraIndex],
                                           lights.data(),
                                           activeLightCount,
                                           viewProjMatrix,
                                           viewport,
                                           framebuffer,
                                           zBuffer,
                                           backfaceCullingEnabled);
        }
        void drawMeshInstanceShadow(MeshInstance *instance)
        {
            ShadowRenderer::drawMeshInstanceShadow(instance,
                                                   shadowsEnabled,
                                                   shadowSettings,
                                                   cameras[activeCameraIndex],
                                                   lights.data(),
                                                   activeLightCount,
                                                   viewProjMatrix,
                                                   viewport,
                                                   framebuffer,
                                                   zBuffer,
                                                   backfaceCullingEnabled);
        }
        void setShadowOpacity(float opacity)
        {
            shadowSettings.shadowOpacity = clamp(opacity, 0.0f, 1.0f);
        }

        void setShadowColor(const Color &color)
        {
            shadowSettings.shadowColor = color;
        }

        void setShadowPlane(const Vector3 &normal, float distance)
        {
            shadowSettings.plane = ShadowProjector::ShadowPlane(normal, distance);
        }

        void setShadowPlaneY(float y)
        {
            shadowSettings.plane = ShadowProjector::ShadowPlane(Vector3(0, 1, 0), -y);
        }

        ShadowSettings &getShadowSettings() { return shadowSettings; }

    private:
        __attribute__((always_inline)) inline void addDirtyRect(MeshInstance *instance, int16_t x, int16_t y, int16_t w, int16_t h)
        {
            DirtyRegionHelper::addDirtyRect(instance, x, y, w, h,
                                            viewport,
                                            worldInstanceDirty,
                                            worldDirtyMinX,
                                            worldDirtyMinY,
                                            worldDirtyMaxX,
                                            worldDirtyMaxY,
                                            hasWorldDirtyRegion);
        }

        void addHudDirtyRect(int16_t x, int16_t y, int16_t w, int16_t h)
        {
            DirtyRegionHelper::addHudDirtyRect(x, y, w, h,
                                               viewport,
                                               hudDirtyMinX,
                                               hudDirtyMinY,
                                               hudDirtyMaxX,
                                               hudDirtyMaxY,
                                               hasHudDirtyRegion);
        }

        void addDirtyFromSphere(MeshInstance *instance, const Vector3 &c, float r)
        {
            if (r <= 0.0f)
                return;

            Vector3 pc = project(c);
            Vector3 px = project(Vector3(c.x + r, c.y, c.z));
            Vector3 py = project(Vector3(c.x, c.y + r, c.z));
            Vector3 pz = project(Vector3(c.x, c.y, c.z + r));

            float dx = fabsf(px.x - pc.x);
            float dy = fabsf(px.y - pc.y);

            float t = fabsf(py.x - pc.x);
            if (t > dx)
                dx = t;
            t = fabsf(pz.x - pc.x);
            if (t > dx)
                dx = t;

            t = fabsf(py.y - pc.y);
            if (t > dy)
                dy = t;
            t = fabsf(pz.y - pc.y);
            if (t > dy)
                dy = t;

            float rScr = dx > dy ? dx : dy;

            int16_t x0 = (int16_t)(pc.x - rScr);
            int16_t y0 = (int16_t)(pc.y - rScr);
            int16_t x1 = (int16_t)(pc.x + rScr + 1.0f);
            int16_t y1 = (int16_t)(pc.y + rScr + 1.0f);

            addDirtyRect(instance, x0, y0, x1 - x0, y1 - y0);
        }

        __attribute__((always_inline)) inline void drawWaterTriangleInternal(const Vector3 &v0,
                                                                             const Vector3 &v1,
                                                                             const Vector3 &v2,
                                                                             const Color &waterColor,
                                                                             uint8_t alphaByte,
                                                                             const Camera &cam,
                                                                             const DisplayConfig &cfg,
                                                                             uint16_t *frameBufferPtr)
        {
            Vector3 p0 = CameraController::project(v0, viewProjMatrix, viewport);
            Vector3 p1 = CameraController::project(v1, viewProjMatrix, viewport);
            Vector3 p2 = CameraController::project(v2, viewProjMatrix, viewport);

            float x0 = p0.x, y0 = p0.y, z0 = p0.z;
            float x1 = p1.x, y1 = p1.y, z1 = p1.z;
            float x2 = p2.x, y2 = p2.y, z2 = p2.z;

            float minXf = fminf(x0, fminf(x1, x2));
            float maxXf = fmaxf(x0, fmaxf(x1, x2));
            float minYf = fminf(y0, fminf(y1, y2));
            float maxYf = fmaxf(y0, fmaxf(y1, y2));

            int16_t minX = static_cast<int16_t>(floorf(minXf));
            int16_t maxX = static_cast<int16_t>(ceilf(maxXf));
            int16_t minY = static_cast<int16_t>(floorf(minYf));
            int16_t maxY = static_cast<int16_t>(ceilf(maxYf));

            // Clip to full screen bounds first.
            if (maxX < 0 || maxY < 0 || minX >= (int16_t)SCREEN_WIDTH || minY >= (int16_t)SCREEN_HEIGHT)
            {
                return;
            }

            if (minX < 0)
                minX = 0;
            if (minY < 0)
                minY = 0;
            if (maxX >= (int16_t)SCREEN_WIDTH)
                maxX = (int16_t)SCREEN_WIDTH - 1;
            if (maxY >= (int16_t)SCREEN_HEIGHT)
                maxY = (int16_t)SCREEN_HEIGHT - 1;

            // Then clip to current band vertically.
            int16_t bandTop = currentBandOffsetY();
            int16_t bandH = currentBandHeight();
            int16_t bandBottom = static_cast<int16_t>(bandTop + bandH);

            if (maxY < bandTop || minY >= bandBottom)
            {
                return;
            }

            if (minY < bandTop)
                minY = bandTop;
            if (maxY >= bandBottom)
                maxY = static_cast<int16_t>(bandBottom - 1);

            float denom = (y1 - y2) * (x0 - x2) + (x2 - x1) * (y0 - y2);
            if (fabsf(denom) < 1e-6f)
            {
                return;
            }
            float invDenom = 1.0f / denom;

            for (int16_t y = minY; y <= maxY; ++y)
            {
                float py = static_cast<float>(y) + 0.5f;
                int16_t yLocal = static_cast<int16_t>(y - bandTop);

                for (int16_t x = minX; x <= maxX; ++x)
                {
                    float px = static_cast<float>(x) + 0.5f;

                    float w0 = ((y1 - y2) * (px - x2) + (x2 - x1) * (py - y2)) * invDenom;
                    float w1 = ((y2 - y0) * (px - x2) + (x0 - x2) * (py - y2)) * invDenom;
                    float w2 = 1.0f - w0 - w1;

                    if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f)
                        continue;

                    uint16_t &dst = frameBufferPtr[yLocal * cfg.width + x];
                    Color bg(dst);
                    dst = bg.blend(waterColor, alphaByte).rgb565;
                }
            }
        }

        ~Renderer()
        {
            if (zBuffer)
                delete zBuffer;
            if (display)
                delete display;
        }
    };

}

#endif
