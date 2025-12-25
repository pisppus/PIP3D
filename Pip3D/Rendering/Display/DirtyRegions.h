#ifndef DIRTYREGIONS_H
#define DIRTYREGIONS_H

#include <stdint.h>
#include "../../Core/Core.h"
#include "FrameBuffer.h"

namespace pip3D
{
    class MeshInstance;

    static constexpr int MAX_WORLD_DIRTY_INSTANCES = 32;

    struct WorldInstanceDirtySlot
    {
        MeshInstance *instance;
        int16_t curMinX;
        int16_t curMinY;
        int16_t curMaxX;
        int16_t curMaxY;
        int16_t lastMinX;
        int16_t lastMinY;
        int16_t lastMaxX;
        int16_t lastMaxY;
        bool hasCurrent;
        bool hasLast;
    };

    class DirtyRegionHelper
    {
    public:
        static void addDirtyRect(MeshInstance *instance,
                                 int16_t x, int16_t y, int16_t w, int16_t h,
                                 const Viewport &viewport,
                                 WorldInstanceDirtySlot worldInstanceDirty[MAX_WORLD_DIRTY_INSTANCES],
                                 int16_t &worldDirtyMinX,
                                 int16_t &worldDirtyMinY,
                                 int16_t &worldDirtyMaxX,
                                 int16_t &worldDirtyMaxY,
                                 bool &hasWorldDirtyRegion)
        {
            int16_t x0 = x;
            int16_t y0 = y;
            int16_t x1 = x + w;
            int16_t y1 = y + h;

            if (x1 <= 0 || y1 <= 0 || x0 >= viewport.width || y0 >= viewport.height)
                return;

            if (x0 < 0)
                x0 = 0;
            if (y0 < 0)
                y0 = 0;
            if (x1 > viewport.width)
                x1 = viewport.width;
            if (y1 > viewport.height)
                y1 = viewport.height;

            if (instance == nullptr)
            {
                if (!hasWorldDirtyRegion)
                {
                    worldDirtyMinX = x0;
                    worldDirtyMinY = y0;
                    worldDirtyMaxX = x1;
                    worldDirtyMaxY = y1;
                    hasWorldDirtyRegion = true;
                }
                else
                {
                    if (x0 < worldDirtyMinX)
                        worldDirtyMinX = x0;
                    if (y0 < worldDirtyMinY)
                        worldDirtyMinY = y0;
                    if (x1 > worldDirtyMaxX)
                        worldDirtyMaxX = x1;
                    if (y1 > worldDirtyMaxY)
                        worldDirtyMaxY = y1;
                }
                return;
            }

            int freeIndex = -1;
            int slotIndex = -1;
            for (int i = 0; i < MAX_WORLD_DIRTY_INSTANCES; ++i)
            {
                if (worldInstanceDirty[i].instance == instance)
                {
                    slotIndex = i;
                    break;
                }
                if (freeIndex < 0 && worldInstanceDirty[i].instance == nullptr &&
                    !worldInstanceDirty[i].hasCurrent && !worldInstanceDirty[i].hasLast)
                {
                    freeIndex = i;
                }
            }

            if (slotIndex < 0)
            {
                if (freeIndex >= 0)
                {
                    slotIndex = freeIndex;
                    worldInstanceDirty[slotIndex].instance = instance;
                    worldInstanceDirty[slotIndex].hasCurrent = false;
                    worldInstanceDirty[slotIndex].hasLast = false;
                }
                else
                {
                    if (!hasWorldDirtyRegion)
                    {
                        worldDirtyMinX = x0;
                        worldDirtyMinY = y0;
                        worldDirtyMaxX = x1;
                        worldDirtyMaxY = y1;
                        hasWorldDirtyRegion = true;
                    }
                    else
                    {
                        if (x0 < worldDirtyMinX)
                            worldDirtyMinX = x0;
                        if (y0 < worldDirtyMinY)
                            worldDirtyMinY = y0;
                        if (x1 > worldDirtyMaxX)
                            worldDirtyMaxX = x1;
                        if (y1 > worldDirtyMaxY)
                            worldDirtyMaxY = y1;
                    }
                    return;
                }
            }

            WorldInstanceDirtySlot &slot = worldInstanceDirty[slotIndex];
            if (!slot.hasCurrent)
            {
                slot.curMinX = x0;
                slot.curMinY = y0;
                slot.curMaxX = x1;
                slot.curMaxY = y1;
                slot.hasCurrent = true;
            }
            else
            {
                if (x0 < slot.curMinX)
                    slot.curMinX = x0;
                if (y0 < slot.curMinY)
                    slot.curMinY = y0;
                if (x1 > slot.curMaxX)
                    slot.curMaxX = x1;
                if (y1 > slot.curMaxY)
                    slot.curMaxY = y1;
            }
        }

