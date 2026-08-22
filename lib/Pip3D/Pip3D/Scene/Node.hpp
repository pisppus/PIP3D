#pragma once

#include "Core/Platform.hpp"
#include "Camera/Camera.hpp"
#include "Math/Algebra.hpp"
#include "Geometry/Mesh.hpp"
#include "Geometry/Instance.hpp"
#include "Rendering/Lighting/Lighting.hpp"
#include <vector>
#include <string>
#include <algorithm>

namespace pip3D
{
    class Node
    {
    protected:
        std::string name;
        bool visible;
        bool enabled;

        Vector3 position;
        Vector3 rotation;
        Vector3 scale;

        Node *parent;
        std::vector<Node *> children;

        Matrix4x4 localTransform;
        Matrix4x4 worldTransform;
        bool transformDirty;

    public:
        Node(const std::string &nodeName = "Node")
            : name(nodeName), visible(true), enabled(true),
              position(0, 0, 0), rotation(0, 0, 0), scale(1, 1, 1),
              parent(nullptr), transformDirty(true)
        {
            localTransform.reset();
            worldTransform.reset();
        }

        virtual ~Node()
        {
            for (Node *child : children)
                delete child;
            children.clear();
        }

        void addChild(Node *child)
        {
            if (!child)
            {
                LOGW(::pip3D::Debug::LOG_MODULE_SCENE,
                     "Node::addChild called with null child for node '%s'",
                     name.c_str());
                return;
            }

            if (child->parent)
                child->parent->removeChild(child);

            child->parent = this;
            children.push_back(child);
            child->markTransformDirty();
        }

        void removeChild(Node *child)
        {
            if (!child)
            {
                LOGW(::pip3D::Debug::LOG_MODULE_SCENE,
                     "Node::removeChild called with null child for node '%s'",
                     name.c_str());
                return;
            }

            auto it = std::find(children.begin(), children.end(), child);
            if (it != children.end())
            {
                (*it)->parent = nullptr;
                children.erase(it);
            }
            else
            {
                LOGW(::pip3D::Debug::LOG_MODULE_SCENE,
                     "Node::removeChild: child not found under node '%s'",
                     name.c_str());
            }
        }

        Node *getChild(size_t index) const
        {
            if (index < children.size())
                return children[index];
            LOGW(::pip3D::Debug::LOG_MODULE_SCENE,
                 "Node::getChild index out of range (index=%u, count=%u) for node '%s'",
                 static_cast<unsigned int>(index),
                 static_cast<unsigned int>(children.size()),
                 name.c_str());
            return nullptr;
        }

        Node *findChild(const std::string &childName) const
        {
            for (Node *child : children)
                if (child->name == childName)
                    return child;
            return nullptr;
        }

        size_t getChildCount() const { return children.size(); }
        Node *getParent() const { return parent; }

        void setPosition(float x, float y, float z)
        {
            position.x = x;
            position.y = y;
            position.z = z;
            markTransformDirty();
        }

        void setPosition(const Vector3 &pos)
        {
            position = pos;
            markTransformDirty();
        }

        void setRotation(float x, float y, float z)
        {
            rotation.x = x;
            rotation.y = y;
            rotation.z = z;
            markTransformDirty();
        }

        void setRotation(const Vector3 &rot)
        {
            rotation = rot;
            markTransformDirty();
        }

        void setScale(float x, float y, float z)
        {
            scale.x = x;
            scale.y = y;
            scale.z = z;
            markTransformDirty();
        }

        void setScale(const Vector3 &scl)
        {
            scale = scl;
            markTransformDirty();
        }

        void setScale(float uniformScale)
        {
            scale.x = uniformScale;
            scale.y = uniformScale;
            scale.z = uniformScale;
            markTransformDirty();
        }

        void translate(float x, float y, float z)
        {
            position.x += x;
            position.y += y;
            position.z += z;
            markTransformDirty();
        }

        void translate(const Vector3 &offset)
        {
            position = position + offset;
            markTransformDirty();
        }

        void rotate(float x, float y, float z)
        {
            rotation.x += x;
            rotation.y += y;
            rotation.z += z;
            markTransformDirty();
        }

        const Vector3 &getPosition() const { return position; }
        const Vector3 &getRotation() const { return rotation; }
        const Vector3 &getScale() const { return scale; }

        Vector3 getWorldPosition()
        {
            updateWorldTransform();
            return Vector3(worldTransform.m[12], worldTransform.m[13], worldTransform.m[14]);
        }

