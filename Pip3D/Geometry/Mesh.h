#ifndef MESH_H
#define MESH_H

#include "../Math/Math.h"
#include "../Core/Core.h"
#include <esp_heap_caps.h>
#include <esp_attr.h>
#include <soc/cpu.h>
#include <string.h>

#define MESH_SIMD_ALIGN __attribute__((aligned(16)))
#define MESH_FORCE_INLINE __attribute__((always_inline)) inline
#define MESH_HOT_PATH __attribute__((hot))
#define MESH_COLD_PATH __attribute__((cold))
#define MESH_PURE __attribute__((pure))

static constexpr float INV_255 = 1.0f / 255.0f;
static constexpr float SCALE_255 = 255.0f;
static constexpr float EPSILON_SQ = 1e-12f;

namespace pip3D
{

    struct PackedNormal
    {
        uint16_t data;

        MESH_FORCE_INLINE PackedNormal() : data(0) {}

        MESH_HOT_PATH MESH_FORCE_INLINE void set(const Vector3 &n)
        {
            float l1norm = fabsf(n.x) + fabsf(n.y) + fabsf(n.z);
            if (likely(l1norm > 1e-6f))
            {
                float inv_norm = 1.0f / l1norm;
                float nx = n.x * inv_norm;
                float ny = n.y * inv_norm;

                if (unlikely(n.z < 0.0f))
                {
                    float tx = nx;
                    nx = (1.0f - fabsf(ny)) * (nx >= 0.0f ? 1.0f : -1.0f);
                    ny = (1.0f - fabsf(tx)) * (ny >= 0.0f ? 1.0f : -1.0f);
                }

                uint32_t px = (uint32_t)((nx * 0.5f + 0.5f) * SCALE_255);
                uint32_t py = (uint32_t)((ny * 0.5f + 0.5f) * SCALE_255);
                data = (px << 8) | py;
            }
        }

        MESH_HOT_PATH MESH_FORCE_INLINE MESH_PURE Vector3 get() const
        {
            uint32_t px = data >> 8;
            uint32_t py = data & 0xFF;

            float nx = px * INV_255 * 2.0f - 1.0f;
            float ny = py * INV_255 * 2.0f - 1.0f;
            float nz = 1.0f - fabsf(nx) - fabsf(ny);

            if (unlikely(nz < 0.0f))
            {
                float tx = nx;
                nx = (1.0f - fabsf(ny)) * (nx >= 0.0f ? 1.0f : -1.0f);
                ny = (1.0f - fabsf(tx)) * (ny >= 0.0f ? 1.0f : -1.0f);
            }

            Vector3 result(nx, ny, nz);
            return result;
        }
    };

    struct Vertex
    {
        int16_t px, py, pz;
        PackedNormal normal;

        MESH_FORCE_INLINE Vertex() : px(0), py(0), pz(0), normal() {}
    };

    struct Face
    {
        uint16_t v0, v1, v2;

        MESH_FORCE_INLINE Face() : v0(0), v1(0), v2(0) {}
        MESH_FORCE_INLINE Face(uint16_t a, uint16_t b, uint16_t c) : v0(a), v1(b), v2(c) {}
    };

    struct MESH_SIMD_ALIGN MeshCache
    {
        Vector3 boundingCenter;
        float boundingRadius;
        Matrix4x4 transform;
        float maxScale;
        uint32_t transformHash;
        bool transformValid;
        bool boundsValid;

        MESH_FORCE_INLINE MeshCache() : boundingRadius(0), maxScale(1), transformHash(0),
                                        transformValid(false), boundsValid(false) {}
    };

    class MESH_SIMD_ALIGN Mesh
    {
    protected:
        Vertex *__restrict vertices;
        Face *__restrict faces;

        uint16_t vertexCount;
        uint16_t faceCount;
        uint16_t maxVertices;
        uint16_t maxFaces;

        MESH_SIMD_ALIGN Vector3 position;
        MESH_SIMD_ALIGN Vector3 rotation;
        MESH_SIMD_ALIGN Vector3 scale;

        Color meshColor;
        bool visible;
        bool castShadows;
        mutable bool transformDirty;

        bool isStaticStorage;

