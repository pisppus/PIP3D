#ifndef PRIMITIVESHAPES_H
#define PRIMITIVESHAPES_H

#include "Mesh.h"

namespace pip3D
{

    class Cube : public Mesh
    {
    public:
        Cube(float size = 1.0f, const Color &color = Color::WHITE) : Mesh(8, 12, color)
        {
            if (!vertices || !faces)
            {
                LOGE(::pip3D::Debug::LOG_MODULE_RESOURCES,
                     "Cube: Mesh base allocation failed (vertices=%p, faces=%p)",
                     static_cast<void *>(vertices),
                     static_cast<void *>(faces));
                return;
            }

            constexpr float half = 0.5f;

            const float h = size * half;
            addVertex(Vector3(-h, -h, -h));
            addVertex(Vector3(h, -h, -h));
            addVertex(Vector3(h, h, -h));
            addVertex(Vector3(-h, h, -h));
            addVertex(Vector3(-h, -h, h));
            addVertex(Vector3(h, -h, h));
            addVertex(Vector3(h, h, h));
            addVertex(Vector3(-h, h, h));
            addFace(0, 2, 1);
            addFace(0, 3, 2);
            addFace(4, 5, 6);
            addFace(4, 6, 7);
            addFace(4, 3, 0);
            addFace(4, 7, 3);
            addFace(1, 2, 6);
            addFace(1, 6, 5);
            addFace(3, 7, 6);
            addFace(3, 6, 2);
            addFace(4, 0, 1);
            addFace(4, 1, 5);
            finalize();
        }

    private:
        inline void finalize()
        {
            finalizeNormals();
            calculateBoundingSphere();
            updateTransform();
        }
    };

    class Pyramid : public Mesh
    {
    public:
        Pyramid(float size = 1.0f, const Color &color = Color::WHITE) : Mesh(5, 6, color)
        {
            if (!vertices || !faces)
            {
                LOGE(::pip3D::Debug::LOG_MODULE_RESOURCES,
                     "Pyramid: Mesh base allocation failed (vertices=%p, faces=%p)",
                     static_cast<void *>(vertices),
                     static_cast<void *>(faces));
                return;
            }

            constexpr float half = 0.5f;
            const float h = size * half;
            addVertex(Vector3(0, h, 0));
            addVertex(Vector3(-h, -h, -h));
            addVertex(Vector3(h, -h, -h));
            addVertex(Vector3(h, -h, h));
            addVertex(Vector3(-h, -h, h));
            addFace(1, 2, 3);
            addFace(1, 3, 4);
            addFace(0, 2, 1);
            addFace(0, 3, 2);
            addFace(0, 4, 3);
            addFace(0, 1, 4);
            finalize();
        }

    private:
        inline void finalize()
        {
            finalizeNormals();
            calculateBoundingSphere();
            updateTransform();
        }
    };

    class Sphere : public Mesh
    {
    public:
        Sphere(float radius = 1.0f, uint8_t segments = 8, uint8_t rings = 6, const Color &color = Color::WHITE)
            : Mesh(2 + (segments ? segments : 3) * ((rings > 1 ? rings : 2) - 1),
                   2 * (segments ? segments : 3) * ((rings > 1 ? rings : 2) - 1),
                   color)
        {
            if (!vertices || !faces)
            {
                LOGE(::pip3D::Debug::LOG_MODULE_RESOURCES,
                     "Sphere: Mesh base allocation failed (vertices=%p, faces=%p)",
                     static_cast<void *>(vertices),
                     static_cast<void *>(faces));
                return;
            }

            uint8_t segs = segments ? segments : 3;
            uint8_t ringCount = rings > 1 ? rings : 2;

            addVertex(Vector3(0, radius, 0));
            for (uint8_t i = 1; i < ringCount; i++)
            {
                const float phi = PI * i / ringCount;
                const float y = radius * FastMath::fastCos(phi);
                const float r = radius * FastMath::fastSin(phi);
                for (uint8_t j = 0; j < segs; j++)
                {
                    const float theta = TWO_PI * j / segs;
                    addVertex(Vector3(r * FastMath::fastCos(theta), y, r * FastMath::fastSin(theta)));
                }
            }
            const uint16_t bottomIdx = addVertex(Vector3(0, -radius, 0));
            for (uint8_t j = 0; j < segs; j++)
            {
                const uint16_t j1 = (j + 1) % segs;
                addFace(0, 1 + j1, 1 + j);
            }
            for (uint8_t i = 1; i < ringCount - 1; i++)
            {
                const uint16_t base1 = 1 + (i - 1) * segs;
                const uint16_t base2 = 1 + i * segs;
                for (uint8_t j = 0; j < segs; j++)
                {
                    const uint16_t j1 = (j + 1) % segs;
                    addFace(base1 + j, base1 + j1, base2 + j);
                    addFace(base2 + j, base1 + j1, base2 + j1);
                }
            }
            const uint16_t lastRing = 1 + (ringCount - 2) * segs;
            for (uint8_t j = 0; j < segs; j++)
            {
                const uint16_t j1 = (j + 1) % segs;
                addFace(bottomIdx, lastRing + j, lastRing + j1);
            }
            finalize();
        }

