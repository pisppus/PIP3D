#ifndef CAMERA_TIMELINE_H
#define CAMERA_TIMELINE_H

#include "../Core/Camera.h"

namespace pip3D
{

    struct CameraKeyframe
    {
        Vector3 position;
        Vector3 target;
        float fov;
        float duration;
        CameraAnimation::Type type;
        Vector3 up;

        CameraKeyframe(const Vector3 &pos = Vector3(),
                       const Vector3 &tgt = Vector3(),
                       float fovDegrees = 60.0f,
                       float dur = 1.0f,
                       CameraAnimation::Type t = CameraAnimation::SMOOTH,
                       const Vector3 &upVec = Vector3(0.0f, 1.0f, 0.0f))
            : position(pos), target(tgt), fov(fovDegrees), duration(dur), type(t), up(upVec)
        {
        }
    };

    class CameraTimeline
    {
        const CameraKeyframe *keys;
        int count;
        bool loop;
        int currentSegment;
        bool playing;

    public:
        CameraTimeline()
            : keys(nullptr), count(0), loop(false), currentSegment(-1), playing(false)
        {
        }

        void setTrack(const CameraKeyframe *k, int n, bool looped = false)
        {
            keys = k;
            count = n;
            loop = looped;
            currentSegment = -1;
            playing = false;
        }

        void start(Camera &cam)
        {
            if (!keys || count < 2)
            {
                if (keys && count == 1)
                {
                    applyKey(cam, keys[0]);
                }
                playing = false;
                return;
            }
            currentSegment = 0;
            applyKey(cam, keys[0]);
            setupAnim(cam, keys[0], keys[1]);
            playing = true;
        }

        void update(Camera &cam, float dt)
        {
            if (!playing || !keys || count < 2)
                return;

            cam.updateAnim(dt);
            if (!cam.isAnimating())
            {
                advance(cam);
            }
        }

        bool isPlaying() const { return playing; }

    private:
        static void applyKey(Camera &cam, const CameraKeyframe &k)
        {
            cam.position = k.position;
            cam.target = k.target;
            cam.up = k.up;
            cam.fov = k.fov;
            cam.markDirty();
        }

        static void setupAnim(Camera &cam, const CameraKeyframe &from, const CameraKeyframe &to)
        {
            CameraAnimation &a = cam.anim;
            a.startPos = from.position;
            a.startTgt = from.target;
            a.startUp = from.up;
            a.startFov = from.fov;
            a.targetPos = to.position;
            a.targetTgt = to.target;
            a.targetUp = to.up;
            a.targetFov = to.fov;
            a.duration = from.duration;
            a.invDuration = (from.duration > 0.0f) ? (1.0f / from.duration) : 0.0f;
            a.time = 0.0f;
            a.type = from.type;
            a.active = (from.duration > 0.0f);
        }

        void advance(Camera &cam)
        {
            if (!keys || count < 2)
            {
                playing = false;
                return;
            }

            currentSegment++;
            if (currentSegment >= count - 1)
            {
                if (!loop)
                {
                    playing = false;
                    return;
                }
                currentSegment = 0;
                applyKey(cam, keys[0]);
            }

            setupAnim(cam, keys[currentSegment], keys[currentSegment + 1]);
        }
    };

} // namespace pip3D

#endif
