#include <cstdio>
#include <cmath>
#include <cstring>
#include <vector>
#include <algorithm>

#if !defined(PIP3D_PC)
#include <esp_timer.h>
#include <driver/uart.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#else
#include <thread>
#if defined(_WIN32)
#include <windows.h>
#endif
#endif

#define TFT_MOSI 6
#define TFT_MISO -1
#define TFT_SCLK 5

#include <Pip3D.hpp>

using namespace pip3D;

static const int8_t TFT_CS_PIN = 7;
static const int8_t TFT_DC_PIN = 8;
static const int8_t TFT_RST_PIN = -1;
static const int8_t TFT_BL_PIN = -1;

static Plane *g_floorMesh = nullptr;
static Plane *g_backWallMesh = nullptr;

static MeshInstance *g_floorInstance = nullptr;
static MeshInstance *g_backWallInstance = nullptr;

static std::vector<MeshInstance *> g_sceneInstances;
static std::vector<BandCullItem> g_bandCullList;

struct Player
{
    Vector3 eyePos = Vector3(0.0f, 2.0f, 0.4f);
    float yawDeg = 180.0f;
    float pitchDeg = 0.0f;
    float moveSpeed = 4.0f;
};
static Player g_player;

static uint32_t g_frameCounter = 0;
static volatile bool g_captureRequested = false;
static bool g_capturing = false;

static void buildScene()
{
    g_sceneInstances.clear();
    g_bandCullList.clear();

    g_floorMesh = new Plane(30.0f, 30.0f, 2, 1.0f);
    g_floorMesh->setCastShadows(false);
    g_floorMesh->setSingleColorLighting(true);

    g_floorInstance = new MeshInstance(g_floorMesh);
    g_floorInstance->setPosition(0.0f, 0.0f, 0.0f);
    g_floorInstance->setColor(Color::rgb(25, 25, 28));

    g_backWallMesh = new Plane(24.0f, 16.0f, 2, 1.0f);
    g_backWallMesh->setCastShadows(false);
    g_backWallMesh->setSingleColorLighting(true);

    g_backWallInstance = new MeshInstance(g_backWallMesh);
    g_backWallInstance->setPosition(0.0f, 5.0f, -4.0f);
    g_backWallInstance->setEuler(90.0f, 0.0f, 0.0f);
    g_backWallInstance->setColor(Color::rgb(30, 30, 35));

    g_sceneInstances.push_back(g_floorInstance);
    g_sceneInstances.push_back(g_backWallInstance);
    g_bandCullList.reserve(8);
}

static void setupRGBVennLights(Renderer &r)
{
    r.clearPointLights();

    const float lightZ = -3.1f;
    const float lightRange = 6.0f;
    const float lightIntensity = 2.4f;

    r.addPointLight(Vector3(0.0f, 2.7f, lightZ), Color::rgb(255, 0, 0), lightRange, lightIntensity);

    r.addPointLight(Vector3(-0.95f, 1.4f, lightZ), Color::rgb(0, 255, 0), lightRange, lightIntensity);

    r.addPointLight(Vector3(0.95f, 1.4f, lightZ), Color::rgb(0, 0, 255), lightRange, lightIntensity);
}

static void applyPlayerToCamera(Renderer &r)
{
    Camera &cam = r.getCamera();
    cam.position = g_player.eyePos;
    cam.up = Vector3(0.0f, 1.0f, 0.0f);
    cam.setOrientationDeg(g_player.yawDeg, g_player.pitchDeg);
}