        Sphere(float radius, const Color &color)
            : Sphere(radius, 16, 12, color)
        {
        }

    private:
        inline void finalize()
        {
            finalizeNormals();
            calculateBoundingSphere();
            updateTransform();
        }
    };

    class Plane : public Mesh
    {
    public:
        Plane(float width = 2.0f, float depth = 2.0f, uint8_t subdivisions = 1, const Color &color = Color::WHITE)
            : Mesh(((subdivisions ? subdivisions : 1) + 1) * ((subdivisions ? subdivisions : 1) + 1),
                   (subdivisions ? subdivisions : 1) * (subdivisions ? subdivisions : 1) * 2,
                   color)
        {
            if (!vertices || !faces)
            {
                LOGE(::pip3D::Debug::LOG_MODULE_RESOURCES,
                     "Plane: Mesh base allocation failed (vertices=%p, faces=%p)",
                     static_cast<void *>(vertices),
                     static_cast<void *>(faces));
                return;
            }

            uint8_t divs = subdivisions ? subdivisions : 1;

            const float stepX = width / divs, stepZ = depth / divs;
            const float startX = -width * 0.5f, startZ = -depth * 0.5f;
            for (uint8_t z = 0; z <= divs; z++)
            {
                for (uint8_t x = 0; x <= divs; x++)
                {
                    addVertex(Vector3(startX + x * stepX, 0, startZ + z * stepZ));
                }
            }
            const uint16_t pitch = divs + 1;
            for (uint8_t z = 0; z < divs; z++)
            {
                for (uint8_t x = 0; x < divs; x++)
                {
                    const uint16_t i = z * pitch + x;
                    addFace(i, i + pitch, i + 1);
                    addFace(i + 1, i + pitch, i + pitch + 1);
                }
            }
            finalize();
        }

    private:
        inline void finalize()
        {
            finalizeNormals();
            calculateBoundingSphere();
            updateTransform();
        }
    };

    class Cylinder : public Mesh
    {
    public:
        Cylinder(float radius = 1.0f, float height = 2.0f, uint8_t segments = 16, const Color &color = Color::WHITE)
            : Mesh(2 + (segments ? segments : 3) * 2,
                   (segments ? segments : 3) * 4,
                   color)
        {
            if (!vertices || !faces)
            {
                LOGE(::pip3D::Debug::LOG_MODULE_RESOURCES,
                     "Cylinder: Mesh base allocation failed (vertices=%p, faces=%p)",
                     static_cast<void *>(vertices),
                     static_cast<void *>(faces));
                return;
            }

            constexpr float half = 0.5f;
            const float h = height * half;
            const uint16_t topCenter = addVertex(Vector3(0, h, 0));
            uint8_t segs = segments ? segments : 3;
            for (uint8_t i = 0; i < segs; i++)
            {
                const float angle = TWO_PI * i / segs;
                addVertex(Vector3(radius * FastMath::fastCos(angle), h, radius * FastMath::fastSin(angle)));
            }
            for (uint8_t i = 0; i < segs; i++)
            {
                const float angle = TWO_PI * i / segs;
                addVertex(Vector3(radius * FastMath::fastCos(angle), -h, radius * FastMath::fastSin(angle)));
            }
            const uint16_t bottomCenter = addVertex(Vector3(0, -h, 0));
            for (uint8_t i = 0; i < segs; i++)
            {
                const uint16_t next = (i + 1) % segs;
                addFace(topCenter, 1 + next, 1 + i);
                addFace(bottomCenter, 1 + segs + i, 1 + segs + next);
                const uint16_t top1 = 1 + i, top2 = 1 + next, bot1 = 1 + segs + i, bot2 = 1 + segs + next;
                addFace(top1, top2, bot1);
                addFace(top2, bot2, bot1);
            }
            finalize();
        }

    private:
        inline void finalize()
        {
            finalizeNormals();
            calculateBoundingSphere();
            updateTransform();
        }
    };

