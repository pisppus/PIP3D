#ifndef PIP3D_H
#define PIP3D_H

#include "Core/Core.h"
#include "Core/Camera.h"
#include "Core/Frustum.h"
#include "Core/Instance.h"
#include "Core/Jobs.h"
#include "Core/Debug/DebugConfig.h"
#include "Core/Debug/Logging.h"
#include "Core/Debug/DebugDraw.h"

#include "Math/Math.h"
#include "Math/Collision.h"

#include "Physics/Physics.h"

#include "Geometry/Mesh.h"
#include "Geometry/PrimitiveShapes.h"

#include "Rendering/Display/ZBuffer.h"
#include "Rendering/Lighting/Lighting.h"
#include "Rendering/Lighting/Shadow.h"
#include "Rendering/Rasterizer/Rasterizer.h"
#include "Rendering/Rasterizer/Shading.h"
#include "Rendering/Display/FrameBuffer.h"
#include "Rendering/Renderer.h"

#include "Rendering/Display/Drivers/ST7789Driver.h"
#include "Graphics/Font.h"
#include "Graphics/Screen.h"

#include "Scene/SceneHelper.h"
#include "Utils/ObjectUtils.h"
#include "Utils/CameraUtils.h"
#include "Utils/CameraTimeline.h"
#include "Utils/DayNightCycle.h"
#include "Utils/TimeUtils.h"
#include "Utils/FX.h"
#include "Input/Input.h"
#include "Scene/SceneNode.h"
#include "Scene/SceneGraph.h"
#include "Scene/CharacterController.h"

#define PIP3D_VERSION_MAJOR 0
#define PIP3D_VERSION_MINOR 1
#define PIP3D_VERSION_PATCH 1

#define PIP3D_STRINGIFY_IMPL(x) #x
#define PIP3D_STRINGIFY(x) PIP3D_STRINGIFY_IMPL(x)

namespace pip3D
{
    inline const char *getVersion()
    {
        return PIP3D_STRINGIFY(PIP3D_VERSION_MAJOR) "." PIP3D_STRINGIFY(PIP3D_VERSION_MINOR) "." PIP3D_STRINGIFY(PIP3D_VERSION_PATCH) " - Alpha";
    }

    inline Color RGB888(uint8_t r, uint8_t g, uint8_t b)
    {
        return Color::rgb(r, g, b);
    }

    inline Renderer &renderer()
    {
        static Renderer *s_renderer = nullptr;
        if (!s_renderer)
        {
            s_renderer = new Renderer();
        }
        return *s_renderer;
    }

    inline Renderer &begin3D(uint16_t width,
                             uint16_t height,
                             int8_t cs,
                             int8_t dc,
                             int8_t rst,
                             int8_t bl = -1,
                             uint32_t spi_freq = 80000000)
    {
        Renderer &r = renderer();
        DisplayConfig cfg(width, height, cs, dc, rst);
        cfg.bl = bl;
        cfg.spi_freq = spi_freq;

        if (r.init(cfg))
        {
            r.setSkyboxWithLighting(SKYBOX_DAY);
            r.setShadowsEnabled(true);
            r.setShadowPlaneY(0.0f);
        }

        return r;
    }

    inline Renderer &begin3D(int8_t cs,
                             int8_t dc,
                             int8_t rst,
                             int8_t bl = -1,
                             uint32_t spi_freq = 80000000)
    {
        return begin3D(320, 240, cs, dc, rst, bl, spi_freq);
    }
}

#endif