        float qScale;

        mutable MeshCache cache;

    public:
        MESH_HOT_PATH Mesh(uint16_t maxVerts = 64, uint16_t maxFcs = 128, const Color &color = Color::WHITE)
            : vertexCount(0), faceCount(0), maxVertices(maxVerts), maxFaces(maxFcs),
              position(0, 0, 0), rotation(0, 0, 0), scale(1, 1, 1),
              meshColor(color), visible(true), castShadows(true), transformDirty(true),
              isStaticStorage(false), qScale(1.0f)
        {

            const size_t vertexSize = maxVertices * sizeof(Vertex);
            const size_t faceSize = maxFaces * sizeof(Face);

            vertices = (Vertex *)MemUtils::allocData(vertexSize, 16);
            faces = (Face *)MemUtils::allocData(faceSize, 16);

            if (unlikely(!vertices || !faces))
            {
                LOGE(::pip3D::Debug::LOG_MODULE_RESOURCES,
                     "Mesh ctor: failed to allocate geometry buffers (maxVertices=%u, maxFaces=%u, vertexSize=%u, faceSize=%u)",
                     static_cast<unsigned int>(maxVertices),
                     static_cast<unsigned int>(maxFaces),
                     static_cast<unsigned int>(vertexSize),
                     static_cast<unsigned int>(faceSize));
                cleanup();
                return;
            }

            cache.transform.identity();
        }

        MESH_HOT_PATH Mesh(const Vertex *externalVertices,
                           uint16_t vertCount,
                           const Face *externalFaces,
                           uint16_t faceCountIn,
                           const Color &color = Color::WHITE,
                           bool staticStorage = true)
            : vertices(const_cast<Vertex *>(externalVertices)),
              faces(const_cast<Face *>(externalFaces)),
              vertexCount(vertCount), faceCount(faceCountIn),
              maxVertices(vertCount), maxFaces(faceCountIn),
              position(0, 0, 0), rotation(0, 0, 0), scale(1, 1, 1),
              meshColor(color), visible(true), castShadows(true), transformDirty(true),
              isStaticStorage(staticStorage), qScale(1.0f)
        {
            cache.transform.identity();
        }

        MESH_FORCE_INLINE void autoScale(float size)
        {
            if (size <= 0.0f)
            {
                qScale = 1.0f;
                return;
            }
            const float half = size * 0.5f;
            const float denom = 32767.0f;
            qScale = half / denom;
        }

        MESH_FORCE_INLINE int16_t quantizeCoord(float x) const
        {
            if (qScale <= 0.0f)
            {
                return 0;
            }
            float q = x / qScale;
            float clamped = fminf(fmaxf(q, -32768.0f), 32767.0f);
            if (clamped >= 0.0f)
                clamped += 0.5f;
            else
                clamped -= 0.5f;
            return static_cast<int16_t>(clamped);
        }

        MESH_PURE MESH_FORCE_INLINE Vector3 decodePosition(const Vertex &v) const
        {
            return Vector3(
                static_cast<float>(v.px) * qScale,
                static_cast<float>(v.py) * qScale,
                static_cast<float>(v.pz) * qScale);
        }

        MESH_COLD_PATH void cleanup()
        {
            if (vertices && !isStaticStorage)
            {
                MemUtils::freeData(vertices);
                vertices = nullptr;
            }
            if (faces && !isStaticStorage)
            {
                MemUtils::freeData(faces);
                faces = nullptr;
            }
            vertexCount = 0;
            faceCount = 0;
            maxVertices = 0;
            maxFaces = 0;
            cache.boundsValid = false;
        }

        MESH_COLD_PATH virtual ~Mesh()
        {
            cleanup();
        }

        Mesh(const Mesh &) = delete;
        Mesh &operator=(const Mesh &) = delete;