        void updateLocalTransform()
        {
            Matrix4x4 T, R, S;

            T.reset();
            T.m[12] = position.x;
            T.m[13] = position.y;
            T.m[14] = position.z;

            const float radX = rotation.x * kDegToRad;
            const float radY = rotation.y * kDegToRad;
            const float radZ = rotation.z * kDegToRad;

            const float cx = cosf(radX), sx = sinf(radX);
            const float cy = cosf(radY), sy = sinf(radY);
            const float cz = cosf(radZ), sz = sinf(radZ);

            R.reset();
            R.m[0] = cy * cz;
            R.m[1] = cy * sz;
            R.m[2] = -sy;

            R.m[4] = sx * sy * cz - cx * sz;
            R.m[5] = sx * sy * sz + cx * cz;
            R.m[6] = sx * cy;

            R.m[8] = cx * sy * cz + sx * sz;
            R.m[9] = cx * sy * sz - sx * cz;
            R.m[10] = cx * cy;

            S.reset();
            S.m[0] = scale.x;
            S.m[5] = scale.y;
            S.m[10] = scale.z;

            localTransform = T * R * S;
        }

        void updateWorldTransform()
        {
            if (transformDirty)
            {
                updateLocalTransform();

                if (parent)
                {
                    parent->updateWorldTransform();
                    worldTransform = parent->worldTransform * localTransform;
                }
                else
                {
                    worldTransform = localTransform;
                }

                transformDirty = false;
            }
        }

        const Matrix4x4 &getWorldTransform()
        {
            updateWorldTransform();
            return worldTransform;
        }

        const Matrix4x4 &getLocalTransform()
        {
            if (transformDirty)
                updateLocalTransform();
            return localTransform;
        }

        void markTransformDirty()
        {
            transformDirty = true;
            for (Node *child : children)
                child->markTransformDirty();
        }

        void setVisible(bool vis) { visible = vis; }
        bool isVisible() const { return visible; }

        void setEnabled(bool en) { enabled = en; }
        bool isEnabled() const { return enabled; }

        void setName(const std::string &newName) { name = newName; }
        const std::string &getName() const { return name; }

        virtual void update(float deltaTime)
        {
            if (!enabled)
                return;
            for (Node *child : children)
                child->update(deltaTime);
        }

        virtual void render(class Renderer *renderer)
        {
            if (!visible || !enabled)
                return;
            for (Node *child : children)
                child->render(renderer);
        }
    };

    inline void decomposeWorldTransform(const Matrix4x4 &world,
                                        Vector3 &position,
                                        Vector3 &scaleOut,
                                        Matrix4x4 &basisOut)
    {
        position = Vector3(world.m[12], world.m[13], world.m[14]);

        Vector3 axisX(world.m[0], world.m[1], world.m[2]);
        Vector3 axisY(world.m[4], world.m[5], world.m[6]);
        Vector3 axisZ(world.m[8], world.m[9], world.m[10]);

        scaleOut = Vector3(axisX.length(), axisY.length(), axisZ.length());

        if (scaleOut.x > 1e-6f)
            axisX *= FastMath::fastReciprocal(scaleOut.x);
        else
            axisX = Vector3(1.0f, 0.0f, 0.0f);

        if (scaleOut.y > 1e-6f)
            axisY *= FastMath::fastReciprocal(scaleOut.y);
        else
            axisY = Vector3(0.0f, 1.0f, 0.0f);

        if (scaleOut.z > 1e-6f)
            axisZ *= FastMath::fastReciprocal(scaleOut.z);
        else
            axisZ = Vector3(0.0f, 0.0f, 1.0f);

        basisOut.reset();
        basisOut.m[0] = axisX.x;
        basisOut.m[1] = axisX.y;
        basisOut.m[2] = axisX.z;
        basisOut.m[4] = axisY.x;
        basisOut.m[5] = axisY.y;
        basisOut.m[6] = axisY.z;
        basisOut.m[8] = axisZ.x;
        basisOut.m[9] = axisZ.y;
        basisOut.m[10] = axisZ.z;
    }

    inline Vector3 basisToEulerDegrees(const Matrix4x4 &basis)
    {
        const float sy = clamp(-basis.m[2], -1.0f, 1.0f);
        const float yaw = asinf(sy);
        const float cy = cosf(yaw);

        float pitch;
        float roll;

        if (fabsf(cy) > 1e-5f)
        {
            pitch = atan2f(basis.m[6], basis.m[10]);
            roll = atan2f(basis.m[1], basis.m[0]);
        }
        else
        {
            pitch = atan2f(-basis.m[9], basis.m[5]);
            roll = 0.0f;
        }

        return Vector3(pitch * kRadToDeg, yaw * kRadToDeg, roll * kRadToDeg);
    }

