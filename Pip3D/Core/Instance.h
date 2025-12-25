#ifndef INSTANCE_H
#define INSTANCE_H

#include "../Math/Math.h"
#include "../Geometry/Mesh.h"
#include "../Core/Debug/Logging.h"
#include "Frustum.h"
#include <vector>
#include <algorithm>

#ifndef likely
#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#endif

namespace pip3D
{

    class MeshInstance
    {
    private:
        Mesh *sourceMesh;
        Matrix4x4 localTransform;
        Vector3 position;
        Quaternion rotation;
        Vector3 scale;
        Color instanceColor;
        bool visible;
        bool transformDirty;

        mutable Vector3 cachedWorldCenter;
        mutable float cachedWorldRadius;
        mutable bool boundsDirty;

        size_t managerIndex;

        friend class InstanceManager;

    public:
        MeshInstance(Mesh *mesh = nullptr)
            : sourceMesh(mesh),
              position(0, 0, 0),
              rotation(),
              scale(1, 1, 1),
              instanceColor(Color::WHITE),
              visible(true),
              transformDirty(true),
              boundsDirty(true),
              managerIndex((size_t)-1)
        {
            localTransform.identity();
        }

        void reset(Mesh *mesh)
        {
            sourceMesh = mesh;
            position = Vector3(0, 0, 0);
            rotation = Quaternion();
            scale = Vector3(1, 1, 1);
            instanceColor = Color::WHITE;
            visible = true;
            transformDirty = true;
            boundsDirty = true;
            localTransform.identity();
        }

        void setMesh(Mesh *mesh)
        {
            sourceMesh = mesh;
            transformDirty = boundsDirty = true;
        }
        Mesh *getMesh() const { return sourceMesh; }

        void setPosition(const Vector3 &pos)
        {
            position = pos;
            transformDirty = boundsDirty = true;
        }
        void setPosition(float x, float y, float z)
        {
            setPosition(Vector3(x, y, z));
        }

        void setRotation(const Quaternion &rot)
        {
            rotation = rot;
            transformDirty = boundsDirty = true;
        }
        void setEuler(float pitch, float yaw, float roll)
        {
            rotation = Quaternion::fromEuler(pitch * DEG2RAD, yaw * DEG2RAD, roll * DEG2RAD);
            transformDirty = boundsDirty = true;
        }

        void setScale(const Vector3 &scl)
        {
            scale = scl;
            transformDirty = boundsDirty = true;
        }
        void setScale(float uniform)
        {
            setScale(Vector3(uniform, uniform, uniform));
        }
        void setScale(float x, float y, float z)
        {
            setScale(Vector3(x, y, z));
        }

        void rotate(const Quaternion &deltaRot)
        {
            rotation = rotation * deltaRot;
            transformDirty = true;
            boundsDirty = true;
        }

        void setColor(const Color &c) { instanceColor = c; }
        Color color() const { return instanceColor; }
        void show() { visible = true; }
        void hide() { visible = false; }
        bool isVisible() const { return visible && sourceMesh; }

        void updateTransform()
        {
            if (!transformDirty)
                return;
            transformDirty = false;

            rotation.toMatrix(localTransform);

            const float sx = scale.x;
            const float sy = scale.y;
            const float sz = scale.z;

            localTransform.m[0] *= sx;
            localTransform.m[1] *= sx;
            localTransform.m[2] *= sx;

            localTransform.m[4] *= sy;
            localTransform.m[5] *= sy;
            localTransform.m[6] *= sy;

            localTransform.m[8] *= sz;
            localTransform.m[9] *= sz;
            localTransform.m[10] *= sz;

            localTransform.m[12] = position.x;
            localTransform.m[13] = position.y;
            localTransform.m[14] = position.z;
        }

        Vector3 center()
        {
            if (unlikely(boundsDirty))
            {
                updateBounds();
            }
            return cachedWorldCenter;
        }

        float radius()
        {
            if (unlikely(boundsDirty))
            {
                updateBounds();
            }
            return cachedWorldRadius;
        }

    private:
        void updateBounds()
        {
            if (!sourceMesh)
            {
                LOGW(::pip3D::Debug::LOG_MODULE_SCENE,
                     "MeshInstance::updateBounds called with null sourceMesh");
                cachedWorldCenter = position;
                cachedWorldRadius = 0;
                boundsDirty = false;
                return;
            }

            updateTransform();

            const Vector3 localMeshCenter = sourceMesh->center();
            const float localMeshRadius = sourceMesh->radius();

            cachedWorldCenter = localTransform.transformNoDiv(localMeshCenter);

            const float maxScale = fmaxf(fmaxf(scale.x, scale.y), scale.z);
            cachedWorldRadius = localMeshRadius * maxScale;
            boundsDirty = false;
        }

