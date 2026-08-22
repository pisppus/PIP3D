#pragma once

#include <vector>

#include "Core/Platform.hpp"
#include "Core/Color.hpp"
#include "Core/Diagnostics.hpp"
#include "Core/Jobs.hpp"
#include "Debug/Gizmos.hpp"
#include "Math/Algebra.hpp"
#include "Camera/Camera.hpp"
#include "Camera/Frustum.hpp"
#include "Geometry/Instance.hpp"
#include "Geometry/Mesh.hpp"
#include "Rendering/Buffers/ZBuffer.hpp"
#include "Rendering/Environment/Sky.hpp"
#include "Rendering/Resources/Texture.hpp"
#include "Rendering/Lighting/Lighting.hpp"
#include "Rendering/Lighting/Shadow.hpp"
#include "Rendering/Lighting/Deferred.hpp"
#include "Rendering/Lighting/Fog.hpp"
#include "Rendering/Pipeline/Shading.hpp"
#include "Rendering/Pipeline/Culling.hpp"
#include "Rendering/Pipeline/MeshDraw.hpp"
#include "Rendering/Pipeline/Billboard.hpp"
#include "Rendering/Pipeline/Telemetry.hpp"
#include "Rendering/Effects/Glow.hpp"
#include "Rendering/UI/Font.hpp"
#include "Rendering/UI/HUD.hpp"

#include <PipCore/Display.hpp>
#if defined(PIP3D_PC)
#include <PipCore/Platforms/Desktop/Runtime.hpp>
#else
#include <PipCore/Platforms/Select.hpp>
#endif

namespace pip3D
{
    class PhysicsWorld;

    struct BandCullItem
    {
        MeshInstance *inst;
        int16_t minY;
        int16_t maxY;
        float zEye;
        float radiusPixels;
    };

    struct FlushJob
    {
        pipcore::Display *display;
        const uint16_t *pixels;
        int16_t x;
        int16_t y;
        int16_t w;
        int16_t h;
        int32_t stridePixels;
        SemaphoreHandle_t doneSem;
    };

    class Renderer
    {
    private:
        static constexpr int BAND_COUNT = SCREEN_BAND_COUNT;
        static constexpr int BAND_HEIGHT = SCREEN_BAND_HEIGHT;

        FrameBuffer framebuffer;
        alignas(32) ZBuffer zBuffer;
        uint16_t *reflectBuffer;
        uint16_t *reflectWriteBuffer;
        static constexpr uint16_t REFLECT_WIDTH = SCREEN_WIDTH / 4;
        static constexpr uint16_t REFLECT_HEIGHT = SCREEN_HEIGHT / 4;

        std::vector<MeshInstance *> shadowQueue_;
        std::vector<MeshInstance *> opaqueQueue_;
        std::vector<MeshInstance *> blobShadowQueue_;
        std::vector<MeshInstance *> emissiveQueue_;

        std::vector<Vector3> vertexColors_;

        PhysicsWorld *physicsWorld = nullptr;
#if defined(PIP3D_PC)
        bool pcDisplayReady;
#else
        pipcore::Display *display;
#endif

        std::vector<Camera> cameras;
        int activeCameraIndex;

        Matrix4x4 viewMatrix;
        Matrix4x4 projMatrix;
        Matrix4x4 viewProjMatrix;
        bool viewProjMatrixDirty;

        float cachedHfovRad_ = 0.0f;
        bool hfovCacheValid_ = false;

        Viewport viewport;
        Frustum frustum;

        std::vector<Light> lights;
        int activeLightCount;

        std::vector<Light> pointLights;
        bool deferredLightingEnabled;

        bool shadowsEnabled;
        bool backfaceCullingEnabled;
        bool occlusionCullingEnabled;

        bool opaqueSortEnabled_ = false;

        ShadowSettings shadowSettings;
        uint32_t shadowCacheGeneration;
        Color lastAutoShadowColor;
        PerfCounter perfCounter;
        ShadingMode shadingMode;

        uint32_t statsTrianglesTotal;
        uint32_t statsTrianglesBackfaceCulled;
        uint32_t statsInstancesTotal;
        uint32_t statsInstancesFrustumCulled;
        uint32_t statsInstancesOcclusionCulled;

        bool initialized;

        bool fogEnabled;
        Color fogColor;
        float fogNear;
        float fogFar;

        float ambientScale = 1.0f;
        float exposureScale = 1.0f;

