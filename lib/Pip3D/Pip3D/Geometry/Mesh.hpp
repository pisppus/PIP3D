#pragma once

#include "Math/Algebra.hpp"
#include "Math/Quant.hpp"
#include "Core/Memory.hpp"
#include "Core/Color.hpp"

namespace pip3D
{
    struct Texture;

    struct alignas(16) Vertex
    {
        int16_t px, py, pz;
        PackedNormal normal;
        float tu, tv;

        constexpr Vertex() : px(0), py(0), pz(0), normal(), tu(0.0f), tv(0.0f) {}
        constexpr Vertex(int16_t x, int16_t y, int16_t z, uint16_t norm,
                         float u = 0.0f, float v = 0.0f)
            : px(x), py(y), pz(z), normal(norm), tu(u), tv(v) {}
    };
    static_assert(sizeof(Vertex) == 16);
    static_assert(alignof(Vertex) == 16);

    struct alignas(2) Face16
    {
        uint16_t v0, v1, v2;
        constexpr Face16() : v0(0), v1(0), v2(0) {}
        constexpr Face16(uint16_t a, uint16_t b, uint16_t c) : v0(a), v1(b), v2(c) {}
    };
    static_assert(sizeof(Face16) == 6);
    using Face = Face16;

    struct alignas(4) Face32
    {
        uint32_t v0, v1, v2;
        constexpr Face32() : v0(0), v1(0), v2(0) {}
        constexpr Face32(uint32_t a, uint32_t b, uint32_t c) : v0(a), v1(b), v2(c) {}
    };
    static_assert(sizeof(Face32) == 12);

    struct alignas(4) MeshChunk
    {
        int16_t minX, minY, minZ;
        int16_t maxX, maxY, maxZ;
        uint32_t vOffset;
        uint32_t faceOffset;
        uint16_t vCount;
        uint16_t faceCount;
        int16_t normX, normY, normZ;
        int16_t coneDot;
    };
    static_assert(sizeof(MeshChunk) == 32, "MeshChunk layout changed — sync with Convert.py!");

    struct SubMesh
    {
        uint16_t faceOffset;
        uint16_t faceCount;
        Color color;
    };
    static_assert(sizeof(SubMesh) == 6, "SubMesh layout changed — sync with Convert.py!");
    static_assert(alignof(SubMesh) == 2, "SubMesh alignment drift");

    struct SubMesh32
    {
        uint32_t faceOffset;
        uint32_t faceCount;
        Color color;
    };
    static_assert(sizeof(SubMesh32) == 12, "SubMesh32 layout changed — sync with Convert.py!");
    static_assert(alignof(SubMesh32) == 4, "SubMesh32 alignment drift");

    class Mesh
    {
    private:
        enum FlagBits : uint8_t
        {
            kFlagCastShadows = 1u << 0,
            kFlagSingleColorLighting = 1u << 1,
            kFlagStaticStorage = 1u << 2,
            kFlagBoundsValid = 1u << 3,
            kFlagIndex32 = 1u << 4,
            kFlagHasSubMeshes = 1u << 5,
            kFlagWantsTexture = 1u << 6,
            kFlagSubMesh32 = 1u << 7,
        };

        static constexpr float kQScaleFactor = 0.5f * (1.0f / 32767.0f);

        PIP3D_COLD void calculateBoundingSphere() const;
        PIP3D_COLD void recomputeHalfExtentsFromVertices() const;

        PIP3D_FORCE_INLINE void initSubMeshes(const SubMesh *sub, uint32_t count) noexcept
        {
            subMeshes_ = sub;
            if (count > 0)
                flags_ |= kFlagHasSubMeshes;
        }

        PIP3D_FORCE_INLINE void initSubMeshes(const SubMesh32 *sub, uint32_t count) noexcept
        {
            subMeshes32_ = sub;
            if (count > 0)
                flags_ |= kFlagHasSubMeshes | kFlagSubMesh32;
        }

        PIP3D_COLD void cleanup()
        {
            if (!(flags_ & kFlagStaticStorage) && vertices_)
                MemUtils::freeData(vertices_);
            vertices_ = nullptr;
            faces_ = nullptr;
            chunks_ = nullptr;
            subMeshes_ = nullptr;
        }