    public:
        const Vector3 &pos() const { return position; }
        const Quaternion &rot() const { return rotation; }
        const Vector3 &scl() const { return scale; }
        const Matrix4x4 &transform()
        {
            updateTransform();
            return localTransform;
        }

        MeshInstance *at(float x, float y, float z)
        {
            setPosition(x, y, z);
            return this;
        }
        MeshInstance *at(const Vector3 &pos)
        {
            setPosition(pos);
            return this;
        }
        MeshInstance *euler(float pitch, float yaw, float roll)
        {
            setEuler(pitch, yaw, roll);
            return this;
        }
        MeshInstance *size(float s)
        {
            setScale(s);
            return this;
        }
        MeshInstance *size(float x, float y, float z)
        {
            setScale(x, y, z);
            return this;
        }
        MeshInstance *color(const Color &c)
        {
            setColor(c);
            return this;
        }
    };

    class InstanceManager
    {
    private:
        std::vector<MeshInstance *> instances;
        std::vector<MeshInstance *> pool;

    public:
        InstanceManager()
        {
        }

        ~InstanceManager()
        {
            destroyAll();
        }

        void destroyAll()
        {
            for (auto *inst : instances)
            {
                delete inst;
            }
            instances.clear();

            for (auto *inst : pool)
            {
                delete inst;
            }
            pool.clear();

            instances.shrink_to_fit();
            pool.shrink_to_fit();
        }

        MeshInstance *create(Mesh *mesh)
        {
            MeshInstance *inst;
            if (!pool.empty())
            {
                inst = pool.back();
                pool.pop_back();
                inst->reset(mesh);
            }
            else
            {
                inst = new MeshInstance(mesh);
            }
            inst->managerIndex = instances.size();
            instances.push_back(inst);
            return inst;
        }

        std::vector<MeshInstance *> batch(Mesh *mesh, size_t count)
        {
            std::vector<MeshInstance *> result;
            result.reserve(count);

            for (size_t i = 0; i < count; i++)
            {
                result.push_back(create(mesh));
            }

            return result;
        }

        void remove(MeshInstance *inst)
        {
            if (unlikely(!inst))
            {
                LOGW(::pip3D::Debug::LOG_MODULE_SCENE,
                     "InstanceManager::remove called with null instance");
                return;
            }

            size_t index = inst->managerIndex;
            if (index >= instances.size() || instances[index] != inst)
            {
                size_t found = instances.size();
                for (size_t i = 0; i < instances.size(); ++i)
                {
                    if (instances[i] == inst)
                    {
                        found = i;
                        break;
                    }
                }
                if (found == instances.size())
                {
                    LOGW(::pip3D::Debug::LOG_MODULE_SCENE,
                         "InstanceManager::remove: instance not found in manager (instances=%u)",
                         static_cast<unsigned int>(instances.size()));
                    return;
                }
                index = found;
            }

            MeshInstance *last = instances.back();
            instances[index] = last;
            last->managerIndex = index;
            instances.pop_back();
            inst->managerIndex = (size_t)-1;
            pool.push_back(inst);
        }

        const std::vector<MeshInstance *> &all() const
        {
            return instances;
        }

        void cull(const Frustum &frustum, std::vector<MeshInstance *> &result) const
        {
            result.clear();

            for (auto *inst : instances)
            {
                if (likely(inst->isVisible() && frustum.sphere(inst->center(), inst->radius())))
                {
                    result.push_back(inst);
                }
            }
        }

        void clear()
        {
            for (auto *inst : instances)
            {
                inst->managerIndex = (size_t)-1;
                pool.push_back(inst);
            }
            instances.clear();
        }

        void hideAll()
        {
            for (auto *inst : instances)
            {
                inst->hide();
            }
        }

        void showAll()
        {
            for (auto *inst : instances)
            {
                inst->show();
            }
        }

        void tint(const Color &color)
        {
            for (auto *inst : instances)
            {
                inst->setColor(color);
            }
        }

        MeshInstance *spawn(Mesh *mesh, float x, float y, float z)
        {
            MeshInstance *inst = create(mesh);
            inst->at(x, y, z);
            return inst;
        }

        void sort(const Vector3 &cameraPos, std::vector<MeshInstance *> &insts)
        {
            std::sort(insts.begin(), insts.end(),
                      [&cameraPos](const MeshInstance *a, const MeshInstance *b)
                      {
                          float distA = (a->pos() - cameraPos).lengthSquared();
                          float distB = (b->pos() - cameraPos).lengthSquared();
                          return distA < distB;
                      });
        }

        size_t count() const { return instances.size(); }
    };

}

#endif
