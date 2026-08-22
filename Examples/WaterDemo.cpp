#include <cstdio>
#include <cmath>

#if !defined(PIP3D_PC)
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_timer.h>
#include <driver/uart.h>
#else
#include <PipCore/Platforms/Desktop/Runtime.hpp>
#endif

#define TFT_MOSI 6
#define TFT_MISO -1
#define TFT_SCLK 5

#include "Pip3D.hpp"

using namespace pip3D;

static const int8_t TFT_CS_PIN = 7;
static const int8_t TFT_DC_PIN = 8;
static const int8_t TFT_RST_PIN = -1;
static const int8_t TFT_BL_PIN = -1;

static constexpr float CAMERA_FAR = 220.0f;

static constexpr float HOLE_HALF = 3.0f;
static constexpr float RIM_THICK = 3.0f;
static constexpr float WATER_LEVEL = 0.05f;

static InstanceManager g_instances;

static Plane *g_floorNorth = nullptr;
static Plane *g_floorSouth = nullptr;
static Plane *g_floorWest = nullptr;
static Plane *g_floorEast = nullptr;

static Plane *g_waterMesh = nullptr;

static Cube *g_cubeBig = nullptr;
static Cube *g_cubeMid = nullptr;
static Cube *g_cubeSmall = nullptr;

static MeshInstance *g_fnInst = nullptr;
static MeshInstance *g_fsInst = nullptr;
static MeshInstance *g_fwInst = nullptr;
static MeshInstance *g_feInst = nullptr;

static MeshInstance *g_bigInst = nullptr;
static MeshInstance *g_midInst = nullptr;
static MeshInstance *g_smallInst = nullptr;

static float g_demoTime = 0.0f;

static void initMeshes()
{
    const Color floorColor = Color::fromRGB888(72, 78, 92);

    g_floorNorth = new Plane(HOLE_HALF * 2, RIM_THICK, 1, floorColor);
    g_floorSouth = new Plane(HOLE_HALF * 2, RIM_THICK, 1, floorColor);
    g_floorWest = new Plane(RIM_THICK, HOLE_HALF * 2 + RIM_THICK * 2, 1, floorColor);
    g_floorEast = new Plane(RIM_THICK, HOLE_HALF * 2 + RIM_THICK * 2, 1, floorColor);

    g_waterMesh = new Plane(HOLE_HALF * 2, HOLE_HALF * 2, 1, Color::rgb(5, 55, 115));

    g_cubeBig = new Cube(1.6f, Color::WHITE);
    g_cubeMid = new Cube(1.1f, Color::WHITE);
    g_cubeSmall = new Cube(0.7f, Color::WHITE);

    g_waterMesh->setCastShadows(false);
    g_cubeBig->setCastShadows(true);
    g_cubeMid->setCastShadows(true);
    g_cubeSmall->setCastShadows(true);
}

static void buildScene()
{
    const Color floorTint = Color::fromRGB888(82, 88, 102);

    const float edge = HOLE_HALF + RIM_THICK * 0.5f;
    const float sideSpan = HOLE_HALF + RIM_THICK;

    g_fnInst = g_instances.create(g_floorNorth);
    g_fnInst->setPosition(0.0f, 0.0f, -edge);
    g_fnInst->setColor(floorTint);

    g_fsInst = g_instances.create(g_floorSouth);
    g_fsInst->setPosition(0.0f, 0.0f, edge);
    g_fsInst->setColor(floorTint);

    g_fwInst = g_instances.create(g_floorWest);
    g_fwInst->setPosition(-edge, 0.0f, 0.0f);
    g_fwInst->setColor(floorTint);

    g_feInst = g_instances.create(g_floorEast);
    g_feInst->setPosition(edge, 0.0f, 0.0f);
    g_feInst->setColor(floorTint);

    g_waterMesh->setPosition(0.0f, WATER_LEVEL, 0.0f);

    g_bigInst = g_instances.create(g_cubeBig);
    g_bigInst->setPosition(0.0f, 0.8f, 0.0f);
    g_bigInst->setColor(Color::fromRGB888(210, 80, 70));

    g_midInst = g_instances.create(g_cubeMid);
    g_midInst->setPosition(0.0f, 1.95f, 0.0f);
    g_midInst->setColor(Color::fromRGB888(80, 200, 130));

    g_smallInst = g_instances.create(g_cubeSmall);
    g_smallInst->setPosition(0.0f, 2.85f, 0.0f);
    g_smallInst->setColor(Color::fromRGB888(240, 200, 80));
}