        static void addHudDirtyRect(int16_t x, int16_t y, int16_t w, int16_t h,
                                    const Viewport &viewport,
                                    int16_t &hudDirtyMinX,
                                    int16_t &hudDirtyMinY,
                                    int16_t &hudDirtyMaxX,
                                    int16_t &hudDirtyMaxY,
                                    bool &hasHudDirtyRegion)
        {
            int16_t x0 = x;
            int16_t y0 = y;
            int16_t x1 = x + w;
            int16_t y1 = y + h;

            if (x1 <= 0 || y1 <= 0 || x0 >= viewport.width || y0 >= viewport.height)
                return;

            if (x0 < 0)
                x0 = 0;
            if (y0 < 0)
                y0 = 0;
            if (x1 > viewport.width)
                x1 = viewport.width;
            if (y1 > viewport.height)
                y1 = viewport.height;

            if (!hasHudDirtyRegion)
            {
                hudDirtyMinX = x0;
                hudDirtyMinY = y0;
                hudDirtyMaxX = x1;
                hudDirtyMaxY = y1;
                hasHudDirtyRegion = true;
            }
            else
            {
                if (x0 < hudDirtyMinX)
                    hudDirtyMinX = x0;
                if (y0 < hudDirtyMinY)
                    hudDirtyMinY = y0;
                if (x1 > hudDirtyMaxX)
                    hudDirtyMaxX = x1;
                if (y1 > hudDirtyMaxY)
                    hudDirtyMaxY = y1;
            }
        }

