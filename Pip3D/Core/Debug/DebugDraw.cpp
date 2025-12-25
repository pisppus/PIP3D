#include "DebugDraw.h"

#if ENABLE_DEBUG_DRAW

#include "../../Rendering/Renderer.h"
#include <math.h>

namespace pip3D
{
    namespace Debug
    {

        static constexpr int MAX_DEBUG_LINES = 256;

        static DebugLine g_lines[MAX_DEBUG_LINES];
        static uint16_t g_lineCount = 0;
        static uint16_t g_categoriesMask = DEBUG_CATEGORY_ALL;

        static bool isCategoryEnabled(uint16_t categories)
        {
            return (g_categoriesMask & categories) != 0u;
        }

        static void addLineImpl(const Vector3 &a, const Vector3 &b,
                                uint16_t color,
                                uint16_t categories,
                                uint16_t lifetimeFrames,
                                uint8_t thickness,
                                bool skipCategoryCheck)
        {
            if (!skipCategoryCheck && !isCategoryEnabled(categories))
                return;
            if (g_lineCount >= MAX_DEBUG_LINES)
                return;
            DebugLine &ln = g_lines[g_lineCount];
            ln.a = a;
            ln.b = b;
            ln.color = color;
            ln.categories = categories;
            ln.framesLeft = lifetimeFrames;
            ln.thickness = thickness == 0 ? 1 : thickness;
            ln.reserved = 0;
            g_lineCount++;
        }

        void DebugDraw::setCategories(uint16_t mask)
        {
            g_categoriesMask = mask;
        }

        void DebugDraw::enableCategories(uint16_t mask)
        {
            g_categoriesMask |= mask;
        }

        void DebugDraw::disableCategories(uint16_t mask)
        {
            g_categoriesMask &= static_cast<uint16_t>(~mask);
        }

        uint16_t DebugDraw::getCategories()
        {
            return g_categoriesMask;
        }

        void DebugDraw::beginFrame()
        {
            if (g_lineCount == 0)
                return;

            uint16_t dst = 0;
            for (uint16_t i = 0; i < g_lineCount; ++i)
            {
                DebugLine &ln = g_lines[i];
                if (ln.framesLeft > 0)
                {
                    ln.framesLeft--;
                    if (ln.framesLeft == 0)
                        continue;
                }

                if (dst != i)
                {
                    g_lines[dst] = ln;
                }
                dst++;
            }
            g_lineCount = dst;
        }

        bool DebugDraw::hasPrimitives()
        {
            return g_lineCount > 0;
        }

        void DebugDraw::addLine(const Vector3 &a, const Vector3 &b,
                                uint16_t color,
                                uint16_t categories,
                                uint16_t lifetimeFrames,
                                uint8_t thickness)
        {
            addLineImpl(a, b, color, categories, lifetimeFrames, thickness, false);
        }

        void DebugDraw::addRay(const Vector3 &origin, const Vector3 &dir,
                               float length,
                               uint16_t color,
                               uint16_t categories,
                               uint16_t lifetimeFrames,
                               uint8_t thickness)
        {
            if (length <= 0.0f)
                return;
            Vector3 d = dir;
            float lenSq = d.lengthSquared();
            if (lenSq <= 1e-8f)
                return;
            float invLen = FastMath::fastInvSqrt(lenSq);
            d *= invLen * length;
            addLine(origin, origin + d, color, categories, lifetimeFrames, thickness);
        }

        void DebugDraw::addAABB(const AABB &box,
                                uint16_t color,
                                uint16_t categories,
                                uint16_t lifetimeFrames,
                                uint8_t thickness)
        {
            if (!isCategoryEnabled(categories))
                return;

            const Vector3 &mn = box.min;
            const Vector3 &mx = box.max;

            Vector3 c000(mn.x, mn.y, mn.z);
            Vector3 c001(mn.x, mn.y, mx.z);
            Vector3 c010(mn.x, mx.y, mn.z);
            Vector3 c011(mn.x, mx.y, mx.z);
            Vector3 c100(mx.x, mn.y, mn.z);
            Vector3 c101(mx.x, mn.y, mx.z);
            Vector3 c110(mx.x, mx.y, mn.z);
            Vector3 c111(mx.x, mx.y, mx.z);

            addLineImpl(c000, c100, color, categories, lifetimeFrames, thickness, true);
            addLineImpl(c100, c110, color, categories, lifetimeFrames, thickness, true);
            addLineImpl(c110, c010, color, categories, lifetimeFrames, thickness, true);
            addLineImpl(c010, c000, color, categories, lifetimeFrames, thickness, true);

            addLineImpl(c001, c101, color, categories, lifetimeFrames, thickness, true);
            addLineImpl(c101, c111, color, categories, lifetimeFrames, thickness, true);
            addLineImpl(c111, c011, color, categories, lifetimeFrames, thickness, true);
            addLineImpl(c011, c001, color, categories, lifetimeFrames, thickness, true);

            addLineImpl(c000, c001, color, categories, lifetimeFrames, thickness, true);
            addLineImpl(c100, c101, color, categories, lifetimeFrames, thickness, true);
            addLineImpl(c110, c111, color, categories, lifetimeFrames, thickness, true);
            addLineImpl(c010, c011, color, categories, lifetimeFrames, thickness, true);
        }

