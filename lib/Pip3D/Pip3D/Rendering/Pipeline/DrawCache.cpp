#include "Rendering/Pipeline/DrawCache.hpp"

#include "Math/Algebra.hpp"

namespace pip3D
{
    DrawCache::~DrawCache() noexcept
    {
        safeFree(storage_);

        worldNormals_ = nullptr;
        screenVerts_ = nullptr;
    }

    bool DrawCache::ensureCapacity(uint16_t required, bool withNormals) noexcept
    {
        if (required == 0)
            return false;

        const bool currentHasNormals = (worldNormals_ != nullptr);

        if (likely(capacity_ >= required && storage_))
        {
            if (!withNormals || currentHasNormals)
                return true;
        }

        constexpr size_t kAlign = 16;
        const size_t vertsBytes = static_cast<size_t>(required) * sizeof(Vector3);
        const size_t alignedVertsBytes = (vertsBytes + kAlign - 1) & ~(kAlign - 1);
        const size_t totalBytes = withNormals ? 3 * alignedVertsBytes : 2 * alignedVertsBytes;

        Vector3 *block = static_cast<Vector3 *>(
            MemUtils::allocData(totalBytes, static_cast<uint8_t>(kAlign)));

        if (unlikely(!block))
        {
            safeFree(storage_);
            worldNormals_ = nullptr;
            screenVerts_ = nullptr;
            capacity_ = 0;
            cachedTransformVersion_ = 0;
            screenVertsFrameStamp_ = 0;
            shadowGen_ = 0;
            shadowVertsValid_ = false;
            return false;
        }

        safeFree(storage_);

        uint8_t *base = reinterpret_cast<uint8_t *>(block);
        storage_ = block;
        if (withNormals)
        {
            worldNormals_ = reinterpret_cast<Vector3 *>(base + alignedVertsBytes);
            screenVerts_ = reinterpret_cast<Vector3 *>(base + 2 * alignedVertsBytes);
        }
        else
        {
            worldNormals_ = nullptr;
            screenVerts_ = reinterpret_cast<Vector3 *>(base + alignedVertsBytes);
        }
        capacity_ = required;
        cachedTransformVersion_ = 0;
        screenVertsFrameStamp_ = 0;
        shadowVertsValid_ = false;
        shadowGen_ = 0;
        return true;
    }

    Vector3 *DrawCache::acquireShadowVerts(uint32_t gen, uint16_t count,
                                           bool &needsCompute) noexcept
    {
        if (!storage_ || capacity_ < count)
        {
            if (!ensureCapacity(count))
            {
                needsCompute = true;
                return nullptr;
            }
        }

        if (shadowVertsValid_ && shadowGen_ == gen)
        {
            needsCompute = false;
            return screenVerts_;
        }

        needsCompute = true;
        return screenVerts_;
    }
}