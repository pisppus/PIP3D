#ifndef PIP3D_CHARACTERCONTROLLER_H
#define PIP3D_CHARACTERCONTROLLER_H

#include "../Math/Math.h"
#include "../Core/Instance.h"
#include "../Core/Camera.h"
#include "../Geometry/PrimitiveShapes.h"
#include "SceneNode.h"

namespace pip3D
{

    struct CharacterInput
    {
        float moveX;
        float moveY;
        bool jump;
        bool sprint;

        CharacterInput()
            : moveX(0.0f), moveY(0.0f), jump(false), sprint(false) {}
    };

    class CharacterController
    {
    private:
        Vector3 position;
        Vector3 velocity;
        float yaw;
        float height;
        float radius;
        float moveSpeed;
        float sprintMultiplier;
        float jumpSpeed;
        float gravity;
        bool onGround;
        MeshInstance *visual;

        Node *visualRoot;
        Node *visualBody;
        Node *visualHead;
        Node *visualArmL;
        Node *visualArmR;
        Node *visualLegL;
        Node *visualLegR;
        float walkTime;
        float walkAmount;
        bool ownsVisualNodes;

    public:
        CharacterController()
            : position(0.0f, 0.9f, 0.0f),
              velocity(0.0f, 0.0f, 0.0f),
              yaw(0.0f),
              height(1.8f),
              radius(0.9f),
              moveSpeed(5.0f),
              sprintMultiplier(2.0f),
              jumpSpeed(4.0f),
              gravity(-9.81f),
              onGround(true),
              visual(nullptr),
              visualRoot(nullptr),
              visualBody(nullptr),
              visualHead(nullptr),
              visualArmL(nullptr),
              visualArmR(nullptr),
              visualLegL(nullptr),
              visualLegR(nullptr),
              walkTime(0.0f),
              walkAmount(0.0f),
              ownsVisualNodes(false)
        {
            initDefaultVisual();
        }

        ~CharacterController()
        {
            if (ownsVisualNodes && visualRoot)
            {
                delete visualRoot;
                visualRoot = nullptr;
            }
        }

        void setVisual(MeshInstance *mesh)
        {
            visual = mesh;
            if (visual)
            {
                visual->setPosition(position);
            }
        }

        void setVisualRig(Node *root,
                          Node *body,
                          Node *head,
                          Node *armL,
                          Node *armR,
                          Node *legL,
                          Node *legR)
        {
            if (ownsVisualNodes && visualRoot && visualRoot != root)
            {
                delete visualRoot;
            }

            visualRoot = root;
            visualBody = body;
            visualHead = head;
            visualArmL = armL;
            visualArmR = armR;
            visualLegL = legL;
            visualLegR = legR;
            ownsVisualNodes = false;

            if (visualRoot)
            {
                visualRoot->setPosition(position);
                visualRoot->setRotation(0.0f, yaw, 0.0f);
            }
        }

