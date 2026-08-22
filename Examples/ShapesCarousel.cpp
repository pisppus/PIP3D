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

static InstanceManager g_instances;
static TimeOfDayController g_timeOfDay;

static Cube *g_cubeMesh = nullptr;
static Sphere *g_sphereMesh = nullptr;
static Cylinder *g_cylinderMesh = nullptr;
static TrefoilKnot *g_knotMesh = nullptr;
static Plane *g_planeMesh = nullptr;

static MeshInstance *g_planeInstance = nullptr;
static MeshInstance *g_figSphere = nullptr;
static MeshInstance *g_figCube = nullptr;
static MeshInstance *g_figCylinder = nullptr;
static MeshInstance *g_figKnot = nullptr;

static float g_demoTime = 0.0f;

static void initMeshes()
{
    if (!g_cubeMesh)
        g_cubeMesh = new Cube(1.0f, Color::WHITE);
    if (!g_sphereMesh)
        g_sphereMesh = new Sphere(0.5f, 12, 10, Color::WHITE);
    if (!g_cylinderMesh)
        g_cylinderMesh = new Cylinder(0.5f, 1.0f, 12, Color::WHITE);
    if (!g_knotMesh)
        g_knotMesh = new TrefoilKnot(0.5f, 32, 8, Color::WHITE);
    if (!g_planeMesh)
        g_planeMesh = new Plane(1.0f, 1.0f, 1, Color::WHITE);
}

static void buildBenchmarkScene()
{
    g_planeInstance = g_instances.create(g_planeMesh);
    g_planeInstance->setPosition(0.0f, 0.0f, 0.0f);
    g_planeInstance->setScale(18.0f, 1.0f, 18.0f);
    g_planeInstance->setColor(Color::fromRGB888(70, 76, 88));

    g_figSphere = g_instances.create(g_sphereMesh);
    g_figSphere->setColor(Color::fromRGB888(255, 75, 75));

    g_figCube = g_instances.create(g_cubeMesh);
    g_figCube->setColor(Color::fromRGB888(75, 255, 75));

    g_figCylinder = g_instances.create(g_cylinderMesh);
    g_figCylinder->setColor(Color::fromRGB888(75, 150, 255));

    g_figKnot = g_instances.create(g_knotMesh);
    g_figKnot->setColor(Color::fromRGB888(255, 180, 50));
}

static void updateScene(Renderer &r, float dt)
{
    g_demoTime += dt;

    g_timeOfDay.update(dt);

    float camRadius = 12.0f;
    float camSpeed = 0.25f;
    float camX = cosf(g_demoTime * camSpeed) * camRadius;
    float camZ = sinf(g_demoTime * camSpeed) * camRadius;
    float camY = 7.5f + sinf(g_demoTime * 0.5f) * 1.5f;

    Camera &cam = r.getCamera();
    cam.position = Vector3(camX, camY, camZ);
    cam.target = Vector3(0.0f, 1.5f, 0.0f);
    cam.up = Vector3(0.0f, 1.0f, 0.0f);
    cam.setPerspective(60.0f, 0.1f, CAMERA_FAR);
    cam.markDirty();

    float orbitRadius = 5.5f;
    float orbitSpeed = 0.8f;

    float angle1 = g_demoTime * orbitSpeed;
    Vector3 pos1(cosf(angle1) * orbitRadius,
                 1.5f + sinf(g_demoTime * 2.0f) * 0.3f,
                 sinf(angle1) * orbitRadius);
    g_figSphere->setPosition(pos1);
    g_figSphere->setEuler(g_demoTime * 45.0f, g_demoTime * 90.0f, 0.0f);
    g_figSphere->setScale(2.8f);

    float angle2 = angle1 + (PI * 0.5f);
    Vector3 pos2(cosf(angle2) * orbitRadius,
                 1.4f + cosf(g_demoTime * 2.0f) * 0.3f,
                 sinf(angle2) * orbitRadius);
    g_figCube->setPosition(pos2);
    g_figCube->setEuler(g_demoTime * 60.0f, 0.0f, g_demoTime * 30.0f);
    g_figCube->setScale(2.5f);

    float angle3 = angle1 + PI;
    Vector3 pos3(cosf(angle3) * orbitRadius,
                 1.5f + sinf(g_demoTime * 1.5f) * 0.3f,
                 sinf(angle3) * orbitRadius);
    g_figCylinder->setPosition(pos3);
    g_figCylinder->setEuler(0.0f, g_demoTime * 120.0f, g_demoTime * 45.0f);
    g_figCylinder->setScale(2.0f, 3.0f, 2.0f);

    float angle4 = angle1 + (PI * 1.5f);
    Vector3 pos4(cosf(angle4) * orbitRadius,
                 1.6f + cosf(g_demoTime * 1.5f) * 0.3f,
                 sinf(angle4) * orbitRadius);
    g_figKnot->setPosition(pos4);
    g_figKnot->setEuler(g_demoTime * 45.0f, g_demoTime * 45.0f, g_demoTime * 45.0f);
    g_figKnot->setScale(2.2f);
}