    class Cone : public Mesh
    {
    public:
        Cone(float radius = 1.0f, float height = 2.0f, uint8_t segments = 16, const Color &color = Color::WHITE)
            : Mesh((segments ? segments : 3) + 2,
                   (segments ? segments : 3) * 2,
                   color)
        {
            if (!vertices || !faces)
            {
                LOGE(::pip3D::Debug::LOG_MODULE_RESOURCES,
                     "Cone: Mesh base allocation failed (vertices=%p, faces=%p)",
                     static_cast<void *>(vertices),
                     static_cast<void *>(faces));
                return;
            }

            constexpr float half = 0.5f;
            const float h = height * half;
            const uint16_t apex = addVertex(Vector3(0, h, 0));
            uint8_t segs = segments ? segments : 3;
            for (uint8_t i = 0; i < segs; i++)
            {
                const float angle = TWO_PI * i / segs;
                addVertex(Vector3(radius * FastMath::fastCos(angle), -h, radius * FastMath::fastSin(angle)));
            }
            const uint16_t baseCenter = addVertex(Vector3(0, -h, 0));
            for (uint8_t i = 0; i < segs; i++)
            {
                const uint16_t next = (i + 1) % segs;
                addFace(apex, 1 + next, 1 + i);
                addFace(baseCenter, 1 + i, 1 + next);
            }
            finalize();
        }

    private:
        inline void finalize()
        {
            finalizeNormals();
            calculateBoundingSphere();
            updateTransform();
        }
    };

    class Capsule : public Mesh
    {
    public:
        Capsule(float radius = 1.0f, float height = 2.0f, uint8_t segments = 12, uint8_t rings = 6, const Color &color = Color::WHITE)
            : Mesh((segments ? segments : 3) * (rings < 2 ? 2 : rings) + 2,
                   (segments ? segments : 3) * ((rings < 2 ? 2 : rings) + 1) * 2,
                   color)
        {
            if (!vertices || !faces)
            {
                LOGE(::pip3D::Debug::LOG_MODULE_RESOURCES,
                     "Capsule: Mesh base allocation failed (vertices=%p, faces=%p)",
                     static_cast<void *>(vertices),
                     static_cast<void *>(faces));
                return;
            }

            constexpr float two = 2.0f;
            constexpr float half = 0.5f;
            const float cylinderHeight = height - two * radius;
            const float halfCyl = cylinderHeight * half;
            uint8_t segs = segments ? segments : 3;
            uint8_t ringCount = rings < 2 ? 2 : rings;
            uint8_t halfRings = ringCount / 2;
            const uint16_t topPole = addVertex(Vector3(0, halfCyl + radius, 0));
            for (uint8_t ring = 1; ring <= halfRings; ring++)
            {
                constexpr float piHalf = PI * 0.5f;
                const float phi = piHalf * ring / halfRings;
                const float y = halfCyl + radius * FastMath::fastCos(phi);
                const float r = radius * FastMath::fastSin(phi);
                for (uint8_t seg = 0; seg < segs; seg++)
                {
                    const float theta = TWO_PI * seg / segs;
                    addVertex(Vector3(r * FastMath::fastCos(theta), y, r * FastMath::fastSin(theta)));
                }
            }
            for (uint8_t ring = 1; ring <= halfRings; ring++)
            {
                constexpr float piHalf = PI * 0.5f;
                const float phi = piHalf * ring / halfRings;
                const float y = -halfCyl - radius * FastMath::fastCos(phi);
                const float r = radius * FastMath::fastSin(phi);
                for (uint8_t seg = 0; seg < segs; seg++)
                {
                    const float theta = TWO_PI * seg / segs;
                    addVertex(Vector3(r * FastMath::fastCos(theta), y, r * FastMath::fastSin(theta)));
                }
            }
            const uint16_t bottomPole = addVertex(Vector3(0, -halfCyl - radius, 0));
            for (uint8_t seg = 0; seg < segs; seg++)
            {
                const uint16_t next = (seg + 1) % segs;
                addFace(topPole, 1 + seg, 1 + next);
            }
            for (uint8_t ring = 0; ring < ringCount - 1; ring++)
            {
                for (uint8_t seg = 0; seg < segs; seg++)
                {
                    const uint16_t next = (seg + 1) % segs;
                    const uint16_t curr = 1 + ring * segs + seg, currNext = 1 + ring * segs + next;
                    const uint16_t below = 1 + (ring + 1) * segs + seg, belowNext = 1 + (ring + 1) * segs + next;
                    addFace(curr, below, currNext);
                    addFace(currNext, below, belowNext);
                }
            }
            const uint16_t lastRingStart = 1 + (ringCount - 1) * segs;
            for (uint8_t seg = 0; seg < segs; seg++)
            {
                const uint16_t next = (seg + 1) % segs;
                addFace(bottomPole, lastRingStart + next, lastRingStart + seg);
            }
            finalize();
        }

