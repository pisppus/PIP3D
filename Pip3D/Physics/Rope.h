
#ifndef PIP3D_PHYSICS_ROPE_H
#define PIP3D_PHYSICS_ROPE_H

#include "Body.h"
#include "../Core/Core.h"
#include "../Core/Instance.h"
#include "../Rendering/Renderer.h"

namespace pip3D
{

    class Rope
    {
    public:
        struct Node
        {
            Vector3 position;
            Vector3 prevPosition;
            bool fixed;

            Node()
                : position(0.0f, 0.0f, 0.0f),
                  prevPosition(0.0f, 0.0f, 0.0f),
                  fixed(false)
            {
            }
        };

        static constexpr int MAX_NODES = 64;

    private:
        Node nodes[MAX_NODES];
        int nodeCount;

        float segmentLength;
        int iterations;

        Vector3 gravity;
        float airDamping;

        float floorHeight;
        float floorFriction;
        float collisionFriction;

    public:
        Rope()
            : nodeCount(0),
              segmentLength(0.25f),
              iterations(8),
              gravity(0.0f, -9.81f, 0.0f),
              airDamping(0.98f),
              floorHeight(0.0f),
              floorFriction(0.6f),
              collisionFriction(0.6f)
        {
        }

        void setIterations(int it)
        {
            if (it < 1)
                it = 1;
            if (it > 32)
                it = 32;
            iterations = it;
        }

        void setGravity(const Vector3 &g)
        {
            gravity = g;
        }

        void setAirDamping(float d)
        {
            if (d < 0.0f)
                d = 0.0f;
            if (d > 1.0f)
                d = 1.0f;
            airDamping = d;
        }

        void setFloorHeight(float h)
        {
            floorHeight = h;
        }

        void setFloorFriction(float f)
        {
            if (f < 0.0f)
                f = 0.0f;
            if (f > 1.0f)
                f = 1.0f;
            floorFriction = f;
        }

        void setCollisionFriction(float f)
        {
            if (f < 0.0f)
                f = 0.0f;
            if (f > 1.0f)
                f = 1.0f;
            collisionFriction = f;
        }

        int getNodeCount() const
        {
            return nodeCount;
        }

        const Node &getNode(int index) const
        {
            if (index < 0)
                index = 0;
            if (index >= nodeCount)
                index = nodeCount > 0 ? nodeCount - 1 : 0;
            return nodes[index];
        }

        Node &getNode(int index)
        {
            if (index < 0)
                index = 0;
            if (index >= nodeCount)
                index = nodeCount > 0 ? nodeCount - 1 : 0;
            return nodes[index];
        }

        Vector3 getEndDirection() const
        {
            if (nodeCount < 2)
                return Vector3(0.0f, -1.0f, 0.0f);
            const Node &a = nodes[nodeCount - 2];
            const Node &b = nodes[nodeCount - 1];
            Vector3 dir = b.position - a.position;
            dir.normalize();
            return dir;
        }

        void initLinear(const Vector3 &start,
                        const Vector3 &end,
                        int segments,
                        bool fixStart = true,
                        bool fixEnd = false)
        {
            if (segments < 1)
                segments = 1;
            if (segments >= MAX_NODES)
                segments = MAX_NODES - 1;

            nodeCount = segments + 1;

            Vector3 delta = end - start;
            float totalLen = delta.length();
            if (totalLen <= 1e-5f)
            {
                delta = Vector3(0.0f, -1.0f, 0.0f);
                totalLen = 1.0f;
            }

            segmentLength = totalLen / static_cast<float>(segments);

            Vector3 step = delta * (1.0f / static_cast<float>(segments));

            for (int i = 0; i < nodeCount; ++i)
            {
                Vector3 p = start + step * static_cast<float>(i);
                nodes[i].position = p;
                nodes[i].prevPosition = p;
                nodes[i].fixed = false;
            }

            if (fixStart)
                nodes[0].fixed = true;
            if (fixEnd)
                nodes[nodeCount - 1].fixed = true;
        }

        void simulate(float dt)
        {
            if (nodeCount <= 1)
                return;
            if (dt <= 0.0f)
                return;

            float dt2 = dt * dt;

            for (int i = 0; i < nodeCount; ++i)
            {
                Node &n = nodes[i];
                if (n.fixed)
                    continue;

                Vector3 cur = n.position;
                Vector3 vel = (n.position - n.prevPosition) * airDamping;
                Vector3 next = cur + vel + gravity * dt2;

                n.prevPosition = cur;
                n.position = next;
            }

            for (int it = 0; it < iterations; ++it)
            {
                satisfyConstraints();
            }
        }

        void resolveCollisions(RigidBody **bodies, size_t bodyCount)
        {
            if (nodeCount == 0)
                return;

            for (int i = 0; i < nodeCount; ++i)
            {
                Node &n = nodes[i];
                if (n.fixed)
                    continue;

                // Floor collision (simple plane)
                if (n.position.y < floorHeight)
                {
                    float penetration = floorHeight - n.position.y;
                    n.position.y = floorHeight;
                    (void)penetration;
                    applyFriction(n, Vector3(0.0f, 1.0f, 0.0f), floorFriction);
                }

                for (size_t b = 0; b < bodyCount; ++b)
                {
                    RigidBody *body = bodies[b];
                    if (!body)
                        continue;

                    if (body->shape == BODY_SHAPE_SPHERE)
                    {
                        resolveSphereCollision(n, *body);
                    }
                    else
                    {
                        resolveBoxCollision(n, *body);
                    }
                }
            }
        }

