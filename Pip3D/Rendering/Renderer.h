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

        FrameBuffer framebuffer;
        ZBuffer<320, 240> *zBuffer;
        ST7789Driver *display;

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
                     occlusionCullingEnabled(true),
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
                display = new ST7789Driver();
                if (!display)
                {
                    LOGE(::pip3D::Debug::LOG_MODULE_RENDER,
                         "Renderer::init: failed to allocate ST7789Driver");
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

            if (!framebuffer.init(cfg, display))
            {
                LOGE(::pip3D::Debug::LOG_MODULE_RENDER,
                     "Renderer::init: FrameBuffer::init failed for %dx%d",
                     cfg.width,
                     cfg.height);
                delete display;
                display = nullptr;
                return false;
            }

            zBuffer = new ZBuffer<320, 240>();
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

            viewport = Viewport(0, 0, cfg.width, cfg.height);

            LOGI(::pip3D::Debug::LOG_MODULE_RENDER,
                 "Renderer::init OK: viewport %dx%d",
                 cfg.width,
                 cfg.height);
            return true;
        }

        void beginFrame()
        {
            perfCounter.begin();
            framebuffer.beginFrame();
            zBuffer->clear();

#if ENABLE_DEBUG_DRAW
            ::pip3D::Debug::DebugDraw::beginFrame();
#endif

            for (int i = 0; i < MAX_WORLD_DIRTY_INSTANCES; ++i)
            {
                worldInstanceDirty[i].hasCurrent = false;
            }

            hasWorldDirtyRegion = false;
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

        void endFrame()
        {
            bool forceFullFrame = false;
            bool showDirtyOverlay = debugShowDirtyRegions;

#if ENABLE_DEBUG_DRAW
            ::pip3D::Debug::DebugDraw::render(*this);
#endif

            DirtyRegionHelper::finalizeFrame(framebuffer,
                                             perfCounter,
                                             worldInstanceDirty,
                                             worldDirtyMinX,
                                             worldDirtyMinY,
                                             worldDirtyMaxX,
                                             worldDirtyMaxY,
                                             lastWorldDirtyMinX,
                                             lastWorldDirtyMinY,
                                             lastWorldDirtyMaxX,
                                             lastWorldDirtyMaxY,
                                             hasWorldDirtyRegion,
                                             hasLastWorldDirtyRegion,
                                             hudDirtyMinX,
                                             hudDirtyMinY,
                                             hudDirtyMaxX,
                                             hudDirtyMaxY,
                                             hasHudDirtyRegion,
                                             cameraChangedThisFrame,
                                             statsTrianglesTotal,
                                             statsTrianglesBackfaceCulled,
                                             statsInstancesTotal,
                                             statsInstancesFrustumCulled,
                                             statsInstancesOcclusionCulled,
                                             forceFullFrame,
                                             showDirtyOverlay);
        }

        void endFrameRegion(int16_t x, int16_t y, int16_t w, int16_t h)
        {
            framebuffer.endFrameRegion(x, y, w, h);
            perfCounter.endFrame();
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