    protected:
        Vertex *PIP3D_RESTRICT vertices_;
        union
        {
            Face16 *faces_;
            Face16 *faces16_;
            Face32 *faces32_;
            void *facesRaw_;
        };
        const MeshChunk *chunks_;
        union
        {
            const SubMesh *subMeshes_;
            const SubMesh32 *subMeshes32_;
        };
        uint32_t chunkCount_;
        uint32_t subMeshCount_;
        float qScale_;
        uint32_t vertexCount_;
        uint32_t faceCount_;
        mutable uint8_t flags_;
        mutable Vector3 boundsCenter_;
        mutable float boundsRadius_;
        mutable Vector3 boundsHalfExtents_;
        const Texture *meshTexture_;
        uint32_t maxVertices_;
        uint32_t maxFaces_;
        void (*deleter_)(Mesh *);

        ~Mesh() { cleanup(); }

    public:
        explicit Mesh(uint32_t maxVerts, uint32_t maxFaces)
            : vertices_(nullptr), faces_(nullptr), chunks_(nullptr),
              subMeshes_(nullptr), chunkCount_(0), subMeshCount_(0),
              qScale_(1.0f),
              vertexCount_(0), faceCount_(0),
              flags_(kFlagCastShadows),
              boundsCenter_(0.0f, 0.0f, 0.0f),
              boundsRadius_(0.0f),
              boundsHalfExtents_(0.0f, 0.0f, 0.0f),
              meshTexture_(nullptr),
              maxVertices_(maxVerts), maxFaces_(maxFaces),
              deleter_(&defaultDeleter)
        {
            const size_t vBytes = static_cast<size_t>(maxVerts) * sizeof(Vertex);
            const size_t fBytes = static_cast<size_t>(maxFaces) * sizeof(Face16);
            Vertex *buf = static_cast<Vertex *>(MemUtils::allocData(vBytes + fBytes, 16));
            vertices_ = buf;
            faces_ = (buf != nullptr) ? reinterpret_cast<Face16 *>(buf + maxVerts) : nullptr;
        }

        template <typename SubMeshT = SubMesh>
        Mesh(const Vertex *extVerts, uint32_t vertCount,
             const Face16 *extFaces, uint32_t faceCountIn,
             bool staticStorage = true,
             const MeshChunk *extChunks = nullptr, uint32_t chunkCountIn = 0,
             const SubMeshT *extSubMeshes = nullptr, uint32_t subMeshCountIn = 0)
            : vertices_(const_cast<Vertex *>(extVerts)),
              faces_(const_cast<Face16 *>(extFaces)),
              chunks_(extChunks),
              subMeshes_(nullptr),
              chunkCount_(chunkCountIn),
              subMeshCount_(subMeshCountIn),
              qScale_(1.0f),
              vertexCount_(vertCount), faceCount_(faceCountIn),
              flags_(static_cast<uint8_t>(kFlagCastShadows |
                                          (staticStorage ? static_cast<uint8_t>(kFlagStaticStorage) : 0u))),
              boundsCenter_(0.0f, 0.0f, 0.0f),
              boundsRadius_(0.0f),
              boundsHalfExtents_(0.0f, 0.0f, 0.0f),
              meshTexture_(nullptr),
              maxVertices_(vertCount), maxFaces_(faceCountIn),
              deleter_(&defaultDeleter)
        {
            initSubMeshes(extSubMeshes, subMeshCountIn);
        }

        template <typename SubMeshT = SubMesh>
        Mesh(const Vertex *extVerts, uint32_t vertCount,
             const Face32 *extFaces, uint32_t faceCountIn,
             bool staticStorage = true,
             const MeshChunk *extChunks = nullptr, uint32_t chunkCountIn = 0,
             const SubMeshT *extSubMeshes = nullptr, uint32_t subMeshCountIn = 0)
            : vertices_(const_cast<Vertex *>(extVerts)),
              faces32_(const_cast<Face32 *>(extFaces)),
              chunks_(extChunks),
              subMeshes_(nullptr),
              chunkCount_(chunkCountIn),
              subMeshCount_(subMeshCountIn),
              qScale_(1.0f),
              vertexCount_(vertCount), faceCount_(faceCountIn),
              flags_(static_cast<uint8_t>(kFlagCastShadows | kFlagIndex32 |
                                          (staticStorage ? static_cast<uint8_t>(kFlagStaticStorage) : 0u))),
              boundsCenter_(0.0f, 0.0f, 0.0f),
              boundsRadius_(0.0f),
              boundsHalfExtents_(0.0f, 0.0f, 0.0f),
              meshTexture_(nullptr),
              maxVertices_(vertCount), maxFaces_(faceCountIn),
              deleter_(&defaultDeleter)
        {
            initSubMeshes(extSubMeshes, subMeshCountIn);
        }

