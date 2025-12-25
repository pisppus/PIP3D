#ifndef PIP3D_DEBUG_DRAW_H
#define PIP3D_DEBUG_DRAW_H

#include "../Core.h"
#include "DebugConfig.h"
#include "../../Math/Collision.h"
#include <stdint.h>

namespace pip3D
{

    class Renderer;

    namespace Debug
    {

        enum DebugCategory : uint16_t
        {
            DEBUG_CATEGORY_NONE = 0,
            DEBUG_CATEGORY_PHYSICS = 1u << 0,
            DEBUG_CATEGORY_CAMERA = 1u << 1,
            DEBUG_CATEGORY_MESHES = 1u << 2,
            DEBUG_CATEGORY_LIGHTING = 1u << 3,
            DEBUG_CATEGORY_PERFORMANCE = 1u << 4,
            DEBUG_CATEGORY_USER = 1u << 5,
            DEBUG_CATEGORY_ALL = 0xFFFFu
        };

        struct DebugLine
        {
            Vector3 a;
            Vector3 b;
            uint16_t color;
            uint16_t categories;
            uint16_t framesLeft;
            uint8_t thickness;
            uint8_t reserved;
        };

        class DebugDraw
        {
        public:
            static void setCategories(uint16_t mask);
            static void enableCategories(uint16_t mask);
            static void disableCategories(uint16_t mask);
            static uint16_t getCategories();

            static void beginFrame();
            static bool hasPrimitives();

            static void addLine(const Vector3 &a, const Vector3 &b,
                                uint16_t color,
                                uint16_t categories = DEBUG_CATEGORY_USER,
                                uint16_t lifetimeFrames = 1,
                                uint8_t thickness = 1);

            static void addRay(const Vector3 &origin, const Vector3 &dir,
                               float length,
                               uint16_t color,
                               uint16_t categories = DEBUG_CATEGORY_USER,
                               uint16_t lifetimeFrames = 1,
                               uint8_t thickness = 1);

            static void addAABB(const AABB &box,
                                uint16_t color,
                                uint16_t categories = DEBUG_CATEGORY_PHYSICS,
                                uint16_t lifetimeFrames = 1,
                                uint8_t thickness = 1);

            static void addSphere(const Vector3 &center, float radius,
                                  uint16_t color,
                                  uint16_t categories = DEBUG_CATEGORY_PHYSICS,
                                  uint16_t lifetimeFrames = 1,
                                  uint8_t thickness = 1);

            static void addAxes(const Vector3 &origin, float size,
                                uint16_t categories = DEBUG_CATEGORY_CAMERA,
                                uint16_t lifetimeFrames = 1,
                                uint8_t thickness = 1);

            static void render(Renderer &renderer);
        };

    }
}

#if ENABLE_DEBUG_DRAW

#define DBG_LINE(renderer, a, b, color, categories)                  \
    do                                                               \
    {                                                                \
        ::pip3D::Debug::DebugDraw::addLine(a, b, color, categories); \
    } while (0)

#define DBG_RAY(renderer, origin, dir, length, color, categories)                  \
    do                                                                             \
    {                                                                              \
        ::pip3D::Debug::DebugDraw::addRay(origin, dir, length, color, categories); \
    } while (0)

#define DBG_AABB(renderer, box, color, categories)                  \
    do                                                              \
    {                                                               \
        ::pip3D::Debug::DebugDraw::addAABB(box, color, categories); \
    } while (0)

#define DBG_SPHERE(renderer, center, radius, color, categories)                  \
    do                                                                           \
    {                                                                            \
        ::pip3D::Debug::DebugDraw::addSphere(center, radius, color, categories); \
    } while (0)

#define DBG_AXES(renderer, origin, size, categories)                  \
    do                                                                \
    {                                                                 \
        ::pip3D::Debug::DebugDraw::addAxes(origin, size, categories); \
    } while (0)

#else

#define DBG_LINE(renderer, a, b, color, categories) \
    do                                              \
    {                                               \
        (void)(renderer);                           \
    } while (0)
#define DBG_RAY(renderer, origin, dir, length, color, categories) \
    do                                                            \
    {                                                             \
        (void)(renderer);                                         \
    } while (0)
#define DBG_AABB(renderer, box, color, categories) \
    do                                             \
    {                                              \
        (void)(renderer);                          \
    } while (0)
#define DBG_SPHERE(renderer, center, radius, color, categories) \
    do                                                          \
    {                                                           \
        (void)(renderer);                                       \
    } while (0)
#define DBG_AXES(renderer, origin, size, categories) \
    do                                               \
    {                                                \
        (void)(renderer);                            \
    } while (0)

#endif

#endif