        void initDefaultVisual()
        {
            if (visualRoot)
                return;

            float bodyScaleX = 0.6f;
            float bodyScaleY = 1.0f;
            float bodyScaleZ = 0.3f;

            float headScale = 0.65f;

            float armScaleX = 0.25f;
            float armScaleY = 0.8f;
            float armScaleZ = 0.25f;

            float legScaleX = 0.3f;
            float legScaleY = 0.9f;
            float legScaleZ = 0.3f;

            visualRoot = new Node("CharacterRoot");

            MeshNode *bodyNode = new MeshNode(new Cube(1.0f, Color::fromRGB888(60, 110, 190)), "CharBody", true);
            MeshNode *headNode = new MeshNode(new Cube(1.0f, Color::fromRGB888(235, 210, 185)), "CharHead", true);
            MeshNode *armLNode = new MeshNode(new Cube(1.0f, Color::fromRGB888(50, 80, 170)), "CharArmL", true);
            MeshNode *armRNode = new MeshNode(new Cube(1.0f, Color::fromRGB888(50, 80, 170)), "CharArmR", true);
            MeshNode *legLNode = new MeshNode(new Cube(1.0f, Color::fromRGB888(30, 40, 90)), "CharLegL", true);
            MeshNode *legRNode = new MeshNode(new Cube(1.0f, Color::fromRGB888(30, 40, 90)), "CharLegR", true);

            Node *armLPivot = new Node("CharArmL_Pivot");
            Node *armRPivot = new Node("CharArmR_Pivot");
            Node *legLPivot = new Node("CharLegL_Pivot");
            Node *legRPivot = new Node("CharLegR_Pivot");

            visualBody = bodyNode;
            visualHead = headNode;
            visualArmL = armLPivot;
            visualArmR = armRPivot;
            visualLegL = legLPivot;
            visualLegR = legRPivot;

            visualRoot->addChild(visualBody);

            visualBody->addChild(visualHead);

            visualBody->addChild(armLPivot);
            visualBody->addChild(armRPivot);
            visualBody->addChild(legLPivot);
            visualBody->addChild(legRPivot);

            armLPivot->addChild(armLNode);
            armRPivot->addChild(armRNode);
            legLPivot->addChild(legLNode);
            legRPivot->addChild(legRNode);

            visualBody->setScale(bodyScaleX, bodyScaleY, bodyScaleZ);
            visualBody->setPosition(0.0f, 0.5f, 0.0f);

            visualHead->setScale(headScale, headScale, headScale);
            visualHead->setPosition(0.0f, 0.8f, 0.0f);

            armLNode->setScale(armScaleX, armScaleY, armScaleZ);
            armRNode->setScale(armScaleX, armScaleY, armScaleZ);

            legLNode->setScale(legScaleX, legScaleY, legScaleZ);
            legRNode->setScale(legScaleX, legScaleY, legScaleZ);

            float bodyHalfHeight = bodyScaleY * 0.5f;
            float bodyHalfWidth = bodyScaleX * 0.5f;
            float armHalfWidth = armScaleX * 0.5f;
            float armHalfLength = armScaleY * 0.5f;
            float legHalfLength = legScaleY * 0.5f;

            float shoulderYWorld = bodyHalfHeight * 0.9f;
            float shoulderXWorld = bodyHalfWidth + armHalfWidth + 0.02f;

            float shoulderYLocal = shoulderYWorld / bodyScaleY;
            float shoulderXLocal = shoulderXWorld / bodyScaleX;

            visualArmL->setPosition(-shoulderXLocal, shoulderYLocal, 0.0f);
            visualArmR->setPosition(shoulderXLocal, shoulderYLocal, 0.0f);

            armLNode->setPosition(0.0f, -armHalfLength, 0.0f);
            armRNode->setPosition(0.0f, -armHalfLength, 0.0f);

            float hipYWorld = -bodyHalfHeight;
            float hipOffsetXWorld = 0.18f * bodyScaleX;

            float hipYLocal = hipYWorld / bodyScaleY;
            float hipOffsetXLocal = hipOffsetXWorld / bodyScaleX;

            visualLegL->setPosition(-hipOffsetXLocal, hipYLocal, 0.0f);
            visualLegR->setPosition(hipOffsetXLocal, hipYLocal, 0.0f);

            legLNode->setPosition(0.0f, -legHalfLength, 0.0f);
            legRNode->setPosition(0.0f, -legHalfLength, 0.0f);

            ownsVisualNodes = true;

            if (visualRoot)
            {
                visualRoot->setPosition(position);
                visualRoot->setRotation(0.0f, yaw, 0.0f);
            }
        }

        void setPosition(const Vector3 &pos)
        {
            position = pos;
            if (visual)
                visual->setPosition(pos);
            if (visualRoot)
                visualRoot->setPosition(pos);
        }

        const Vector3 &getPosition() const { return position; }

        float getYaw() const { return yaw; }

        void setYaw(float y)
        {
            yaw = y;
        }