        MESH_HOT_PATH MESH_FORCE_INLINE uint16_t addVertex(const Vector3 &pos)
        {
            if (unlikely(isStaticStorage))
            {
                LOGE(::pip3D::Debug::LOG_MODULE_RESOURCES,
                     "Mesh::addVertex called on static-storage mesh");
                return 0xFFFF;
            }
            if (unlikely(!vertices || vertexCount >= maxVertices))
            {
                LOGE(::pip3D::Debug::LOG_MODULE_RESOURCES,
                     "Mesh::addVertex failed: vertices=%p, vertexCount=%u, maxVertices=%u",
                     static_cast<void *>(vertices),
                     static_cast<unsigned int>(vertexCount),
                     static_cast<unsigned int>(maxVertices));
                return 0xFFFF;
            }

            Vertex &v = vertices[vertexCount];
            v.px = quantizeCoord(pos.x);
            v.py = quantizeCoord(pos.y);
            v.pz = quantizeCoord(pos.z);
            v.normal.data = 0;

            cache.boundsValid = false;
            return vertexCount++;
        }

        MESH_HOT_PATH MESH_FORCE_INLINE bool addFace(uint16_t v0, uint16_t v1, uint16_t v2)
        {
            if (unlikely(isStaticStorage))
            {
                LOGE(::pip3D::Debug::LOG_MODULE_RESOURCES,
                     "Mesh::addFace called on static-storage mesh");
                return false;
            }
            if (unlikely(!faces || !vertices))
            {
                LOGE(::pip3D::Debug::LOG_MODULE_RESOURCES,
                     "Mesh::addFace failed: vertices=%p, faces=%p",
                     static_cast<void *>(vertices),
                     static_cast<void *>(faces));
                return false;
            }
            if (unlikely(faceCount >= maxFaces ||
                         v0 >= vertexCount ||
                         v1 >= vertexCount ||
                         v2 >= vertexCount))
            {
                LOGE(::pip3D::Debug::LOG_MODULE_RESOURCES,
                     "Mesh::addFace invalid indices or overflow: faceCount=%u, maxFaces=%u, v=(%u,%u,%u), vertexCount=%u",
                     static_cast<unsigned int>(faceCount),
                     static_cast<unsigned int>(maxFaces),
                     static_cast<unsigned int>(v0),
                     static_cast<unsigned int>(v1),
                     static_cast<unsigned int>(v2),
                     static_cast<unsigned int>(vertexCount));
                return false;
            }

            Face &f = faces[faceCount++];
            f.v0 = v0;
            f.v1 = v1;
            f.v2 = v2;
            return true;
        }

        MESH_HOT_PATH void finalizeNormals()
        {
            if (unlikely(isStaticStorage))
            {
                LOGW(::pip3D::Debug::LOG_MODULE_RESOURCES,
                     "Mesh::finalizeNormals skipped for static-storage mesh");
                return;
            }
            if (unlikely(!vertices || !faces))
            {
                LOGE(::pip3D::Debug::LOG_MODULE_RESOURCES,
                     "Mesh::finalizeNormals: missing geometry buffers (vertices=%p, faces=%p, vertexCount=%u, faceCount=%u)",
                     static_cast<void *>(vertices),
                     static_cast<void *>(faces),
                     static_cast<unsigned int>(vertexCount),
                     static_cast<unsigned int>(faceCount));
                return;
            }
            if (unlikely(vertexCount == 0))
            {
                return;
            }

            const size_t normalSize = vertexCount * sizeof(Vector3);
            Vector3 *vertexNormals = (Vector3 *)heap_caps_aligned_alloc(16, normalSize, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);

            if (unlikely(!vertexNormals))
            {
                LOGE(::pip3D::Debug::LOG_MODULE_RESOURCES,
                     "Mesh::finalizeNormals: failed to allocate %u bytes for temporary normals (vertexCount=%u)",
                     static_cast<unsigned int>(normalSize),
                     static_cast<unsigned int>(vertexCount));
                return;
            }

            const Vertex *__restrict vertPtr = vertices;
            const Face *__restrict facePtr = faces;

            const size_t positionSize = vertexCount * sizeof(Vector3);
            Vector3 *vertexPositions = (Vector3 *)heap_caps_aligned_alloc(16, positionSize, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);

            if (vertexPositions)
            {
                for (uint16_t i = 0; i < vertexCount; i++)
                {
                    vertexNormals[i] = Vector3(0.0f, 0.0f, 0.0f);
                    vertexPositions[i] = decodePosition(vertPtr[i]);
                }

                for (uint16_t f = 0; f < faceCount; f++)
                {
                    const Face &face = facePtr[f];

                    const Vector3 &v0 = vertexPositions[face.v0];
                    const Vector3 &v1 = vertexPositions[face.v1];
                    const Vector3 &v2 = vertexPositions[face.v2];

                    Vector3 edge1 = v1 - v0;
                    Vector3 edge2 = v2 - v0;
                    Vector3 normal = edge1.cross(edge2);

                    float lenSq = normal.lengthSquared();
                    if (likely(lenSq > EPSILON_SQ))
                    {
                        vertexNormals[face.v0] += normal;
                        vertexNormals[face.v1] += normal;
                        vertexNormals[face.v2] += normal;
                    }
                }

                heap_caps_free(vertexPositions);
            }
            else
            {
                memset(vertexNormals, 0, normalSize);

                for (uint16_t f = 0; f < faceCount; f++)
                {
                    const Face &face = facePtr[f];

                    Vector3 v0 = decodePosition(vertPtr[face.v0]);
                    Vector3 v1 = decodePosition(vertPtr[face.v1]);
                    Vector3 v2 = decodePosition(vertPtr[face.v2]);

                    Vector3 edge1 = v1 - v0;
                    Vector3 edge2 = v2 - v0;
                    Vector3 normal = edge1.cross(edge2);

                    float lenSq = normal.lengthSquared();
                    if (likely(lenSq > EPSILON_SQ))
                    {
                        vertexNormals[face.v0] += normal;
                        vertexNormals[face.v1] += normal;
                        vertexNormals[face.v2] += normal;
                    }
                }
            }

            for (uint16_t i = 0; i < vertexCount; i++)
            {
                vertexNormals[i].normalize();
                vertices[i].normal.set(vertexNormals[i]);
            }

            heap_caps_free(vertexNormals);
        }

