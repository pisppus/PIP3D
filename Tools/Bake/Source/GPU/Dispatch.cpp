#include <cstdlib>
#include <cstring>
#include <cstdio>

#include "GPU/Dispatch.hpp"
#include "Core/Progress.hpp"

namespace pip3D
{
    namespace Bake
    {
        namespace Gpu
        {
            namespace detailVk
            {
                bool vkInit(VulkanImpl *&impl) noexcept;
                void vkShutdown(VulkanImpl *&impl) noexcept;
                const char *vkDeviceName(const VulkanImpl *impl) noexcept;
            }

            bool gpuDispatchVk(VulkanImpl *impl, const GpuJob &job) noexcept;

            GpuBaker::~GpuBaker()
            {
                shutdown();
            }

            bool GpuBaker::init()
            {
                if (m_vkImpl)
                    return true;

                const char *sel = std::getenv("PIP3D_BAKE_GPU");
                if (sel && *sel && std::strcmp(sel, "auto") != 0 &&
                    std::strcmp(sel, "vulkan") != 0 && std::strcmp(sel, "VULKAN") != 0 &&
                    std::strcmp(sel, "vk") != 0)
                {
                    std::printf("\033[33m[!] PIP3D_BAKE_GPU=%s - only 'auto'/'vulkan' exist, baking on Vulkan\033[0m\n",
                                sel);
                }

                VulkanImpl *vk = nullptr;
                if (detailVk::vkInit(vk))
                {
                    m_vkImpl = vk;
                    std::printf("\033[36m[Pip3D]\033[0m GPU bake: \033[32m%s\033[0m (Vulkan compute)\n",
                                detailVk::vkDeviceName(m_vkImpl));
                    return true;
                }
                std::printf("\033[91m[-] GPU bake: Vulkan compute unavailable.\033[0m\n"
                            "    Install/update a GPU driver with Vulkan 1.0+ (vulkan-1.dll / libvulkan.so.1)\n"
                            "    and ensure the device is not busy.\n");
                return false;
            }

            void GpuBaker::shutdown() noexcept
            {
                if (m_vkImpl)
                    detailVk::vkShutdown(m_vkImpl);
            }

            bool GpuBaker::bakeTexels(const BakeConfig &cfg, const BakeBVH &bvh,
                                      InstanceBakeData &ib, uint32_t rw, uint32_t rh,
                                      const SceneLighting &light,
                                      const Vector3 &sunT1, const Vector3 &sunT2,
                                      const std::vector<EmissiveTriLight> &emissives,
                                      float emissiveTotalArea,
                                      bool finalMode,
                                      const char *progressStatus)
            {
                if (!m_vkImpl)
                    return false;

                GpuJob job;
                job.cfg = &cfg;
                job.bvh = &bvh;
                job.ib = &ib;
                job.rw = rw;
                job.rh = rh;
                job.light = &light;
                job.sunT1 = sunT1;
                job.sunT2 = sunT2;
                job.emissives = &emissives;
                job.emissiveTotalArea = emissiveTotalArea;
                job.finalMode = finalMode;
                job.progressStatus = progressStatus;

                return gpuDispatchVk(m_vkImpl, job);
            }

            GpuBaker &globalGpuBaker() noexcept
            {
                static GpuBaker inst;
                return inst;
            }

        }
    }
}