        Mesh(const Mesh &) = delete;
        Mesh &operator=(const Mesh &) = delete;

        PIP3D_COLD static void defaultDeleter(Mesh *p) { delete p; }

        template <typename T>
        PIP3D_COLD static void typedDeleter(Mesh *p) { delete static_cast<T *>(p); }

        template <typename T>
        PIP3D_COLD void bindDeleter() { deleter_ = &typedDeleter<T>; }

        PIP3D_COLD static void destroy(Mesh *m)
        {
            if (m)
                m->deleter_(m);
        }

        PIP3D_COLD void autoScale(float size)
        {
            qScale_ = size * kQScaleFactor;
            flags_ &= ~kFlagBoundsValid;
        }

        PIP3D_COLD void finalizeGeometry(uint32_t vCount, uint32_t fCount,
                                         const Vector3 &boundCenter, float boundRadius)
        {
            vertexCount_ = vCount;
            faceCount_ = fCount;
            boundsCenter_ = boundCenter;
            boundsRadius_ = boundRadius;
            flags_ |= kFlagBoundsValid;
        }

        [[nodiscard]] PIP3D_FORCE_INLINE uint32_t numFaces() const noexcept { return faceCount_; }
        [[nodiscard]] PIP3D_FORCE_INLINE uint32_t numVertices() const noexcept { return vertexCount_; }
        [[nodiscard]] PIP3D_FORCE_INLINE uint32_t numChunks() const noexcept { return chunkCount_; }
        [[nodiscard]] PIP3D_FORCE_INLINE uint32_t numSubMeshes() const noexcept { return subMeshCount_; }

        [[nodiscard]] PIP3D_FORCE_INLINE bool isIndex32() const noexcept { return (flags_ & kFlagIndex32) != 0; }
        [[nodiscard]] PIP3D_FORCE_INLINE const Vertex *vertexData() const noexcept { return vertices_; }
        [[nodiscard]] PIP3D_FORCE_INLINE const Face16 *faceData16() const noexcept { return faces16_; }
        [[nodiscard]] PIP3D_FORCE_INLINE const Face32 *faceData32() const noexcept { return faces32_; }
        [[nodiscard]] PIP3D_FORCE_INLINE const Face16 *faceData() const noexcept { return faces_; }
        [[nodiscard]] PIP3D_FORCE_INLINE const MeshChunk *chunkData() const noexcept { return chunks_; }
        [[nodiscard]] PIP3D_FORCE_INLINE const MeshChunk &getChunk(uint32_t i) const noexcept { return chunks_[i]; }
        [[nodiscard]] PIP3D_FORCE_INLINE bool hasSubMeshes() const noexcept { return (flags_ & kFlagHasSubMeshes) != 0 && subMeshCount_ > 0; }

        [[nodiscard]] PIP3D_FORCE_INLINE uint32_t subMeshFaceEnd(uint32_t i) const noexcept
        {
            if (flags_ & kFlagSubMesh32)
                return subMeshes32_[i].faceOffset + subMeshes32_[i].faceCount;
            return static_cast<uint32_t>(subMeshes_[i].faceOffset) +
                   static_cast<uint32_t>(subMeshes_[i].faceCount);
        }
        [[nodiscard]] PIP3D_FORCE_INLINE Color subMeshColor(uint32_t i) const noexcept
        {
            return (flags_ & kFlagSubMesh32) ? subMeshes32_[i].color : subMeshes_[i].color;
        }
        [[nodiscard]] PIP3D_FORCE_INLINE bool wantsTexture() const noexcept { return (flags_ & kFlagWantsTexture) != 0; }
        PIP3D_FORCE_INLINE void setWantsTexture(bool e) noexcept
        {
            if (e)
                flags_ |= kFlagWantsTexture;
            else
                flags_ &= ~kFlagWantsTexture;
        }
        [[nodiscard]] PIP3D_FORCE_INLINE float getQScale() const noexcept { return qScale_; }