        bool sunEnabled = true;
        bool sunVisible = false;
        Color sunColor = Color::WHITE;
        float sunIntensity = 1.0f;
        Vector3 sunWorldDir = Vector3(0.0f, 1.0f, 0.0f);

        bool shouldRenderShadowForBounds(const Vector3 &center, float radius) const;
        void drawWaterTriangleInternal(const Vector3 &v0, const Vector3 &v1, const Vector3 &v2,
                                       const Color &waterColor, uint8_t alphaByte, const DisplayConfig &cfg,
                                       uint16_t *frameBufferPtr);

        void drawMeshInstanceInternal(MeshInstance *instance, bool performFrustumCull);
        void drawMeshInstanceBanded(MeshInstance *instance, float zEye, float radiusPixels);
        void drawMeshInstanceShadow(MeshInstance *instance);
        void prepareFrameState(bool incrementFrameStamp);

        __attribute__((always_inline)) inline float ensureHfovCached()
        {
            if (likely(hfovCacheValid_))
                return cachedHfovRad_;

            const Camera &cam = cameras[activeCameraIndex];
            const float aspect = static_cast<float>(viewport.width) /
                                 static_cast<float>(viewport.height);
            cachedHfovRad_ = 2.0f * atanf(
                                        tanf(cam.fov * 0.5f * kDegToRad) * aspect);
            hfovCacheValid_ = true;
            return cachedHfovRad_;
        }

        static void flushJobFunc(void *userData);
        static constexpr int FLUSH_JOB_SLOTS = 2;
        FlushJob flushJobs[FLUSH_JOB_SLOTS];
        int flushJobNext = 0;
#if !defined(PIP3D_PC)
        SemaphoreHandle_t flushSlotSem[FLUSH_JOB_SLOTS] = {nullptr, nullptr};
#endif

    public:
        Renderer();
        ~Renderer();

        bool init(const DisplayConfig &cfg);
        bool ensureReflectBuffers();

        void beginFrame();
        void endFrame();
        void beginFrameBand(int bandIndex);
        void endFrameBand(int bandIndex);

        void drawSkyboxBackground();

        void fillSkyGradient();
        void drawCloudsAfterGeometry();

        Vector3 project(const Vector3 &v);

        void updateCameraView();

        size_t buildBandCullList(const MeshInstance *const *instances, size_t count,
                                 BandCullItem *outItems);

        void buildBandCullList(const std::vector<MeshInstance *> &instances,
                               std::vector<BandCullItem> &outItems);

        void drawBandInstances(int bandIndex, const BandCullItem *items, size_t count);

        int createCamera();
        void setActiveCamera(int index);
        Camera &getCamera();

        void setLight(int index, const Light &light);
        Light *getLight(int index);
        const Light *getLights() const { return lights.data(); }
        int getActiveLightCount() const { return activeLightCount; }
        void clearLights();
        void setMainDirectionalLight(const Vector3 &direction, const Color &color, float intensity = 1.0f);
        void setLightPosition(const Vector3 &pos);
        void setLightDirection(const Vector3 &dir);
        void setLightTemperature(float kelvin);

        bool isInitialized() const
        {
            return initialized;
        }

        void setPhysicsWorld(PhysicsWorld *world) { physicsWorld = world; }
        const Viewport &getViewport() const { return viewport; }
        float getFPS() const { return perfCounter.getFPS(); }
        float getAverageFPS() const { return perfCounter.getAverageFPS(); }
        uint32_t getFrameTime() const { return perfCounter.getFrameTime(); }
        int getActiveCameraIndex() const { return activeCameraIndex; }
        int getCameraCount() const { return cameras.size(); }
        uint16_t *getFrameBuffer() const { return const_cast<uint16_t *>(framebuffer.getBuffer()); }
        ZBuffer &getZBuffer() { return zBuffer; }
        const ZBuffer &getZBuffer() const { return zBuffer; }
        const Frustum &getFrustum() const { return frustum; }
        const Matrix4x4 &getViewProjMatrix() const { return viewProjMatrix; }

        uint32_t getStatsTrianglesTotal() const { return statsTrianglesTotal; }
        uint32_t getStatsTrianglesBackfaceCulled() const { return statsTrianglesBackfaceCulled; }
        uint32_t getStatsInstancesTotal() const { return statsInstancesTotal; }
        uint32_t getStatsInstancesFrustumCulled() const { return statsInstancesFrustumCulled; }
        uint32_t getStatsInstancesOcclusionCulled() const { return statsInstancesOcclusionCulled; }

