#ifndef SCENENODE_H
#define SCENENODE_H

#include "../Core/Core.h"
#include "../Core/Camera.h"
#include "../Math/Math.h"
#include "../Geometry/Mesh.h"
#include "../Rendering/Lighting/Lighting.h"
#include <vector>
#include <algorithm>

namespace pip3D
{

    class Node
    {
    protected:
        String name;
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
        Node(const String &nodeName = "Node")
            : name(nodeName), visible(true), enabled(true), position(0, 0, 0), rotation(0, 0, 0), scale(1, 1, 1), parent(nullptr), transformDirty(true)
        {
            localTransform.identity();
            worldTransform.identity();
        }

        virtual ~Node()
        {
            for (Node *child : children)
            {
                delete child;
            }
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
            {
                child->parent->removeChild(child);
            }

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
            {
                return children[index];
            }
            LOGW(::pip3D::Debug::LOG_MODULE_SCENE,
                 "Node::getChild index out of range (index=%u, count=%u) for node '%s'",
                 static_cast<unsigned int>(index),
                 static_cast<unsigned int>(children.size()),
                 name.c_str());
            return nullptr;
        }

        Node *findChild(const String &childName) const
        {
            for (Node *child : children)
            {
                if (child->name == childName)
                {
                    return child;
                }
            }
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

            T.identity();
            T.m[12] = position.x;
            T.m[13] = position.y;
            T.m[14] = position.z;

            float radX = rotation.x * DEG_TO_RAD;
            float radY = rotation.y * DEG_TO_RAD;
            float radZ = rotation.z * DEG_TO_RAD;

            float cx = cos(radX), sx = sin(radX);
            float cy = cos(radY), sy = sin(radY);
            float cz = cos(radZ), sz = sin(radZ);

            R.identity();
            R.m[0] = cy * cz;
            R.m[1] = cy * sz;
            R.m[2] = -sy;

            R.m[4] = sx * sy * cz - cx * sz;
            R.m[5] = sx * sy * sz + cx * cz;
            R.m[6] = sx * cy;

            R.m[8] = cx * sy * cz + sx * sz;
            R.m[9] = cx * sy * sz - sx * cz;
            R.m[10] = cx * cy;

            S.identity();
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
            {
                updateLocalTransform();
            }
            return localTransform;
        }

        void markTransformDirty()
        {
            transformDirty = true;

            for (Node *child : children)
            {
                child->markTransformDirty();
            }
        }

        void setVisible(bool vis) { visible = vis; }
        bool isVisible() const { return visible; }

        void setEnabled(bool en) { enabled = en; }
        bool isEnabled() const { return enabled; }

        void setName(const String &newName) { name = newName; }
        const String &getName() const { return name; }

        virtual void update(float deltaTime)
        {
            if (!enabled)
                return;

            for (Node *child : children)
            {
                child->update(deltaTime);
            }
        }

        virtual void render(class Renderer *renderer)
        {
            if (!visible)
                return;

            for (Node *child : children)
            {
                child->render(renderer);
            }
        }
    };

    class MeshNode : public Node
    {
    protected:
        Mesh *mesh;
        bool ownsMesh;
        bool castShadows;

    public:
        MeshNode(const String &nodeName = "MeshNode")
            : Node(nodeName), mesh(nullptr), ownsMesh(false), castShadows(true) {}

        MeshNode(Mesh *meshPtr, const String &nodeName = "MeshNode", bool owns = false)
            : Node(nodeName), mesh(meshPtr), ownsMesh(owns), castShadows(true) {}

        ~MeshNode() override
        {
            if (ownsMesh && mesh)
            {
                delete mesh;
            }
        }

        void setMesh(Mesh *meshPtr, bool owns = false)
        {
            if (ownsMesh && mesh)
            {
                delete mesh;
            }
            mesh = meshPtr;
            ownsMesh = owns;
        }

        Mesh *getMesh() const { return mesh; }

        void setCastShadows(bool cast) { castShadows = cast; }
        bool getCastShadows() const { return castShadows; }

        void render(class Renderer *renderer) override;
    };

    class CameraNode : public Node
    {
    protected:
        float fov;
        float nearPlane;
        float farPlane;
        ProjectionType projectionType;

    public:
        CameraNode(const String &nodeName = "Camera")
            : Node(nodeName), fov(60.0f), nearPlane(0.1f), farPlane(100.0f), projectionType(PERSPECTIVE) {}

        void setFOV(float fieldOfView) { fov = fieldOfView; }
        float getFOV() const { return fov; }

        void setNearPlane(float near) { nearPlane = near; }
        float getNearPlane() const { return nearPlane; }

        void setFarPlane(float far) { farPlane = far; }
        float getFarPlane() const { return farPlane; }

        void setProjectionType(ProjectionType type) { projectionType = type; }
        ProjectionType getProjectionType() const { return projectionType; }

        void applyToCamera(class Camera &camera)
        {
            Vector3 worldPos = getWorldPosition();
            camera.position = worldPos;
            camera.fov = fov;
            camera.nearPlane = nearPlane;
            camera.farPlane = farPlane;
            camera.projectionType = projectionType;
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
        LightNode(const String &nodeName = "Light")
            : Node(nodeName), lightType(LIGHT_DIRECTIONAL), color(Color::WHITE), intensity(1.0f), direction(0, -1, 0), range(0.0f) {}

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
                Vector3 worldPos = getWorldPosition();
                light.position = worldPos;
            }
        }
    };

}

#endif