        void DebugDraw::addSphere(const Vector3 &center, float radius,
                                  uint16_t color,
                                  uint16_t categories,
                                  uint16_t lifetimeFrames,
                                  uint8_t thickness)
        {
            if (!isCategoryEnabled(categories))
                return;
            if (radius <= 0.0f)
                return;

            static constexpr int SEGMENTS = 16;
            const float step = 2.0f * 3.14159265f / SEGMENTS;

            Vector3 prev1(center.x + radius, center.y, center.z);
            Vector3 prev2(center.x + radius, center.y, center.z);
            Vector3 prev3(center.x, center.y + radius, center.z);

            for (int i = 1; i <= SEGMENTS; ++i)
            {
                float a = step * i;
                float ca = cosf(a);
                float sa = sinf(a);

                Vector3 p1(center.x + radius * ca, center.y, center.z + radius * sa);
                Vector3 p2(center.x + radius * ca, center.y + radius * sa, center.z);
                Vector3 p3(center.x, center.y + radius * ca, center.z + radius * sa);

                addLineImpl(prev1, p1, color, categories, lifetimeFrames, thickness, true);
                addLineImpl(prev2, p2, color, categories, lifetimeFrames, thickness, true);
                addLineImpl(prev3, p3, color, categories, lifetimeFrames, thickness, true);

                prev1 = p1;
                prev2 = p2;
                prev3 = p3;
            }
        }

        void DebugDraw::addAxes(const Vector3 &origin, float size,
                                uint16_t categories,
                                uint16_t lifetimeFrames,
                                uint8_t thickness)
        {
            if (size <= 0.0f)
                return;

            if (!isCategoryEnabled(categories))
                return;

            addLineImpl(origin, origin + Vector3(size, 0.0f, 0.0f), Color::RED, categories, lifetimeFrames, thickness, true);
            addLineImpl(origin, origin + Vector3(0.0f, size, 0.0f), Color::GREEN, categories, lifetimeFrames, thickness, true);
            addLineImpl(origin, origin + Vector3(0.0f, 0.0f, size), Color::BLUE, categories, lifetimeFrames, thickness, true);
        }

        static void drawLine2D(uint16_t *fb,
                               const Viewport &viewport,
                               int x0, int y0,
                               int x1, int y1,
                               uint16_t color,
                               uint8_t thickness)
        {
            int x = x0;
            int y = y0;
            int dx = (x1 > x0) ? (x1 - x0) : (x0 - x1);
            int sx = (x0 < x1) ? 1 : -1;
            int dy = (y1 > y0) ? (y1 - y0) : (y0 - y1);
            int sy = (y0 < y1) ? 1 : -1;
            int err = dx - dy;

            const int vpX = viewport.x;
            const int vpY = viewport.y;
            const int vpW = static_cast<int>(viewport.width);
            const int vpH = static_cast<int>(viewport.height);

            int half = thickness > 0 ? (thickness - 1) / 2 : 0;

            if (half == 0)
            {
                while (true)
                {
                    int localX = x - vpX;
                    int localY = y - vpY;
                    if (localX >= 0 && localX < vpW && localY >= 0 && localY < vpH)
                    {
                        fb[localY * vpW + localX] = color;
                    }

                    if (x == x1 && y == y1)
                        break;

                    int e2 = err * 2;
                    if (e2 > -dy)
                    {
                        err -= dy;
                        x += sx;
                    }
                    if (e2 < dx)
                    {
                        err += dx;
                        y += sy;
                    }
                }
            }
            else
            {
                while (true)
                {
                    int localX = x - vpX;
                    int localY = y - vpY;
                    if (localX >= 0 && localX < vpW && localY >= 0 && localY < vpH)
                    {
                        for (int o = -half; o <= half; ++o)
                        {
                            int yy = localY + o;
                            if (yy < 0 || yy >= vpH)
                                continue;
                            fb[yy * vpW + localX] = color;
                        }
                    }

                    if (x == x1 && y == y1)
                        break;

                    int e2 = err * 2;
                    if (e2 > -dy)
                    {
                        err -= dy;
                        x += sx;
                    }
                    if (e2 < dx)
                    {
                        err += dx;
                        y += sy;
                    }
                }
            }
        }

        static bool projectToScreen(Renderer &renderer,
                                    const Vector3 &world,
                                    const Viewport &viewport,
                                    int &sx, int &sy)
        {
            Vector3 p = renderer.project(world);
            if (renderer.getCamera().projectionType == PERSPECTIVE && p.z <= 0.0f)
                return false;

            sx = static_cast<int>(p.x + 0.5f);
            sy = static_cast<int>(p.y + 0.5f);

            if (sx < viewport.x - 1 || sx >= viewport.x + viewport.width + 1 ||
                sy < viewport.y - 1 || sy >= viewport.y + viewport.height + 1)
            {
                return false;
            }
            return true;
        }

        void DebugDraw::render(Renderer &renderer)
        {
            if (!hasPrimitives())
                return;

            uint16_t *fb = renderer.getFrameBuffer();
            if (!fb)
                return;

            const Viewport &viewport = renderer.getViewport();
            if (viewport.width == 0 || viewport.height == 0)
                return;

            for (uint16_t i = 0; i < g_lineCount; ++i)
            {
                const DebugLine &ln = g_lines[i];
                if (!isCategoryEnabled(ln.categories))
                    continue;

                int x0, y0, x1, y1;
                bool ok0 = projectToScreen(renderer, ln.a, viewport, x0, y0);
                bool ok1 = projectToScreen(renderer, ln.b, viewport, x1, y1);

                if (!ok0 && !ok1)
                    continue;

                uint8_t thickness = ln.thickness == 0 ? 1 : ln.thickness;
                drawLine2D(fb, viewport, x0, y0, x1, y1, ln.color, thickness);
            }
        }

    }
}

#endif