static void setupSceneLightingAndCamera(Renderer &r)
{
    Camera &cam = r.getCamera();
    cam.position = Vector3(5.5f, 3.2f, 6.5f);
    cam.target = Vector3(0.0f, 1.4f, 0.0f);
    cam.up = Vector3(0.0f, 1.0f, 0.0f);
    cam.setPerspective(60.0f, 0.1f, CAMERA_FAR);
    cam.markDirty();
    cam.shakeState.trauma = 0.0f;
    cam.shakeState.timePhase = 0.0f;

    Light keyLight;
    keyLight.type = LIGHT_DIRECTIONAL;
    keyLight.direction = Vector3(0.4f, -1.0f, 0.6f);
    keyLight.direction.normalize();
    keyLight.color = Color::rgb(240, 235, 215);
    keyLight.intensity = 1.1f;
    r.setLight(0, keyLight);

    Light fillLight;
    fillLight.type = LIGHT_POINT;
    fillLight.position = Vector3(-4.0f, 2.5f, 3.0f);
    fillLight.color = Color::rgb(80, 160, 255);
    fillLight.intensity = 0.9f;
    fillLight.setRange(14.0f);
    r.setLight(1, fillLight);

    Light rimLight;
    rimLight.type = LIGHT_POINT;
    rimLight.position = Vector3(4.0f, 2.0f, -3.0f);
    rimLight.color = Color::rgb(255, 150, 90);
    rimLight.intensity = 0.8f;
    rimLight.setRange(12.0f);
    r.setLight(2, rimLight);
}

static void updateScene(Renderer &r, float dt)
{
    (void)r;
    g_demoTime += dt;

    const float spin = g_demoTime * 18.0f;

    if (g_bigInst)
        g_bigInst->setEuler(0.0f, spin, 0.0f);
    if (g_midInst)
        g_midInst->setEuler(0.0f, -spin * 1.4f, 0.0f);
    if (g_smallInst)
        g_smallInst->setEuler(0.0f, spin * 1.8f, 0.0f);
}

static void renderWorld(Renderer &r)
{
    if (g_fnInst)
        r.drawMeshInstanceStatic(g_fnInst);
    if (g_fsInst)
        r.drawMeshInstanceStatic(g_fsInst);
    if (g_fwInst)
        r.drawMeshInstanceStatic(g_fwInst);
    if (g_feInst)
        r.drawMeshInstanceStatic(g_feInst);

    if (g_bigInst)
        r.drawMeshInstanceStatic(g_bigInst);
    if (g_midInst)
        r.drawMeshInstanceStatic(g_midInst);
    if (g_smallInst)
        r.drawMeshInstanceStatic(g_smallInst);

    r.drawWaterMesh(g_waterMesh, g_demoTime);
}

static void drawHud(Renderer &r)
{
    r.drawText(12, 12, "PIP3D SSPR WATER DEMO", Color::WHITE);
    r.drawText(12, 22, "Screen-Space Reflections", Color::rgb(0, 220, 255));

    char buf[64];

    snprintf(buf, sizeof(buf), "FPS: %.1f | Frame: %.1f ms",
             r.getFPS(), r.getFrameTime() / 1000.0f);
    r.drawText(12, 38, buf, Color::WHITE);

    snprintf(buf, sizeof(buf), "Triangles: %lu (Culled: %lu)",
             (unsigned long)r.getStatsTrianglesTotal(),
             (unsigned long)r.getStatsTrianglesBackfaceCulled());
    r.drawText(12, 50, buf, Color::rgb(170, 230, 110));

    snprintf(buf, sizeof(buf), "Free SRAM: %lu KB",
             (unsigned long)(MemUtils::getFreeHeap() / 1024));
    r.drawText(12, 62, buf, Color::rgb(255, 180, 50));
}

extern "C" void app_main(void)
{
#if !defined(PIP3D_PC)
    vTaskDelay(pdMS_TO_TICKS(500));
#endif

    std::printf("=== Pip3D SSPR Water Demo ===\n");

    Renderer &r = begin3D(SCREEN_WIDTH, SCREEN_HEIGHT,
                          TFT_CS_PIN, TFT_DC_PIN, TFT_RST_PIN, TFT_BL_PIN,
                          80000000);

    if (!r.isInitialized())
    {
        std::printf("Renderer init failed!\n");
        return;
    }

    initMeshes();
    buildScene();

    r.setShadingMode(SHADING_GOURAUD);
    r.setBackfaceCullingEnabled(true);

    r.setShadowsEnabled(true);
    r.setShadowPlaneY(0.0f);
    r.setShadowOpacity(0.5f);
    r.setShadowColor(Color::fromRGB888(8, 10, 18));

    r.setSkybox(DAY);
    r.setSkyboxEnabled(true);
    r.setClearColor(Color::fromRGB888(90, 150, 220));

    r.setFogEnabled(true);
    r.setFogColor(Color::fromRGB888(180, 210, 235));
    r.setFogLimits(6.0f, 30.0f);

    setupSceneLightingAndCamera(r);

    g_demoTime = 0.0f;
    uint64_t lastTickUs = 0;

    while (true)
    {
#if defined(PIP3D_PC)
        if (pipcore::desktop::Runtime::instance().shouldQuit())
            break;
#endif

        const uint64_t nowUs = pip3D::getSystemMicros();
        float dt = (lastTickUs == 0) ? 0.016f : static_cast<float>(nowUs - lastTickUs) * 1e-6f;
        lastTickUs = nowUs;

        if (dt < 0.001f)
            dt = 0.001f;
        if (dt > 0.05f)
            dt = 0.05f;

        updateScene(r, dt);

        for (int band = 0; band < SCREEN_BAND_COUNT; ++band)
        {
            r.beginFrameBand(band);
            r.drawSkyboxBackground();

            renderWorld(r);
            r.flushQueue();

            drawHud(r);

            r.endFrameBand(band);
        }
    }
}
