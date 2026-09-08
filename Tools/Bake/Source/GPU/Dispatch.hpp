#pragma once

#include <cstdint>
#include <vector>

#include "Core/Config.hpp"
#include "Core/Data.hpp"
#include "Trace/Bvh.hpp"
#include "Scene/Collect.hpp"

namespace pip3D
{
    namespace Bake
    {
        namespace Gpu
        {

            struct GpuJob
            {
                const BakeConfig *cfg = nullptr;
                const BakeBVH *bvh = nullptr;
                InstanceBakeData *ib = nullptr;

                uint32_t rw = 0;
                uint32_t rh = 0;

                const SceneLighting *light = nullptr;
                Vector3 sunT1 = Vector3(0, 0, 0);
                Vector3 sunT2 = Vector3(0, 0, 0);

                const std::vector<EmissiveTriLight> *emissives = nullptr;
                float emissiveTotalArea = 0.0f;

                bool finalMode = false;

                const char *progressStatus = nullptr;
            };

            struct VulkanImpl;

            class GpuBaker
            {
            public:
                GpuBaker() noexcept = default;
                ~GpuBaker();

                GpuBaker(const GpuBaker &) = delete;
                GpuBaker &operator=(const GpuBaker &) = delete;

                bool init();

                [[nodiscard]] bool ok() const noexcept { return m_vkImpl != nullptr; }

                void shutdown() noexcept;

                bool bakeTexels(const BakeConfig &cfg, const BakeBVH &bvh,
                                InstanceBakeData &ib, uint32_t rw, uint32_t rh,
                                const SceneLighting &light,
                                const Vector3 &sunT1, const Vector3 &sunT2,
                                const std::vector<EmissiveTriLight> &emissives,
                                float emissiveTotalArea,
                                bool finalMode,
                                const char *progressStatus = nullptr);

            private:
                VulkanImpl *m_vkImpl = nullptr;
            };

            GpuBaker &globalGpuBaker() noexcept;

        }
    }
}