        MESH_HOT_PATH MESH_FORCE_INLINE void setPosition(float x, float y, float z)
        {
            position.x = x;
            position.y = y;
            position.z = z;
            invalidateTransform();
        }

        MESH_HOT_PATH MESH_FORCE_INLINE void setRotation(float x, float y, float z)
        {
            rotation.x = x;
            rotation.y = y;
            rotation.z = z;
            invalidateTransform();
        }

        MESH_HOT_PATH MESH_FORCE_INLINE void setScale(float x, float y, float z)
        {
            scale.x = x;
            scale.y = y;
            scale.z = z;
            invalidateTransform();
        }

        MESH_HOT_PATH MESH_FORCE_INLINE void rotate(float x, float y, float z)
        {
            rotation.x += x;
            rotation.y += y;
            rotation.z += z;
            invalidateTransform();
        }

        MESH_HOT_PATH MESH_FORCE_INLINE void translate(float x, float y, float z)
        {
            position.x += x;
            position.y += y;
            position.z += z;
            invalidateTransform();
        }

        MESH_FORCE_INLINE void invalidateTransform()
        {
            transformDirty = true;
            cache.transformValid = false;
        }

        MESH_PURE MESH_FORCE_INLINE Vector3 pos() const { return position; }

        MESH_HOT_PATH void calculateBoundingSphere()
        {
            if (unlikely(!vertices || vertexCount == 0))
            {
                cache.boundingCenter = Vector3(0, 0, 0);
                cache.boundingRadius = 0;
                cache.boundsValid = true;
                return;
            }

            Vector3 center(0, 0, 0);
            const Vertex *__restrict vPtr = vertices;

            for (uint16_t i = 0; i < vertexCount; i++)
            {
                center += decodePosition(vPtr[i]);
            }

            float invCount = 1.0f / vertexCount;
            center *= invCount;

            float maxDistSq = 0;
            for (uint16_t i = 0; i < vertexCount; i++)
            {
                Vector3 diff = decodePosition(vPtr[i]) - center;
                float distSq = diff.lengthSquared();
                if (distSq > maxDistSq)
                    maxDistSq = distSq;
            }

            cache.boundingCenter = center;
            cache.boundingRadius = sqrtf(maxDistSq);
            cache.boundsValid = true;
        }