        void renderLines(Renderer &renderer,
                         uint16_t color,
                         uint8_t thickness = 1)
        {
            if (nodeCount < 2)
                return;

            uint16_t *fb = renderer.getFrameBuffer();
            if (!fb)
                return;

            const Viewport &vp = renderer.getViewport();
            if (vp.width == 0 || vp.height == 0)
                return;

            for (int i = 0; i < nodeCount - 1; ++i)
            {
                const Node &a = nodes[i];
                const Node &b = nodes[i + 1];

                int x0, y0, x1, y1;
                if (!projectToScreen(renderer, vp, a.position, x0, y0))
                    continue;
                if (!projectToScreen(renderer, vp, b.position, x1, y1))
                    continue;

                drawLine2D(fb, vp, x0, y0, x1, y1, color, thickness);
            }
        }

        void renderChain(Renderer &renderer,
                         MeshInstance *linkInstance)
        {
            if (!linkInstance)
                return;
            if (nodeCount < 2)
                return;

            for (int i = 0; i < nodeCount - 1; ++i)
            {
                const Node &a = nodes[i];
                const Node &b = nodes[i + 1];

                Vector3 dir = b.position - a.position;
                float lenSq = dir.lengthSquared();
                if (lenSq < 1e-6f)
                    continue;
                dir.normalize();

                Vector3 mid = (a.position + b.position) * 0.5f;

                Quaternion rot = rotationBetween(Vector3(0.0f, 1.0f, 0.0f), dir);

                linkInstance->setPosition(mid);
                linkInstance->setRotation(rot);

                renderer.drawMeshInstance(linkInstance);
            }
        }

    private:
        void satisfyConstraints()
        {
            for (int i = 0; i < nodeCount - 1; ++i)
            {
                Node &a = nodes[i];
                Node &b = nodes[i + 1];

                Vector3 delta = b.position - a.position;
                float distSq = delta.lengthSquared();
                if (distSq <= 1e-8f)
                    continue;

                float dist = sqrtf(distSq);
                float diff = (dist - segmentLength) / dist;

                if (!a.fixed && !b.fixed)
                {
                    Vector3 correction = delta * (0.5f * diff);
                    a.position += correction;
                    b.position -= correction;
                }
                else if (a.fixed && !b.fixed)
                {
                    Vector3 correction = delta * diff;
                    b.position -= correction;
                }
                else if (!a.fixed && b.fixed)
                {
                    Vector3 correction = delta * diff;
                    a.position += correction;
                }
            }
        }

        void applyFriction(Node &n, const Vector3 & /*normal*/, float friction)
        {
            if (friction <= 0.0f)
                return;
            if (friction > 1.0f)
                friction = 1.0f;

            Vector3 vel = n.position - n.prevPosition;
            vel *= (1.0f - friction);
            n.prevPosition = n.position - vel;
        }

        void resolveSphereCollision(Node &n, const RigidBody &body)
        {
            float r = body.radius;
            if (r <= 0.0f)
                return;

            Vector3 toNode = n.position - body.position;
            float distSq = toNode.lengthSquared();
            float rSq = r * r;
            if (distSq >= rSq || distSq <= 1e-8f)
                return;

            float dist = sqrtf(distSq);
            Vector3 normal = toNode * (1.0f / dist);
            float penetration = r - dist;

            n.position += normal * penetration;
            applyFriction(n, normal, collisionFriction);
        }

        void resolveBoxCollision(Node &n, const RigidBody &body)
        {
            Vector3 half = body.size * 0.5f;
            if (half.x <= 0.0f || half.y <= 0.0f || half.z <= 0.0f)
                return;

            Quaternion invRot = body.orientation.conjugate();
            Vector3 local = invRot.rotate(n.position - body.position);

            float ax = fabsf(local.x);
            float ay = fabsf(local.y);
            float az = fabsf(local.z);

            if (ax > half.x || ay > half.y || az > half.z)
            {
                return;
            }

            float dx = half.x - ax;
            float dy = half.y - ay;
            float dz = half.z - az;

            Vector3 localNormal(0.0f, 0.0f, 0.0f);
            float penetration = dx;

            if (dx < dy && dx < dz)
            {
                localNormal.x = (local.x > 0.0f) ? 1.0f : -1.0f;
                penetration = dx;
            }
            else if (dy < dz)
            {
                localNormal.y = (local.y > 0.0f) ? 1.0f : -1.0f;
                penetration = dy;
            }
            else
            {
                localNormal.z = (local.z > 0.0f) ? 1.0f : -1.0f;
                penetration = dz;
            }

            Vector3 worldNormal = body.orientation.rotate(localNormal);

            n.position += worldNormal * penetration;
            applyFriction(n, worldNormal, collisionFriction);
        }

        static bool projectToScreen(Renderer &renderer,
                                    const Viewport &viewport,
                                    const Vector3 &world,
                                    int &sx,
                                    int &sy)
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

        static Quaternion rotationBetween(const Vector3 &fromDir, const Vector3 &toDir)
        {
            Vector3 v0 = fromDir;
            Vector3 v1 = toDir;
            v0.normalize();
            v1.normalize();

            float dot = v0.dot(v1);
            if (dot < -0.9999f)
            {
                Vector3 axis = v0.cross(Vector3(0.0f, 0.0f, 1.0f));
                if (axis.lengthSquared() < 1e-6f)
                {
                    axis = v0.cross(Vector3(1.0f, 0.0f, 0.0f));
                }
                axis.normalize();
                return Quaternion::fromAxisAngle(axis, 3.14159265f);
            }

            Vector3 c = v0.cross(v1);
            Quaternion q(c.x, c.y, c.z, 1.0f + dot);
            q.normalize();
            return q;
        }
    };

}

#endif