        [[nodiscard]] PIP3D_FORCE_INLINE uint16_t maxChunkVertexCount() const noexcept
        {
            if (chunkCount_ == 0)
                return static_cast<uint16_t>(vertexCount_ > 65535 ? 65535 : vertexCount_);
            uint16_t m = 0;
            for (uint32_t i = 0; i < chunkCount_; ++i)
            {
                const uint16_t c = chunks_[i].vCount;
                if (c > m)
                    m = c;
            }
            return m;
        }

        [[nodiscard]] PIP3D_FORCE_INLINE Vector3 decodePosition(const Vertex &v) const noexcept
        {
            return Vector3(static_cast<float>(v.px) * qScale_,
                           static_cast<float>(v.py) * qScale_,
                           static_cast<float>(v.pz) * qScale_);
        }

        PIP3D_HOT void decodePositions(Vector3 *PIP3D_RESTRICT out, uint32_t count) const
        {
            const Vertex *PIP3D_RESTRICT src = vertices_;
            const float s = qScale_;
            for (uint32_t i = 0; i < count; ++i)
            {
                out[i].x = static_cast<float>(src[i].px) * s;
                out[i].y = static_cast<float>(src[i].py) * s;
                out[i].z = static_cast<float>(src[i].pz) * s;
            }
        }

        PIP3D_FORCE_INLINE void getBounds(Vector3 &outCenter, float &outRadius) const
        {
            if (unlikely(!(flags_ & kFlagBoundsValid)))
                calculateBoundingSphere();
            outCenter = boundsCenter_;
            outRadius = boundsRadius_;
        }

        PIP3D_FORCE_INLINE void getLocalExtents(Vector3 &outHalfExtents) const
        {
            if (unlikely(!(flags_ & kFlagBoundsValid)))
                calculateBoundingSphere();
            if (boundsHalfExtents_.x == 0.0f && boundsHalfExtents_.y == 0.0f && boundsHalfExtents_.z == 0.0f && boundsRadius_ > 1e-4f)
                recomputeHalfExtentsFromVertices();
            outHalfExtents = boundsHalfExtents_;
        }

        [[nodiscard]] PIP3D_FORCE_INLINE const Vector3 &center() const
        {
            if (unlikely(!(flags_ & kFlagBoundsValid)))
                calculateBoundingSphere();
            return boundsCenter_;
        }
        [[nodiscard]] PIP3D_FORCE_INLINE float radius() const
        {
            if (unlikely(!(flags_ & kFlagBoundsValid)))
                calculateBoundingSphere();
            return boundsRadius_;
        }

        [[nodiscard]] PIP3D_FORCE_INLINE bool getCastShadows() const noexcept { return (flags_ & kFlagCastShadows) != 0; }
        PIP3D_FORCE_INLINE void setCastShadows(bool e) noexcept
        {
            if (e)
                flags_ |= kFlagCastShadows;
            else
                flags_ &= ~kFlagCastShadows;
        }

        [[nodiscard]] PIP3D_FORCE_INLINE bool getSingleColorLighting() const noexcept { return (flags_ & kFlagSingleColorLighting) != 0; }
        PIP3D_FORCE_INLINE void setSingleColorLighting(bool e) noexcept
        {
            if (e)
                flags_ |= kFlagSingleColorLighting;
            else
                flags_ &= ~kFlagSingleColorLighting;
        }

        [[nodiscard]] PIP3D_FORCE_INLINE const Texture *getTexture() const noexcept { return meshTexture_; }
        PIP3D_FORCE_INLINE void setTexture(const Texture *t) noexcept { meshTexture_ = t; }
        [[nodiscard]] PIP3D_FORCE_INLINE bool isTextured() const noexcept { return meshTexture_ != nullptr; }
    };