        static void finalizeFrame(FrameBuffer &framebuffer,
                                  PerformanceCounter &perfCounter,
                                  WorldInstanceDirtySlot worldInstanceDirty[MAX_WORLD_DIRTY_INSTANCES],
                                  int16_t &worldDirtyMinX,
                                  int16_t &worldDirtyMinY,
                                  int16_t &worldDirtyMaxX,
                                  int16_t &worldDirtyMaxY,
                                  int16_t &lastWorldDirtyMinX,
                                  int16_t &lastWorldDirtyMinY,
                                  int16_t &lastWorldDirtyMaxX,
                                  int16_t &lastWorldDirtyMaxY,
                                  bool &hasWorldDirtyRegion,
                                  bool &hasLastWorldDirtyRegion,
                                  int16_t &hudDirtyMinX,
                                  int16_t &hudDirtyMinY,
                                  int16_t &hudDirtyMaxX,
                                  int16_t &hudDirtyMaxY,
                                  bool &hasHudDirtyRegion,
                                  bool cameraChangedThisFrame,
                                  uint32_t statsTrianglesTotal,
                                  uint32_t statsTrianglesBackfaceCulled,
                                  uint32_t statsInstancesTotal,
                                  uint32_t statsInstancesFrustumCulled,
                                  uint32_t statsInstancesOcclusionCulled,
                                  bool forceFullFrame,
                                  bool showDirtyOverlay)
        {
            static uint32_t debugFrameCounter = 0;
            debugFrameCounter++;

            const DisplayConfig &cfg = framebuffer.getConfig();

            auto flushFullFrame = [&]()
            {
                framebuffer.endFrame();
                perfCounter.endFrame();

                if ((debugFrameCounter & 31u) == 0u)
                {
                    LOGD(::pip3D::Debug::LOG_MODULE_RENDER,
                         "Stats: tri=%lu bf=%lu inst=%lu fr=%lu occ=%lu",
                         (unsigned long)statsTrianglesTotal,
                         (unsigned long)statsTrianglesBackfaceCulled,
                         (unsigned long)statsInstancesTotal,
                         (unsigned long)statsInstancesFrustumCulled,
                         (unsigned long)statsInstancesOcclusionCulled);
                }

                hasWorldDirtyRegion = false;
                hasLastWorldDirtyRegion = false;
                hasHudDirtyRegion = false;
                for (int i = 0; i < MAX_WORLD_DIRTY_INSTANCES; ++i)
                {
                    worldInstanceDirty[i].instance = nullptr;
                    worldInstanceDirty[i].hasCurrent = false;
                    worldInstanceDirty[i].hasLast = false;
                }
            };

            if (forceFullFrame)
            {
                flushFullFrame();
                return;
            }

            bool anyWorldDirty = hasWorldDirtyRegion || hasLastWorldDirtyRegion;
            for (int i = 0; i < MAX_WORLD_DIRTY_INSTANCES && !anyWorldDirty; ++i)
            {
                if (worldInstanceDirty[i].hasCurrent || worldInstanceDirty[i].hasLast)
                {
                    anyWorldDirty = true;
                }
            }

            if (cameraChangedThisFrame || (!anyWorldDirty && !hasHudDirtyRegion))
            {
                flushFullFrame();
                return;
            }

            struct Rect
            {
                int16_t x0;
                int16_t y0;
                int16_t x1;
                int16_t y1;
            };

            Rect worldRects[MAX_WORLD_DIRTY_INSTANCES + 1];
            int worldRectCount = 0;

            for (int i = 0; i < MAX_WORLD_DIRTY_INSTANCES; ++i)
            {
                WorldInstanceDirtySlot &slot = worldInstanceDirty[i];
                if (!slot.hasCurrent && !slot.hasLast)
                    continue;

                Rect r;

                if (slot.hasCurrent && slot.hasLast)
                {
                    r.x0 = (slot.curMinX < slot.lastMinX) ? slot.curMinX : slot.lastMinX;
                    r.y0 = (slot.curMinY < slot.lastMinY) ? slot.curMinY : slot.lastMinY;
                    r.x1 = (slot.curMaxX > slot.lastMaxX) ? slot.curMaxX : slot.lastMaxX;
                    r.y1 = (slot.curMaxY > slot.lastMaxY) ? slot.curMaxY : slot.lastMaxY;
                }
                else if (slot.hasCurrent)
                {
                    r.x0 = slot.curMinX;
                    r.y0 = slot.curMinY;
                    r.x1 = slot.curMaxX;
                    r.y1 = slot.curMaxY;
                }
                else
                {
                    r.x0 = slot.lastMinX;
                    r.y0 = slot.lastMinY;
                    r.x1 = slot.lastMaxX;
                    r.y1 = slot.lastMaxY;
                }

                int16_t rw = static_cast<int16_t>(r.x1 - r.x0);
                int16_t rh = static_cast<int16_t>(r.y1 - r.y0);
                if (rw > 0 && rh > 0)
                {
                    if (worldRectCount < static_cast<int>(sizeof(worldRects) / sizeof(worldRects[0])))
                    {
                        worldRects[worldRectCount++] = r;
                    }
                }

                if (slot.hasCurrent)
                {
                    slot.lastMinX = slot.curMinX;
                    slot.lastMinY = slot.curMinY;
                    slot.lastMaxX = slot.curMaxX;
                    slot.lastMaxY = slot.curMaxY;
                    slot.hasLast = true;
                }
                else
                {
                    slot.instance = nullptr;
                    slot.hasLast = false;
                }
                slot.hasCurrent = false;
            }

            if (hasWorldDirtyRegion || hasLastWorldDirtyRegion)
            {
                Rect r;
                if (hasWorldDirtyRegion && hasLastWorldDirtyRegion)
                {
                    r.x0 = (worldDirtyMinX < lastWorldDirtyMinX) ? worldDirtyMinX : lastWorldDirtyMinX;
                    r.y0 = (worldDirtyMinY < lastWorldDirtyMinY) ? worldDirtyMinY : lastWorldDirtyMinY;
                    r.x1 = (worldDirtyMaxX > lastWorldDirtyMaxX) ? worldDirtyMaxX : lastWorldDirtyMaxX;
                    r.y1 = (worldDirtyMaxY > lastWorldDirtyMaxY) ? worldDirtyMaxY : lastWorldDirtyMaxY;
                }
                else if (hasWorldDirtyRegion)
                {
                    r.x0 = worldDirtyMinX;
                    r.y0 = worldDirtyMinY;
                    r.x1 = worldDirtyMaxX;
                    r.y1 = worldDirtyMaxY;
                }
                else
                {
                    r.x0 = lastWorldDirtyMinX;
                    r.y0 = lastWorldDirtyMinY;
                    r.x1 = lastWorldDirtyMaxX;
                    r.y1 = lastWorldDirtyMaxY;
                }

                int16_t rw = static_cast<int16_t>(r.x1 - r.x0);
                int16_t rh = static_cast<int16_t>(r.y1 - r.y0);
                if (rw > 0 && rh > 0)
                {
                    if (worldRectCount < static_cast<int>(sizeof(worldRects) / sizeof(worldRects[0])))
                    {
                        worldRects[worldRectCount++] = r;
                    }
                }

                if (hasWorldDirtyRegion)
                {
                    lastWorldDirtyMinX = worldDirtyMinX;
                    lastWorldDirtyMinY = worldDirtyMinY;
                    lastWorldDirtyMaxX = worldDirtyMaxX;
                    lastWorldDirtyMaxY = worldDirtyMaxY;
                    hasLastWorldDirtyRegion = true;
                }
                else
                {
                    hasLastWorldDirtyRegion = false;
                }
                hasWorldDirtyRegion = false;
            }

            bool hasWorld = worldRectCount > 0;
            bool hasHud = hasHudDirtyRegion;

            int16_t hx0 = 0, hy0 = 0, hx1 = 0, hy1 = 0;
            int16_t hw = 0, hh = 0;
            int32_t hudArea = 0;

            if (hasHud)
            {
                hx0 = hudDirtyMinX;
                hy0 = hudDirtyMinY;
                hx1 = hudDirtyMaxX;
                hy1 = hudDirtyMaxY;

                hw = static_cast<int16_t>(hx1 - hx0);
                hh = static_cast<int16_t>(hy1 - hy0);

                if (hw <= 0 || hh <= 0)
                {
                    hasHud = false;
                }
                else
                {
                    hudArea = static_cast<int32_t>(hw) * static_cast<int32_t>(hh);
                }
            }

            if (!hasWorld && !hasHud)
            {
                flushFullFrame();
                return;
            }

            Rect mergedRects[MAX_WORLD_DIRTY_INSTANCES + 1];
            int mergedCount = worldRectCount;
            for (int i = 0; i < worldRectCount; ++i)
            {
                mergedRects[i] = worldRects[i];
            }

            bool mergedSomething;
            do
            {
                mergedSomething = false;
                for (int i = 0; i < mergedCount && !mergedSomething; ++i)
                {
                    for (int j = i + 1; j < mergedCount; ++j)
                    {
                        Rect &a = mergedRects[i];
                        Rect &b = mergedRects[j];

                        if (!(a.x1 < b.x0 || b.x1 < a.x0 || a.y1 < b.y0 || b.y1 < a.y0))
                        {
                            if (b.x0 < a.x0)
                                a.x0 = b.x0;
                            if (b.y0 < a.y0)
                                a.y0 = b.y0;
                            if (b.x1 > a.x1)
                                a.x1 = b.x1;
                            if (b.y1 > a.y1)
                                a.y1 = b.y1;

                            for (int k = j + 1; k < mergedCount; ++k)
                            {
                                mergedRects[k - 1] = mergedRects[k];
                            }
                            mergedCount--;
                            mergedSomething = true;
                            break;
                        }
                    }
                }
            } while (mergedSomething);

            int32_t worldArea = 0;
            for (int i = 0; i < mergedCount; ++i)
            {
                int16_t rw = static_cast<int16_t>(mergedRects[i].x1 - mergedRects[i].x0);
                int16_t rh = static_cast<int16_t>(mergedRects[i].y1 - mergedRects[i].y0);
                if (rw > 0 && rh > 0)
                {
                    worldArea += static_cast<int32_t>(rw) * static_cast<int32_t>(rh);
                }
            }

            int32_t fullArea = static_cast<int32_t>(cfg.width) * static_cast<int32_t>(cfg.height);
            int32_t combinedArea = worldArea + hudArea;

            if (combinedArea * 100 >= fullArea * 70)
            {
                flushFullFrame();
                return;
            }

            if (showDirtyOverlay)
            {
                uint16_t *fb = framebuffer.getBuffer();
                if (fb)
                {
                    uint16_t overlayColor = Color::fromRGB888(255, 255, 0).rgb565;

                    auto drawRectBorder = [&](int16_t x0, int16_t y0, int16_t x1, int16_t y1)
                    {
                        if (x1 <= x0 || y1 <= y0)
                            return;

                        int16_t w = cfg.width;
                        int16_t h = cfg.height;

                        if (x0 < 0)
                            x0 = 0;
                        if (y0 < 0)
                            y0 = 0;
                        if (x1 > w)
                            x1 = w;
                        if (y1 > h)
                            y1 = h;
                        if (x1 <= x0 || y1 <= y0)
                            return;

                        int16_t top = y0;
                        int16_t bottom = y1 - 1;
                        for (int16_t x = x0; x < x1; ++x)
                        {
                            fb[top * w + x] = overlayColor;
                            fb[bottom * w + x] = overlayColor;
                        }

                        int16_t left = x0;
                        int16_t right = x1 - 1;
                        for (int16_t y = y0; y < y1; ++y)
                        {
                            fb[y * w + left] = overlayColor;
                            fb[y * w + right] = overlayColor;
                        }
                    };

                    for (int i = 0; i < mergedCount; ++i)
                    {
                        int16_t x0 = static_cast<int16_t>(mergedRects[i].x0 + 1);
                        int16_t y0 = static_cast<int16_t>(mergedRects[i].y0 + 1);
                        int16_t x1 = static_cast<int16_t>(mergedRects[i].x1 - 1);
                        int16_t y1 = static_cast<int16_t>(mergedRects[i].y1 - 1);
                        drawRectBorder(x0, y0, x1, y1);
                    }

                    if (hasHud)
                    {
                        int16_t x0 = static_cast<int16_t>(hx0 + 1);
                        int16_t y0 = static_cast<int16_t>(hy0 + 1);
                        int16_t x1 = static_cast<int16_t>(hx1 - 1);
                        int16_t y1 = static_cast<int16_t>(hy1 - 1);
                        drawRectBorder(x0, y0, x1, y1);
                    }
                }
            }

            for (int i = 0; i < mergedCount; ++i)
            {
                int16_t rw = static_cast<int16_t>(mergedRects[i].x1 - mergedRects[i].x0);
                int16_t rh = static_cast<int16_t>(mergedRects[i].y1 - mergedRects[i].y0);
                if (rw <= 0 || rh <= 0)
                    continue;

                framebuffer.endFrameRegion(mergedRects[i].x0, mergedRects[i].y0, rw, rh);
            }

            if (hasHud)
            {
                framebuffer.endFrameRegion(hx0, hy0, hw, hh);
            }

            perfCounter.endFrame();

            if ((debugFrameCounter & 31u) == 0u)
            {
                LOGD(::pip3D::Debug::LOG_MODULE_RENDER,
                     "Stats: tri=%lu bf=%lu inst=%lu fr=%lu occ=%lu",
                     (unsigned long)statsTrianglesTotal,
                     (unsigned long)statsTrianglesBackfaceCulled,
                     (unsigned long)statsInstancesTotal,
                     (unsigned long)statsInstancesFrustumCulled,
                     (unsigned long)statsInstancesOcclusionCulled);
            }
        }
    };
}

#endif