        void setShadowsEnabled(bool enabled) { shadowsEnabled = enabled; }
        bool getShadowsEnabled() const { return shadowsEnabled; }
        void setBackfaceCullingEnabled(bool enabled) { backfaceCullingEnabled = enabled; }
        bool getBackfaceCullingEnabled() const { return backfaceCullingEnabled; }
        void setOcclusionCullingEnabled(bool enabled) { occlusionCullingEnabled = enabled; }
        bool getOcclusionCullingEnabled() const { return occlusionCullingEnabled; }

        void setOpaqueSortEnabled(bool enabled) { opaqueSortEnabled_ = enabled; }
        bool isOpaqueSortEnabled() const { return opaqueSortEnabled_; }
        void setShadingMode(ShadingMode mode) { shadingMode = mode; }
        ShadingMode getShadingMode() const { return shadingMode; }

        void setLightType(LightType type)
        {
            if (activeLightCount == 0 || lights.empty())
                return;
            lights[0].type = type;
        }

        int addPointLight(const Vector3 &pos, const Color &color,
                          float range, float intensity = 1.0f);
        void setPointLight(int idx, const Vector3 &pos, const Color &color,
                           float range, float intensity);
        void setPointLightPosition(int idx, const Vector3 &pos);
        void removePointLight(int idx);
        void clearPointLights();
        int getPointLightCount() const { return static_cast<int>(pointLights.size()); }
        const Light *getPointLights() const { return pointLights.data(); }

        void setDeferredLightingEnabled(bool enabled) { deferredLightingEnabled = enabled; }
        bool isDeferredLightingEnabled() const { return deferredLightingEnabled; }
        void applyDeferredLighting();
        void syncPointLightsForward();

        void setSkyboxEnabled(bool enabled) { framebuffer.setSkyboxEnabled(enabled); }
        void setSkyboxType(SkyboxType type) { framebuffer.setSkyboxType(type); }
        void setSkybox(SkyboxType type)
        {
            framebuffer.setSkyboxType(type);
            float temp = framebuffer.getSkybox().getLightTemp();
            setLightTemperature(temp);
        }
        void setClearColor(Color color) { framebuffer.setClearColor(color); }
        Skybox &getSkybox() { return framebuffer.getSkybox(); }
        bool isSkyboxEnabled() const { return framebuffer.isSkyboxEnabled(); }
        void invalidateSkyboxCache() { framebuffer.invalidateSkyboxCache(); }

        void setCloudsEnabled(bool enabled) { framebuffer.setCloudsEnabled(enabled); }
        void setCloudColor(const Color &c) { framebuffer.setCloudColor(c); }
        void setCloudAlpha(float a) { framebuffer.setCloudAlpha(a); }
        void setCloudHeight(float meters) { framebuffer.setCloudHeight(meters); }
        void setCloudScale(float meters) { framebuffer.setCloudScale(meters); }
        void setCloudDriftAngle(float angleDeg, float speedMps) { framebuffer.setCloudDriftAngle(angleDeg, speedMps); }
        void updateClouds(float dtSeconds) { framebuffer.updateClouds(dtSeconds); }
        bool areCloudsEnabled() const { return framebuffer.areCloudsEnabled(); }
        void generateClouds(uint32_t seed = 0xC10Du, float coverage = 0.45f)
        {
            framebuffer.generateClouds(seed, coverage);
        }

        void setShadowOpacity(float opacity)
        {
            shadowSettings.shadowOpacity = clamp(opacity, 0.0f, 1.0f);
            ++shadowCacheGeneration;
        }
        void setShadowColor(const Color &color)
        {
            shadowSettings.shadowColor = color;
            shadowSettings.shadowColorAuto = false;
            ++shadowCacheGeneration;
        }
        void setShadowColorAuto()
        {
            shadowSettings.shadowColorAuto = true;
            ++shadowCacheGeneration;
        }
        void setShadowPlane(const Vector3 &normal, float distance)
        {
            shadowSettings.plane = ShadowProjector::ShadowPlane(normal, distance);
            ++shadowCacheGeneration;
        }
        void setShadowPlaneY(float y)
        {
            shadowSettings.plane = ShadowProjector::ShadowPlane(Vector3(0, 1, 0), -y);
            ++shadowCacheGeneration;
        }
        ShadowSettings &getShadowSettings() { return shadowSettings; }
        uint32_t getShadowCacheGeneration() const { return shadowCacheGeneration; }
        void invalidateShadowCache() { ++shadowCacheGeneration; }