    class MeshNode : public Node
    {
    protected:
        Mesh *mesh;
        bool ownsMesh;
        MeshInstance *instance;

    public:
        MeshNode(const std::string &nodeName = "MeshNode")
            : Node(nodeName), mesh(nullptr), ownsMesh(false), instance(nullptr) {}

        MeshNode(Mesh *meshPtr, const std::string &nodeName = "MeshNode", bool owns = false)
            : Node(nodeName), mesh(nullptr), ownsMesh(false), instance(nullptr)
        {
            setMesh(meshPtr, owns);
        }

        ~MeshNode() override
        {
            delete instance;
            instance = nullptr;
            if (ownsMesh && mesh)
            {
                Mesh::destroy(mesh);
                mesh = nullptr;
            }
        }

        void setMesh(Mesh *meshPtr, bool owns = false)
        {
            if (ownsMesh && mesh && mesh != meshPtr)
                Mesh::destroy(mesh);

            mesh = meshPtr;
            ownsMesh = owns;

            if (!instance)
                instance = new MeshInstance(mesh);
            else
                instance->setMesh(mesh);
        }

        Mesh *getMesh() const { return mesh; }
        MeshInstance *getInstance() const { return instance; }

        void render(class Renderer *renderer) override;
    };

    class CameraNode : public Node
    {
    protected:
        float fov;
        float nearPlane;
        float farPlane;

    public:
        CameraNode(const std::string &nodeName = "Camera")
            : Node(nodeName), fov(60.0f), nearPlane(0.1f), farPlane(100.0f) {}

        void setFOV(float fieldOfView) { fov = fieldOfView; }
        float getFOV() const { return fov; }

        void setNearPlane(float nearDist) { nearPlane = nearDist; }
        float getNearPlane() const { return nearPlane; }

        void setFarPlane(float farDist) { farPlane = farDist; }
        float getFarPlane() const { return farPlane; }

        void applyToCamera(class Camera &camera)
        {
            const Matrix4x4 &world = getWorldTransform();
            Vector3 worldPos;
            Vector3 worldScale;
            Matrix4x4 basis;
            decomposeWorldTransform(world, worldPos, worldScale, basis);

            Vector3 forward(basis.m[8], basis.m[9], basis.m[10]);
            Vector3 upVec(basis.m[4], basis.m[5], basis.m[6]);
            if (forward.lengthSquared() <= 1e-6f)
                forward = Vector3(0.0f, 0.0f, 1.0f);
            else
                forward.normalize();
            if (upVec.lengthSquared() <= 1e-6f)
                upVec = Vector3(0.0f, 1.0f, 0.0f);
            else
                upVec.normalize();

            camera.position = worldPos;
            camera.target = worldPos + forward;
            camera.up = upVec;
            camera.fov = fov;
            camera.nearPlane = nearPlane;
            camera.farPlane = farPlane;
            camera.markDirty();
        }
    };

    class LightNode : public Node
    {
    protected:
        LightType lightType;
        Color color;
        float intensity;
        Vector3 direction;
        float range;

    public:
        LightNode(const std::string &nodeName = "Light")
            : Node(nodeName), lightType(LIGHT_DIRECTIONAL), color(Color::WHITE),
              intensity(1.0f), direction(0, -1, 0), range(0.0f) {}

        void setLightType(LightType type) { lightType = type; }
        LightType getLightType() const { return lightType; }

        void setColor(const Color &col) { color = col; }
        const Color &getColor() const { return color; }

        void setIntensity(float inten) { intensity = inten; }
        float getIntensity() const { return intensity; }

        void setDirection(const Vector3 &dir) { direction = dir; }
        const Vector3 &getDirection() const { return direction; }

        void setRange(float r) { range = r; }
        float getRange() const { return range; }

        void applyToLight(Light &light)
        {
            light.type = lightType;
            light.color = color;
            light.intensity = intensity;
            light.setRange(range);
            light.colorCacheDirty = true;

            if (lightType == LIGHT_DIRECTIONAL)
            {
                light.direction = direction;
                light.direction.normalize();
            }
            else
            {
                light.position = getWorldPosition();
            }
        }
    };
}