        MESH_HOT_PATH void updateTransform() const
        {
            if (likely(!transformDirty && cache.transformValid))
                return;

            uint32_t newHash = computeTransformHash();
            if (cache.transformHash == newHash && cache.transformValid)
                return;

            Matrix4x4 t, r, s;
            t.setTranslation(position.x, position.y, position.z);

            Matrix4x4 rx, ry, rz;
            rx.setRotationX(rotation.x);
            ry.setRotationY(rotation.y);
            rz.setRotationZ(rotation.z);
            r = rz * ry * rx;

            s.setScale(scale.x, scale.y, scale.z);

            cache.transform = t * r * s;
            cache.maxScale = fmaxf(fmaxf(scale.x, scale.y), scale.z);
            cache.transformHash = newHash;
            cache.transformValid = true;

            transformDirty = false;
        }

        MESH_PURE MESH_FORCE_INLINE uint32_t computeTransformHash() const
        {
            uint32_t hash = 2166136261u;
            const float *data = &position.x;
            for (int i = 0; i < 9; i++)
            {
                uint32_t val;
                memcpy(&val, &data[i], sizeof(uint32_t));
                hash ^= val;
                hash *= 16777619u;
            }
            return hash;
        }

        MESH_HOT_PATH Vector3 center() const
        {
            updateTransform();
            if (unlikely(!cache.boundsValid))
            {
                const_cast<Mesh *>(this)->calculateBoundingSphere();
            }
            return cache.transform.transformNoDiv(cache.boundingCenter);
        }

        MESH_HOT_PATH float radius() const
        {
            updateTransform();
            if (unlikely(!cache.boundsValid))
            {
                const_cast<Mesh *>(this)->calculateBoundingSphere();
            }
            return cache.boundingRadius * cache.maxScale * 1.2f;
        }

        MESH_HOT_PATH Vector3 vertex(uint16_t index) const
        {
            if (unlikely(index >= vertexCount))
            {
                LOGW(::pip3D::Debug::LOG_MODULE_SCENE,
                     "Mesh::vertex index out of range (index=%u, vertexCount=%u)",
                     static_cast<unsigned int>(index),
                     static_cast<unsigned int>(vertexCount));
                return Vector3();
            }
            updateTransform();
            const Vertex &v = vertices[index];
            Vector3 local = decodePosition(v);
            return cache.transform.transformNoDiv(local);
        }

        MESH_HOT_PATH Vector3 normal(uint16_t index) const
        {
            if (unlikely(index >= vertexCount))
            {
                LOGW(::pip3D::Debug::LOG_MODULE_SCENE,
                     "Mesh::normal index out of range (index=%u, vertexCount=%u)",
                     static_cast<unsigned int>(index),
                     static_cast<unsigned int>(vertexCount));
                return Vector3(0.0f, 1.0f, 0.0f);
            }
            updateTransform();
            Vector3 n = vertices[index].normal.get();
            return cache.transform.transformNormal(n);
        }

        MESH_HOT_PATH MESH_FORCE_INLINE void clear()
        {
            vertexCount = faceCount = 0;
            cache.boundsValid = false;
        }

        MESH_PURE MESH_FORCE_INLINE uint16_t numFaces() const { return faceCount; }
        MESH_PURE MESH_FORCE_INLINE const Face &face(uint16_t i) const { return faces[i]; }
        MESH_PURE MESH_FORCE_INLINE const Vertex &vert(uint16_t i) const { return vertices[i]; }
        MESH_PURE MESH_FORCE_INLINE Color color() const { return meshColor; }
        MESH_FORCE_INLINE void color(const Color &c) { meshColor = c; }
        MESH_FORCE_INLINE void show() { visible = true; }
        MESH_FORCE_INLINE void hide() { visible = false; }
        MESH_PURE MESH_FORCE_INLINE bool isVisible() const { return visible; }

        MESH_FORCE_INLINE void setCastShadows(bool enabled) { castShadows = enabled; }
        MESH_PURE MESH_FORCE_INLINE bool getCastShadows() const { return castShadows; }

        MESH_HOT_PATH const Matrix4x4 &getTransform() const
        {
            updateTransform();
            return cache.transform;
        }
    };

}

#endif
