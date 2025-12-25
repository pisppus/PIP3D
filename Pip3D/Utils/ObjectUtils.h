#ifndef OBJECTUTILS_H
#define OBJECTUTILS_H

#include "../Rendering/Renderer.h"
#include "../Geometry/PrimitiveShapes.h"

namespace pip3D
{

    class ObjectHelper
    {
    public:
        template <typename T>
        __attribute__((always_inline, hot)) static inline void renderWithShadow(Renderer *renderer, T *object)
        {
            if (!object || !renderer)
                return;

            if (renderer->getShadowsEnabled() && object->getCastShadows())
            {
                renderer->drawMeshShadow(object);
            }

            renderer->drawMesh(object);
        }

        template <typename T>
        __attribute__((always_inline, hot)) static inline void renderMultipleWithShadows(Renderer *renderer, T **objects, int count)
        {
            if (!renderer || !objects)
                return;

            if (renderer->getShadowsEnabled())
            {
                for (int i = 0; i < count; i++)
                {
                    if (objects[i])
                    {
                        renderer->drawMeshShadow(objects[i]);
                    }
                }
            }

            for (int i = 0; i < count; i++)
            {
                if (objects[i])
                {
                    renderer->drawMesh(objects[i]);
                }
            }
        }
    };

}

#endif