static void renderWorld(Renderer &r)
{
    if (g_planeInstance && g_planeInstance->isVisible())
    {
        r.drawMeshInstanceStatic(g_planeInstance);
    }

    if (g_figSphere && g_figSphere->isVisible())
        r.draw(g_figSphere);
    if (g_figCube && g_figCube->isVisible())
        r.draw(g_figCube);
    if (g_figCylinder && g_figCylinder->isVisible())
        r.draw(g_figCylinder);
    if (g_figKnot && g_figKnot->isVisible())
        r.draw(g_figKnot);
}

static void drawHud(Renderer &r)
{
    r.drawText(12, 12, "PIP3D CORE BENCHMARK", Color::BLACK);
    r.drawText(12, 22, "Fixed workload testing suite", Color::BLACK);

    char buf[64];

    snprintf(buf, sizeof(buf), "FPS: %.1f (avg: %.1f)", r.getFPS(), r.getAverageFPS());
    r.drawText(12, 42, buf, Color::BLACK);

    snprintf(buf, sizeof(buf), "Frame Time: %.1f ms", r.getFrameTime() / 1000.0f);
    r.drawText(12, 52, buf, Color::BLACK);

    snprintf(buf, sizeof(buf), "Free SRAM: %lu KB", (unsigned long)(MemUtils::getFreeHeap() / 1024));
    r.drawText(12, 72, buf, Color::BLACK);

    snprintf(buf, sizeof(buf), "Free PSRAM: %lu KB", (unsigned long)(MemUtils::getFreePSRAM() / 1024));
    r.drawText(12, 82, buf, Color::BLACK);

    snprintf(buf, sizeof(buf), "Max Alloc Block: %lu KB", (unsigned long)(MemUtils::getLargestFreeBlock() / 1024));
    r.drawText(12, 92, buf, Color::BLACK);

    uint32_t activeTris = r.getStatsTrianglesTotal() - r.getStatsTrianglesBackfaceCulled();
    snprintf(buf, sizeof(buf), "Triangles: %lu (Total: %lu)", (unsigned long)activeTris, (unsigned long)r.getStatsTrianglesTotal());
    r.drawText(12, 216, buf, Color::BLACK);

    snprintf(buf, sizeof(buf), "Backface Culled: %lu", (unsigned long)r.getStatsTrianglesBackfaceCulled());
    r.drawText(12, 226, buf, Color::BLACK);
}

extern "C" void app_main(void)
{
#if !defined(PIP3D_PC)
    vTaskDelay(pdMS_TO_TICKS(100));
#endif

    Renderer &r = begin3D(SCREEN_WIDTH, SCREEN_HEIGHT, TFT_CS_PIN, TFT_DC_PIN, TFT_RST_PIN, TFT_BL_PIN, 80000000);
    if (!r.isInitialized())
    {
        std::printf("Pip3D init failed\n");
        return;
    }

    r.setShadingMode(SHADING_GOURAUD);

    Camera &cam = r.getCamera();
    cam.position = Vector3(0.0f, 7.5f, -13.0f);
    cam.target = Vector3(0.0f, 1.2f, 0.0f);
    cam.up = Vector3(0.0f, 1.0f, 0.0f);
    cam.setPerspective(60.0f, 0.1f, CAMERA_FAR);
    cam.markDirty();

    initMeshes();
    buildBenchmarkScene();

    TimeOfDayConfig todCfg;
    todCfg.dayLengthSeconds = 30.0f;
    todCfg.startHour = 12.0f;
    todCfg.baseIntensity = 1.6f;
    todCfg.nightIntensity = 0.35f;
    todCfg.autoAdvance = false;
    g_timeOfDay.init(&r, todCfg);

    r.setBackfaceCullingEnabled(true);

    r.setShadowsEnabled(true);
    r.setShadowPlaneY(0.0f);
    r.setShadowOpacity(0.38f);
    r.setShadowColor(Color::fromRGB888(16, 20, 28));

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