static void updatePlayerInput(float dt)
{
#if defined(PIP3D_PC) && defined(_WIN32)
    static bool s_mouseCaptured = false;
    static bool s_lastEscDown = false;

    HWND hwnd = GetForegroundWindow();
    const bool isWindowActive = (hwnd != nullptr);

    const bool escDown = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
    if (escDown && !s_lastEscDown && s_mouseCaptured)
    {
        s_mouseCaptured = false;
        ShowCursor(TRUE);
    }
    s_lastEscDown = escDown;

    if (!s_mouseCaptured && isWindowActive)
    {
        if (GetAsyncKeyState(VK_LBUTTON) & 0x8000)
        {
            s_mouseCaptured = true;
            ShowCursor(FALSE);

            RECT rc;
            GetWindowRect(hwnd, &rc);
            SetCursorPos((rc.left + rc.right) / 2, (rc.top + rc.bottom) / 2);
        }
    }

    if (s_mouseCaptured && isWindowActive)
    {
        RECT rc;
        GetWindowRect(hwnd, &rc);
        const int centerX = (rc.left + rc.right) / 2;
        const int centerY = (rc.top + rc.bottom) / 2;

        POINT cur;
        GetCursorPos(&cur);

        const float dx = static_cast<float>(cur.x - centerX);
        const float dy = static_cast<float>(cur.y - centerY);

        if (fabsf(dx) > 0.0f || fabsf(dy) > 0.0f)
        {
            g_player.yawDeg -= dx * 0.08f;
            g_player.pitchDeg -= dy * 0.08f;
            SetCursorPos(centerX, centerY);
        }
    }

    float speed = g_player.moveSpeed;
    if (GetAsyncKeyState(VK_SHIFT) & 0x8000)
        speed *= 2.0f;

    float fwd = 0.0f, right = 0.0f, up = 0.0f;

    if (GetAsyncKeyState('W') & 0x8000)
        fwd += speed * dt;
    if (GetAsyncKeyState('S') & 0x8000)
        fwd -= speed * dt;
    if (GetAsyncKeyState('D') & 0x8000)
        right += speed * dt;
    if (GetAsyncKeyState('A') & 0x8000)
        right -= speed * dt;
    if (GetAsyncKeyState(VK_SPACE) & 0x8000)
        up += speed * dt;
    if (GetAsyncKeyState('C') & 0x8000)
        up -= speed * dt;

    if (GetAsyncKeyState(VK_LEFT) & 0x8000)
        g_player.yawDeg -= 80.0f * dt;
    if (GetAsyncKeyState(VK_RIGHT) & 0x8000)
        g_player.yawDeg += 80.0f * dt;
    if (GetAsyncKeyState(VK_UP) & 0x8000)
        g_player.pitchDeg += 80.0f * dt;
    if (GetAsyncKeyState(VK_DOWN) & 0x8000)
        g_player.pitchDeg -= 80.0f * dt;

    g_player.pitchDeg = fmaxf(-85.0f, fminf(85.0f, g_player.pitchDeg));

    const float yr = g_player.yawDeg * kDegToRad;
    float sy, cy;
    FastMath::fastSinCos(yr, sy, cy);

    const Vector3 fwdVec(sy, 0.0f, cy);
    const Vector3 rightVec(-cy, 0.0f, sy);

    g_player.eyePos += fwdVec * fwd + rightVec * right + Vector3(0.0f, up, 0.0f);
#endif
}

static void processUartCommand(Renderer &r, const char *cmd)
{
    if (cmd[0] == 'M' && cmd[1] == ':')
    {
        float fwd = 0.0f, right = 0.0f, up = 0.0f, yawDelta = 0.0f, pitchDelta = 0.0f;
        if (std::sscanf(cmd + 2, "%f,%f,%f,%f,%f", &fwd, &right, &up, &yawDelta, &pitchDelta) == 5)
        {
            g_player.yawDeg -= yawDelta;
            g_player.pitchDeg += pitchDelta;
            g_player.pitchDeg = fmaxf(-85.0f, fminf(85.0f, g_player.pitchDeg));

            const float yr = g_player.yawDeg * kDegToRad;
            float sy, cy;
            FastMath::fastSinCos(yr, sy, cy);

            const Vector3 fwdVec(sy, 0.0f, cy);
            const Vector3 rightVec(-cy, 0.0f, sy);

            g_player.eyePos += fwdVec * fwd + rightVec * right + Vector3(0.0f, up, 0.0f);
        }
    }
    else if (cmd[0] == 'P' && cmd[1] == ':')
    {
        float yaw, pitch, x, y, z;
        if (std::sscanf(cmd + 2, "%f,%f,%f,%f,%f", &yaw, &pitch, &x, &y, &z) == 5)
        {
            g_player.yawDeg = yaw;
            g_player.pitchDeg = pitch;
            g_player.eyePos = Vector3(x, y, z);
        }
    }
    else if (cmd[0] == 'F' && cmd[1] == ':')
    {
        float fov = 75.0f;
        if (std::sscanf(cmd + 2, "%f", &fov) == 1)
        {
            r.getCamera().setPerspective(fov, 0.1f, 100.0f);
        }
    }
    else if (cmd[0] == 'C' && cmd[1] == '\0')
    {
        g_captureRequested = true;
    }
}

static inline void sendUartBytes(const char *data, size_t len)
{
#if !defined(PIP3D_PC)
    uart_write_bytes(UART_NUM_0, data, len);
#else
    (void)pipcore::desktop::Runtime::instance().serialWrite(reinterpret_cast<const uint8_t *>(data), len);
#endif
}

static void pollUart(Renderer &r)
{
    static char cmdBuf[128];
    static size_t cmdLen = 0;

#if !defined(PIP3D_PC)
    uint8_t ch;
    while (uart_read_bytes(UART_NUM_0, &ch, 1, 0) > 0)
    {
        if (ch == '\n' || ch == '\r')
        {
            if (cmdLen > 0)
            {
                cmdBuf[cmdLen] = '\0';
                processUartCommand(r, cmdBuf);
                cmdLen = 0;
            }
        }
        else if (cmdLen < sizeof(cmdBuf) - 1)
        {
            cmdBuf[cmdLen++] = (char)ch;
        }
    }
#else
    auto &runtime = pipcore::desktop::Runtime::instance();
    while (runtime.serialAvailable() > 0)
    {
        int c = runtime.serialRead();
        if (c < 0)
            break;
        char ch = static_cast<char>(c);
        if (ch == '\n' || ch == '\r')
        {
            if (cmdLen > 0)
            {
                cmdBuf[cmdLen] = '\0';
                processUartCommand(r, cmdBuf);
                cmdLen = 0;
            }
        }
        else if (cmdLen < sizeof(cmdBuf) - 1)
        {
            cmdBuf[cmdLen++] = ch;
        }
    }
#endif
}