    PIP3D_COLD inline void Mesh::calculateBoundingSphere() const
    {
        if (unlikely(!vertices_ || vertexCount_ == 0))
        {
            boundsCenter_ = Vector3(0.0f, 0.0f, 0.0f);
            boundsRadius_ = 0.0f;
            boundsHalfExtents_ = Vector3(0.0f, 0.0f, 0.0f);
            flags_ |= kFlagBoundsValid;
            return;
        }

        const Vertex *PIP3D_RESTRICT vPtr = vertices_;
        const uint32_t vN = vertexCount_;
        const float scale = qScale_;

        float minX = 1e30f, maxX = -1e30f;
        float minY = 1e30f, maxY = -1e30f;
        float minZ = 1e30f, maxZ = -1e30f;

        uint32_t iMinX = 0, iMaxX = 0;
        uint32_t iMinY = 0, iMaxY = 0;
        uint32_t iMinZ = 0, iMaxZ = 0;
        for (uint32_t i = 0; i < vN; ++i)
        {
            const int16_t x = vPtr[i].px;
            const int16_t y = vPtr[i].py;
            const int16_t z = vPtr[i].pz;
            const float fx = float(x) * scale;
            const float fy = float(y) * scale;
            const float fz = float(z) * scale;

            if (i > 0)
            {
                if (x < vPtr[iMinX].px)
                    iMinX = i;
                if (x > vPtr[iMaxX].px)
                    iMaxX = i;
                if (y < vPtr[iMinY].py)
                    iMinY = i;
                if (y > vPtr[iMaxY].py)
                    iMaxY = i;
                if (z < vPtr[iMinZ].pz)
                    iMinZ = i;
                if (z > vPtr[iMaxZ].pz)
                    iMaxZ = i;
            }

            if (fx < minX)
                minX = fx;
            if (fx > maxX)
                maxX = fx;
            if (fy < minY)
                minY = fy;
            if (fy > maxY)
                maxY = fy;
            if (fz < minZ)
                minZ = fz;
            if (fz > maxZ)
                maxZ = fz;
        }
        boundsHalfExtents_ = Vector3((maxX - minX) * 0.5f, (maxY - minY) * 0.5f, (maxZ - minZ) * 0.5f);

        const Vector3 p1(float(vPtr[iMinX].px) * scale, float(vPtr[iMinY].py) * scale, float(vPtr[iMinZ].pz) * scale);
        const Vector3 p2(float(vPtr[iMaxX].px) * scale, float(vPtr[iMaxY].py) * scale, float(vPtr[iMaxZ].pz) * scale);

        Vector3 center = (p1 + p2) * 0.5f;
        float maxDistSq = 0.0f;
        for (uint32_t i = 0; i < vN; ++i)
        {
            const float dx = float(vPtr[i].px) * scale - center.x;
            const float dy = float(vPtr[i].py) * scale - center.y;
            const float dz = float(vPtr[i].pz) * scale - center.z;
            const float dSq = dx * dx + dy * dy + dz * dz;
            if (dSq > maxDistSq)
                maxDistSq = dSq;
        }

        boundsCenter_ = center;
        boundsRadius_ = sqrtf(maxDistSq);
        flags_ |= kFlagBoundsValid;
    }

    PIP3D_COLD inline void Mesh::recomputeHalfExtentsFromVertices() const
    {
        if (!vertices_ || vertexCount_ == 0)
            return;
        const float scale = qScale_;
        float minX = 1e30f, maxX = -1e30f;
        float minY = 1e30f, maxY = -1e30f;
        float minZ = 1e30f, maxZ = -1e30f;
        for (uint32_t i = 0; i < vertexCount_; ++i)
        {
            const float px = float(vertices_[i].px) * scale;
            const float py = float(vertices_[i].py) * scale;
            const float pz = float(vertices_[i].pz) * scale;
            if (px < minX)
                minX = px;
            if (px > maxX)
                maxX = px;
            if (py < minY)
                minY = py;
            if (py > maxY)
                maxY = py;
            if (pz < minZ)
                minZ = pz;
            if (pz > maxZ)
                maxZ = pz;
        }
        boundsHalfExtents_ = Vector3((maxX - minX) * 0.5f, (maxY - minY) * 0.5f, (maxZ - minZ) * 0.5f);
    }
}