        void update(float deltaTime, const CharacterInput &input, const Camera &camera)
        {
            (void)camera;

            Vector3 moveDir(input.moveX, 0.0f, input.moveY);
            float moveLenSq = moveDir.lengthSquared();
            if (moveLenSq > 1e-5f)
            {
                float invLen = FastMath::fastInvSqrt(moveLenSq);
                moveDir *= invLen;

                float targetYaw = atan2f(moveDir.x, moveDir.z) * RAD2DEG;
                float yawDelta = targetYaw - yaw;
                while (yawDelta > 180.0f)
                    yawDelta -= 360.0f;
                while (yawDelta < -180.0f)
                    yawDelta += 360.0f;
                float maxYawChange = 720.0f * deltaTime;
                if (yawDelta > maxYawChange)
                    yawDelta = maxYawChange;
                if (yawDelta < -maxYawChange)
                    yawDelta = -maxYawChange;
                yaw += yawDelta;
            }

            float baseSpeed = moveSpeed * (input.sprint ? sprintMultiplier : 1.0f);

            Vector3 desiredVel(0.0f, velocity.y, 0.0f);
            desiredVel.x = moveDir.x * baseSpeed;
            desiredVel.z = moveDir.z * baseSpeed;

            velocity.x = desiredVel.x;
            velocity.z = desiredVel.z;

            if (onGround)
            {
                if (input.jump)
                {
                    velocity.y = jumpSpeed;
                    onGround = false;
                }
                else
                {
                    velocity.y = 0.0f;
                }
            }
            else
            {
                velocity.y += gravity * deltaTime;
            }

            position += velocity * deltaTime;

            float minY = radius;
            if (position.y < minY)
            {
                position.y = minY;
                velocity.y = 0.0f;
                onGround = true;
            }

            if (visual)
            {
                visual->setPosition(position);
                visual->setEuler(0.0f, yaw, 0.0f);
            }

            if (visualRoot)
            {
                visualRoot->setPosition(position);
                visualRoot->setRotation(0.0f, yaw, 0.0f);

                if (visualBody)
                {
                    const Vector3 &bodyRot = visualBody->getRotation();
                    visualBody->setRotation(bodyRot.x, yaw, bodyRot.z);
                }

                bool isMoving = moveLenSq > 1e-4f;
                float targetAmount = isMoving ? 1.0f : 0.0f;
                const float fadeSpeed = 8.0f;

                if (walkAmount < targetAmount)
                {
                    walkAmount += fadeSpeed * deltaTime;
                    if (walkAmount > targetAmount)
                        walkAmount = targetAmount;
                }
                else if (walkAmount > targetAmount)
                {
                    walkAmount -= fadeSpeed * deltaTime;
                    if (walkAmount < targetAmount)
                        walkAmount = targetAmount;
                }

                if (isMoving)
                {
                    float speedFactor = moveSpeed > 0.0f ? (baseSpeed / moveSpeed) : 1.0f;
                    if (speedFactor < 0.5f)
                        speedFactor = 0.5f;
                    walkTime += deltaTime * 6.0f * speedFactor;
                }

                if (walkAmount < 0.001f)
                {
                    if (visualArmL)
                        visualArmL->setRotation(0.0f, 0.0f, 0.0f);
                    if (visualArmR)
                        visualArmR->setRotation(0.0f, 0.0f, 0.0f);
                    if (visualLegL)
                        visualLegL->setRotation(0.0f, 0.0f, 0.0f);
                    if (visualLegR)
                        visualLegR->setRotation(0.0f, 0.0f, 0.0f);
                    if (visualHead)
                        visualHead->setRotation(0.0f, yaw, 0.0f);
                }
                else
                {
                    float phase = walkTime;
                    float armSwing = sinf(phase) * 30.0f * walkAmount;
                    float legSwing = sinf(phase) * 35.0f * walkAmount;
                    float armSwingOpp = sinf(phase + PI) * 30.0f * walkAmount;
                    float legSwingOpp = sinf(phase + PI) * 35.0f * walkAmount;

                    if (visualArmL)
                        visualArmL->setRotation(armSwing, 0.0f, 0.0f);
                    if (visualArmR)
                        visualArmR->setRotation(armSwingOpp, 0.0f, 0.0f);
                    if (visualLegL)
                        visualLegL->setRotation(legSwingOpp, 0.0f, 0.0f);
                    if (visualLegR)
                        visualLegR->setRotation(legSwing, 0.0f, 0.0f);

                    if (visualHead)
                    {
                        float headBob = sinf(phase * 2.0f) * 5.0f * walkAmount;
                        visualHead->setRotation(headBob, yaw, 0.0f);
                    }
                }
            }
        }

        void applyToCamera(Camera &cam, const Vector3 &offset) const
        {
            float yawRad = yaw * DEG2RAD;
            float s = sinf(yawRad);
            float c = cosf(yawRad);

            Vector3 localOffset(
                offset.x * c + offset.z * s,
                offset.y,
                -offset.x * s + offset.z * c);

            Vector3 camPos = position + localOffset;
            // Направление вперёд по yaw (то же, что направление движения)
            Vector3 forward(s, 0.0f, c);

            // Смотрим немного вперёд от персонажа, чтобы пол был хорошо виден
            Vector3 target = position + forward * 1.5f + Vector3(0.0f, height * 0.5f, 0.0f);

            cam.position = camPos;
            cam.target = target;
            cam.up = Vector3(0.0f, 1.0f, 0.0f);
            cam.markDirty();
        }

        void applyFirstPerson(Camera &cam) const
        {
            float yawRad = yaw * DEG2RAD;
            float s = sinf(yawRad);
            float c = cosf(yawRad);

            Vector3 forward(s, 0.0f, c);

            // Точка глаз чуть выше верхушки сферы-персонажа
            Vector3 eye = position + Vector3(0.0f, radius + 0.1f, 0.0f);

            cam.position = eye;
            cam.target = eye + forward * 3.0f;
            cam.up = Vector3(0.0f, 1.0f, 0.0f);
            cam.markDirty();
        }

        void render(class Renderer *renderer)
        {
            if (!renderer)
                return;

            if (visualRoot)
            {
                visualRoot->render(renderer);
            }
        }
    };

}

#endif