static void drawHud(Renderer &r)
{
    char buf[128];
    const uint32_t freeRamKb = static_cast<uint32_t>(MemUtils::getFreeHeap() / 1024);

    std::snprintf(buf, sizeof(buf),
                  "RGB DEFERRED STRESS | FPS: %.1f | Frame: %u ms | RAM: %u KB",
                  r.getFPS(), (unsigned)(r.getFrameTime() / 1000), (unsigned)freeRamKb);
    r.drawText(10, 10, buf, Color::GRAY);

    std::snprintf(buf, sizeof(buf),
                  "Pos: (%4.1f, %4.1f, %4.1f) | 3 Lights Full-Screen Overlap",
                  g_player.eyePos.x, g_player.eyePos.y, g_player.eyePos.z);
    r.drawText(10, 22, buf, Color::rgb(0, 220, 255));
}

extern "C" void app_main(void)
{
#if !defined(PIP3D_PC)
    vTaskDelay(pdMS_TO_TICKS(100));

    uart_config_t uart_cfg = {};
    uart_cfg.baud_rate = 115200;
    uart_cfg.data_bits = UART_DATA_8_BITS;
    uart_cfg.parity = UART_PARITY_DISABLE;
    uart_cfg.stop_bits = UART_STOP_BITS_1;
    uart_cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    uart_cfg.source_clk = UART_SCLK_DEFAULT;
    uart_param_config(UART_NUM_0, &uart_cfg);
    uart_driver_install(UART_NUM_0, 512, 1024, 0, NULL, 0);
#endif

    Renderer &r = begin3D(SCREEN_WIDTH, SCREEN_HEIGHT,
                          TFT_CS_PIN, TFT_DC_PIN, TFT_RST_PIN, TFT_BL_PIN, 80000000);
    if (!r.isInitialized())
    {
        std::printf("[ERROR] Renderer init failed!\n");
        return;
    }

    buildScene();

    r.setBackfaceCullingEnabled(true);
    r.setShadingMode(SHADING_FLAT);
    r.setMipmapsEnabled(false);
    r.setShadowsEnabled(false);
    r.setCloudsEnabled(false);
    r.setFogEnabled(false);

    r.setSkyboxEnabled(false);
    r.setClearColor(Color::rgb(5, 5, 8));
    r.setSunEnabled(false);
    r.setMainDirectionalLight(Vector3(0, -1, 0), Color::BLACK, 0.0f);

    r.setDeferredLightingEnabled(true);
    setupRGBVennLights(r);

    Camera &cam = r.getCamera();
    cam.setPerspective(75.0f, 0.1f, 100.0f);
    applyPlayerToCamera(r);

    std::printf("[INFO] Pip3D RGB Deferred Stress Test Ready.\n");

    uint64_t lastTickUs = pip3D::getSystemMicros();

    while (true)
    {
#if defined(PIP3D_PC)
        if (pipcore::desktop::Runtime::instance().shouldQuit())
            break;
#endif

        const uint64_t nowUs = pip3D::getSystemMicros();
        const float dt = fminf(static_cast<float>(nowUs - lastTickUs) * 1e-6f, 0.1f);
        lastTickUs = nowUs;
        ++g_frameCounter;

        pollUart(r);
        updatePlayerInput(dt);
        applyPlayerToCamera(r);
        r.updateCameraView();

        r.buildBandCullList(g_sceneInstances, g_bandCullList);

        for (int band = 0; band < SCREEN_BAND_COUNT; ++band)
        {
            r.beginFrameBand(band);

            r.fillSkyGradient();

            r.drawBandInstances(band, g_bandCullList.data(), g_bandCullList.size());

            r.applyDeferredLighting();

            if (band == 0)
                drawHud(r);

            if (band == 0 && g_captureRequested)
            {
                g_capturing = true;
                g_captureRequested = false;
                const char hdr[] = "\n===SHOT_BEGIN===\n";
                sendUartBytes(hdr, sizeof(hdr) - 1);
            }

            if (g_capturing)
            {
                const uint16_t *buf = r.getFrameBuffer();
                const size_t bandBytes = static_cast<size_t>(SCREEN_WIDTH) * SCREEN_BAND_HEIGHT * sizeof(uint16_t);
                sendUartBytes((const char *)buf, bandBytes);

                if (band == SCREEN_BAND_COUNT - 1)
                {
                    const char ftr[] = "\n===SHOT_END===\n";
                    sendUartBytes(ftr, sizeof(ftr) - 1);
                    g_capturing = false;
                }
            }

            r.endFrameBand(band);
        }

#if defined(PIP3D_PC)
        pipcore::desktop::Runtime::instance().delayMs(1);
#else
        vTaskDelay(1);
#endif
    }
}