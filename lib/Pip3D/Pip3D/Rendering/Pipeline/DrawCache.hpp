#pragma once

#include "Core/Memory.hpp"
#include "Core/Platform.hpp"

namespace pip3D
{
    class Vector3;

    class DrawCache
    {
    public:
        DrawCache() noexcept = default;
        ~DrawCache() noexcept;

        DrawCache(const DrawCache &) = delete;
        DrawCache &operator=(const DrawCache &) = delete;
        DrawCache(DrawCache &&) = delete;
        DrawCache &operator=(DrawCache &&) = delete;

        PIP3D_HOT bool ensureCapacity(uint16_t required, bool withNormals = false) noexcept;

        enum class ProjState : uint8_t
        {
            Cached = 0,
            NeedsReproject = 1,
            NeedsTransformAndProject = 2
        };

        PIP3D_FORCE_INLINE ProjState beginProjection(uint32_t frameStamp,
                                                     uint32_t instanceVersion) const noexcept
        {
            if (cachedTransformVersion_ != instanceVersion)
                return ProjState::NeedsTransformAndProject;
            if (screenVertsFrameStamp_ != frameStamp)
                return ProjState::NeedsReproject;
            return ProjState::Cached;
        }

        PIP3D_FORCE_INLINE void commitProjection(uint32_t frameStamp,
                                                 uint32_t instanceVersion) noexcept
        {
            cachedTransformVersion_ = instanceVersion;
            screenVertsFrameStamp_ = frameStamp;
        }

        PIP3D_FORCE_INLINE bool worldVertsValid(uint32_t instanceVersion) const noexcept
        {
            return cachedTransformVersion_ == instanceVersion;
        }

        PIP3D_FORCE_INLINE void commitWorldVerts(uint32_t instanceVersion) noexcept
        {
            cachedTransformVersion_ = instanceVersion;
        }

        PIP3D_FORCE_INLINE Vector3 *worldVerts() noexcept { return storage_; }
        PIP3D_FORCE_INLINE const Vector3 *worldVerts() const noexcept { return storage_; }

        PIP3D_FORCE_INLINE Vector3 *worldNormals() noexcept { return worldNormals_; }
        PIP3D_FORCE_INLINE const Vector3 *worldNormals() const noexcept { return worldNormals_; }

        PIP3D_FORCE_INLINE Vector3 *screenVerts() noexcept { return screenVerts_; }
        PIP3D_FORCE_INLINE const Vector3 *screenVerts() const noexcept { return screenVerts_; }

        PIP3D_HOT Vector3 *acquireShadowVerts(uint32_t gen, uint16_t count,
                                              bool &needsCompute) noexcept;

        PIP3D_FORCE_INLINE void commitShadowVerts(uint32_t gen) noexcept
        {
            shadowGen_ = gen;
            shadowVertsValid_ = true;
        }

    private:
        PIP3D_FORCE_INLINE static void safeFree(Vector3 *&ptr) noexcept
        {
            if (ptr)
            {
                MemUtils::freeData(ptr);
                ptr = nullptr;
            }
        }

        Vector3 *storage_ = nullptr;
        Vector3 *worldNormals_ = nullptr;
        Vector3 *screenVerts_ = nullptr;
        uint16_t capacity_ = 0;

        uint32_t screenVertsFrameStamp_ = 0;
        uint32_t cachedTransformVersion_ = 0;

        uint32_t shadowGen_ = 0;
        bool shadowVertsValid_ = false;
    };
}