    private:
        inline void finalize()
        {
            finalizeNormals();
            calculateBoundingSphere();
            updateTransform();
        }
    };

    class Teapot : public Mesh
    {
    public:
        Teapot(float scale = 1.0f, const Color &color = Color::WHITE)
            : Mesh(512, 1024, color)
        {
            if (!vertices || !faces)
            {
                LOGE(::pip3D::Debug::LOG_MODULE_RESOURCES,
                     "Teapot: Mesh base allocation failed (vertices=%p, faces=%p)",
                     static_cast<void *>(vertices),
                     static_cast<void *>(faces));
                return;
            }

            static const int SEGMENTS = 24;
            static const int BODY_RINGS = 10;
            static const float bodyY[BODY_RINGS + 1] = {
                0.0f, 0.1f, 0.25f, 0.4f, 0.55f, 0.7f, 0.85f, 1.0f, 1.15f, 1.3f, 1.4f};
            static const float bodyR[BODY_RINGS + 1] = {
                0.7f, 0.9f, 1.1f, 1.3f, 1.5f, 1.6f, 1.6f, 1.4f, 1.2f, 1.0f, 0.8f};

            const uint16_t bodyBase = vertexCount;
            for (int ring = 0; ring <= BODY_RINGS; ++ring)
            {
                const float y = bodyY[ring] * scale;
                const float r = bodyR[ring] * scale;
                for (int seg = 0; seg < SEGMENTS; ++seg)
                {
                    const float angle = TWO_PI * (float)seg / (float)SEGMENTS;
                    const float x = r * FastMath::fastCos(angle);
                    const float z = r * FastMath::fastSin(angle);
                    addVertex(Vector3(x, y, z));
                }
            }

            const uint16_t bottomCenter = addVertex(Vector3(0.0f, bodyY[0] * scale, 0.0f));

            for (int ring = 0; ring < BODY_RINGS; ++ring)
            {
                for (int seg = 0; seg < SEGMENTS; ++seg)
                {
                    const int nextSeg = (seg + 1) % SEGMENTS;
                    const uint16_t v00 = bodyBase + ring * SEGMENTS + seg;
                    const uint16_t v10 = bodyBase + (ring + 1) * SEGMENTS + seg;
                    const uint16_t v11 = bodyBase + (ring + 1) * SEGMENTS + nextSeg;
                    const uint16_t v01 = bodyBase + ring * SEGMENTS + nextSeg;
                    addFace(v00, v10, v11);
                    addFace(v00, v11, v01);
                }
            }

            for (int seg = 0; seg < SEGMENTS; ++seg)
            {
                const int nextSeg = (seg + 1) % SEGMENTS;
                const uint16_t v0 = bodyBase + seg;
                const uint16_t v1 = bodyBase + nextSeg;
                addFace(bottomCenter, v1, v0);
            }

            static const int LID_RINGS = 4;
            static const float lidY[LID_RINGS + 1] = {
                1.4f, 1.55f, 1.7f, 1.8f, 1.85f};
            static const float lidR[LID_RINGS + 1] = {
                0.8f, 0.7f, 0.5f, 0.3f, 0.15f};

            const uint16_t lidBase = vertexCount;
            for (int ring = 0; ring <= LID_RINGS; ++ring)
            {
                const float y = lidY[ring] * scale;
                const float r = lidR[ring] * scale;
                for (int seg = 0; seg < SEGMENTS; ++seg)
                {
                    const float angle = TWO_PI * (float)seg / (float)SEGMENTS;
                    const float x = r * FastMath::fastCos(angle);
                    const float z = r * FastMath::fastSin(angle);
                    addVertex(Vector3(x, y, z));
                }
            }

            for (int ring = 0; ring < LID_RINGS; ++ring)
            {
                for (int seg = 0; seg < SEGMENTS; ++seg)
                {
                    const int nextSeg = (seg + 1) % SEGMENTS;
                    const uint16_t v00 = lidBase + ring * SEGMENTS + seg;
                    const uint16_t v10 = lidBase + (ring + 1) * SEGMENTS + seg;
                    const uint16_t v11 = lidBase + (ring + 1) * SEGMENTS + nextSeg;
                    const uint16_t v01 = lidBase + ring * SEGMENTS + nextSeg;
                    addFace(v00, v10, v11);
                    addFace(v00, v11, v01);
                }
            }

            const uint16_t lidTopCenter = addVertex(Vector3(0.0f, lidY[LID_RINGS] * scale, 0.0f));
            const uint16_t lastRingStart = lidBase + LID_RINGS * SEGMENTS;
            for (int seg = 0; seg < SEGMENTS; ++seg)
            {
                const int nextSeg = (seg + 1) % SEGMENTS;
                const uint16_t v0 = lastRingStart + seg;
                const uint16_t v1 = lastRingStart + nextSeg;
                addFace(lidTopCenter, v0, v1);
            }

            static const int KNOB_SEGMENTS = 12;
            const float knobBaseY = (lidY[LID_RINGS] + 0.05f) * scale;
            const float knobTopY = (lidY[LID_RINGS] + 0.18f) * scale;
            const float knobR = 0.15f * scale;

            const uint16_t knobBase = vertexCount;
            for (int seg = 0; seg < KNOB_SEGMENTS; ++seg)
            {
                const float angle = TWO_PI * (float)seg / (float)KNOB_SEGMENTS;
                const float x = knobR * FastMath::fastCos(angle);
                const float z = knobR * FastMath::fastSin(angle);
                addVertex(Vector3(x, knobBaseY, z));
            }

            const uint16_t knobTop = addVertex(Vector3(0.0f, knobTopY, 0.0f));
            for (int seg = 0; seg < KNOB_SEGMENTS; ++seg)
            {
                const int nextSeg = (seg + 1) % KNOB_SEGMENTS;
                const uint16_t v0 = knobBase + seg;
                const uint16_t v1 = knobBase + nextSeg;
                addFace(v0, v1, knobTop);
            }

            finalize();
        }