        void setFogEnabled(bool enabled) { fogEnabled = enabled; }
        bool getFogEnabled() const { return fogEnabled; }

        void setAmbientScale(float s) { ambientScale = (s < 0.0f) ? 0.0f : (s > 2.0f) ? 2.0f
                                                                                      : s; }
        float getAmbientScale() const { return ambientScale; }
        void setExposureScale(float s) { exposureScale = (s < 0.05f) ? 0.05f : (s > 2.0f) ? 2.0f
                                                                                          : s; }
        float getExposureScale() const { return exposureScale; }

        void setMipmapsEnabled(bool enabled) { Rasterizer::g_mipmapsEnabled = enabled; }
        bool getMipmapsEnabled() const { return Rasterizer::g_mipmapsEnabled; }

        void setFogColor(const Color &color) { fogColor = color; }
        Color getFogColor() const { return fogColor; }
        void setFogLimits(float nearDist, float farDist)
        {
            fogNear = nearDist;
            fogFar = farDist;
        }
        float getFogNear() const { return fogNear; }
        float getFogFar() const { return fogFar; }

        void draw(MeshInstance *instance);
        void flushQueue();
        const std::vector<MeshInstance *> &getEmissiveQueue() const { return emissiveQueue_; }

        bool clipAndDrawNearTextured(const DrawTelemetryClipVert inVerts[3],
                                     float nearD,
                                     const Camera &camera,
                                     const Viewport &viewport,
                                     const Matrix4x4 &viewProjMatrix,
                                     FrameBuffer &framebuffer,
                                     ZBuffer *zBuffer,
                                     const Texture &tex);

        void drawTriangle3D(const Vector3 &v0, const Vector3 &v1, const Vector3 &v2, uint16_t color);
        void drawBlobShadow(const Vector3 &position, float radius, float opacity);

        void drawWaterMesh(MeshInstance *instance, float time);
        void drawWater(float yLevel, float size, Color color, float alpha, float time);
        static constexpr uint16_t reflectWidth() { return REFLECT_WIDTH; }
        static constexpr uint16_t reflectHeight() { return REFLECT_HEIGHT; }
        uint16_t *getReflectBuffer() const { return reflectBuffer; }

        void drawSunSprite(const Vector3 &worldPos, const Color &color, float glow, float sizeScale = 1.0f);
        void setSunEnabled(bool enabled) { sunEnabled = enabled; }
        bool isSunEnabled() const { return sunEnabled; }
        void updateSun(const Vector3 &dir, const Color &color, float intensity, bool visible)
        {
            sunWorldDir = dir;
            sunColor = color;
            sunIntensity = intensity;
            sunVisible = visible;
        }
        bool isSunVisible() const { return sunVisible; }
        Vector3 getSunWorldDir() const { return sunWorldDir; }
        void drawSky();
        void drawBillboardQuads(const BillboardQuad *quads, size_t count);

        void drawText(int16_t x, int16_t y, const char *text,
                      uint16_t color = 0xFFFF)
        {
            HudRenderer::drawText(framebuffer, x, y, text, color);
        }

        void drawText(int16_t x, int16_t y, const char *text, Color color)
        {
            HudRenderer::drawText(framebuffer, x, y, text, color.rgb565);
        }

        void drawTextAdaptive(int16_t x, int16_t y, const char *text)
        {
            if (!text || !*text)
                return;
            const int16_t w = getTextWidth(text);

            const uint8_t cacheKey = static_cast<uint8_t>(
                (static_cast<uint32_t>(y) >> 4) & 0x03u);
            const uint16_t color = HudRenderer::getAdaptiveColor(
                framebuffer, viewport, x, y, w, 8, cacheKey);
            HudRenderer::drawText(framebuffer, x, y, text, color);
        }

        uint16_t getAdaptiveTextColor(int16_t x, int16_t y,
                                      int16_t width = 40,
                                      int16_t height = 8)
        {
            return HudRenderer::getAdaptiveColor(
                framebuffer, viewport, x, y, width, height, 0);
        }

        uint16_t getAdaptiveColor(int16_t x, int16_t y,
                                  int16_t width = 40,
                                  int16_t height = 8,
                                  uint8_t cacheKey = 0)
        {
            return HudRenderer::getAdaptiveColor(
                framebuffer, viewport, x, y, width, height, cacheKey);
        }

        int16_t getTextWidth(const char *text)
        {
            return HudRenderer::getTextWidth(text);
        }
    };
}