    private:
        inline void finalize()
        {
            finalizeNormals();
            calculateBoundingSphere();
            updateTransform();
        }
    };

    class TrefoilKnot : public Mesh
    {
    public:
        TrefoilKnot(float scale = 1.0f, uint8_t segments = 64, uint8_t tubeSegments = 12, const Color &color = Color::WHITE)
            : Mesh((segments ? segments : 3) * (tubeSegments ? tubeSegments : 3),
                   (segments ? segments : 3) * (tubeSegments ? tubeSegments : 3) * 2,
                   color)
        {
            if (!vertices || !faces)
            {
                LOGE(::pip3D::Debug::LOG_MODULE_RESOURCES,
                     "TrefoilKnot: Mesh base allocation failed (vertices=%p, faces=%p)",
                     static_cast<void *>(vertices),
                     static_cast<void *>(faces));
                return;
            }

            constexpr float tubeScale = 0.3f;
            const float tubeRadius = tubeScale * scale;
            uint8_t segs = segments ? segments : 3;
            uint8_t tubeSegs = tubeSegments ? tubeSegments : 3;
            for (uint8_t i = 0; i < segs; i++)
            {
                const float t = TWO_PI * i / segs;
                constexpr float two = 2.0f;
                constexpr float three = 3.0f;
                const float sin_t = FastMath::fastSin(t);
                const float cos_t = FastMath::fastCos(t);
                const float sin_2t = FastMath::fastSin(two * t);
                const float cos_2t = FastMath::fastCos(two * t);
                const float sin_3t = FastMath::fastSin(three * t);
                const float x = scale * (sin_t + two * sin_2t);
                const float y = scale * (cos_t - two * cos_2t);
                const float z = scale * (-sin_3t);
                for (uint8_t j = 0; j < tubeSegs; j++)
                {
                    const float angle = TWO_PI * j / tubeSegs;
                    const float nx = FastMath::fastCos(angle), ny = FastMath::fastSin(angle);
                    const float vx = x + tubeRadius * (nx * cos_t - ny * sin_t);
                    const float vy = y + tubeRadius * (nx * sin_t + ny * cos_t);
                    const float vz = z + tubeRadius * ny;
                    addVertex(Vector3(vx, vy, vz));
                }
            }
            for (uint8_t i = 0; i < segs; i++)
            {
                const uint16_t i1 = (i + 1) % segs;
                for (uint8_t j = 0; j < tubeSegs; j++)
                {
                    const uint16_t j1 = (j + 1) % tubeSegs;
                    const uint16_t a = i * tubeSegs + j, b = i1 * tubeSegs + j;
                    const uint16_t c = i1 * tubeSegs + j1, d = i * tubeSegs + j1;
                    addFace(a, b, c);
                    addFace(a, c, d);
                }
            }
            finalize();
        }

    private:
        inline void finalize()
        {
            finalizeNormals();
            calculateBoundingSphere();
            updateTransform();
        }
    };

}

#endif
