#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#include "GPU/Dispatch.hpp"
#include "GPU/TraceSpv.hpp"
#include "Core/Progress.hpp"
#include "Core/Math.hpp"
#include "Core/Noise.hpp"
#include "Trace/Bvh.hpp"
#include "Scene/Collect.hpp"

namespace pip3D
{
    namespace Bake
    {
        namespace Gpu
        {

            struct VulkanImpl
            {
                void *lib = nullptr;
                PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = nullptr;
                PFN_vkCreateInstance vkCreateInstance = nullptr;
                PFN_vkDestroyInstance vkDestroyInstance = nullptr;
                PFN_vkEnumeratePhysicalDevices vkEnumeratePhysicalDevices = nullptr;
                PFN_vkGetPhysicalDeviceProperties vkGetPhysicalDeviceProperties = nullptr;
                PFN_vkGetPhysicalDeviceQueueFamilyProperties vkGetPhysicalDeviceQueueFamilyProperties = nullptr;
                PFN_vkGetPhysicalDeviceMemoryProperties vkGetPhysicalDeviceMemoryProperties = nullptr;
                PFN_vkCreateDevice vkCreateDevice = nullptr;
                PFN_vkGetDeviceProcAddr vkGetDeviceProcAddr = nullptr;
                PFN_vkDestroyDevice vkDestroyDevice = nullptr;
                PFN_vkGetDeviceQueue vkGetDeviceQueue = nullptr;
                PFN_vkCreateBuffer vkCreateBuffer = nullptr;
                PFN_vkDestroyBuffer vkDestroyBuffer = nullptr;
                PFN_vkGetBufferMemoryRequirements vkGetBufferMemoryRequirements = nullptr;
                PFN_vkAllocateMemory vkAllocateMemory = nullptr;
                PFN_vkFreeMemory vkFreeMemory = nullptr;
                PFN_vkBindBufferMemory vkBindBufferMemory = nullptr;
                PFN_vkMapMemory vkMapMemory = nullptr;
                PFN_vkUnmapMemory vkUnmapMemory = nullptr;
                PFN_vkFlushMappedMemoryRanges vkFlushMappedMemoryRanges = nullptr;
                PFN_vkInvalidateMappedMemoryRanges vkInvalidateMappedMemoryRanges = nullptr;
                PFN_vkCreateShaderModule vkCreateShaderModule = nullptr;
                PFN_vkDestroyShaderModule vkDestroyShaderModule = nullptr;
                PFN_vkCreateDescriptorSetLayout vkCreateDescriptorSetLayout = nullptr;
                PFN_vkDestroyDescriptorSetLayout vkDestroyDescriptorSetLayout = nullptr;
                PFN_vkCreatePipelineLayout vkCreatePipelineLayout = nullptr;
                PFN_vkDestroyPipelineLayout vkDestroyPipelineLayout = nullptr;
                PFN_vkCreateComputePipelines vkCreateComputePipelines = nullptr;
                PFN_vkDestroyPipeline vkDestroyPipeline = nullptr;
                PFN_vkCreateDescriptorPool vkCreateDescriptorPool = nullptr;
                PFN_vkDestroyDescriptorPool vkDestroyDescriptorPool = nullptr;
                PFN_vkAllocateDescriptorSets vkAllocateDescriptorSets = nullptr;
                PFN_vkUpdateDescriptorSets vkUpdateDescriptorSets = nullptr;
                PFN_vkCreateCommandPool vkCreateCommandPool = nullptr;
                PFN_vkDestroyCommandPool vkDestroyCommandPool = nullptr;
                PFN_vkAllocateCommandBuffers vkAllocateCommandBuffers = nullptr;
                PFN_vkFreeCommandBuffers vkFreeCommandBuffers = nullptr;
                PFN_vkBeginCommandBuffer vkBeginCommandBuffer = nullptr;
                PFN_vkEndCommandBuffer vkEndCommandBuffer = nullptr;
                PFN_vkCmdBindPipeline vkCmdBindPipeline = nullptr;
                PFN_vkCmdBindDescriptorSets vkCmdBindDescriptorSets = nullptr;
                PFN_vkCmdDispatch vkCmdDispatch = nullptr;
                PFN_vkCreateFence vkCreateFence = nullptr;
                PFN_vkDestroyFence vkDestroyFence = nullptr;
                PFN_vkResetFences vkResetFences = nullptr;
                PFN_vkWaitForFences vkWaitForFences = nullptr;
                PFN_vkQueueSubmit vkQueueSubmit = nullptr;
                PFN_vkDeviceWaitIdle vkDeviceWaitIdle = nullptr;
                PFN_vkResetCommandBuffer vkResetCommandBuffer = nullptr;
                VkInstance instance = VK_NULL_HANDLE;
                VkPhysicalDevice phys = VK_NULL_HANDLE;
                VkDevice device = VK_NULL_HANDLE;
                VkQueue queue = VK_NULL_HANDLE;
                uint32_t qFamily = 0;
                VkPhysicalDeviceMemoryProperties memProps{};
                VkPhysicalDeviceLimits limits{};
                VkCommandPool cmdPool = VK_NULL_HANDLE;
                VkDescriptorPool descPool = VK_NULL_HANDLE;
                VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
                VkPipelineLayout pipeLayout = VK_NULL_HANDLE;
                VkPipeline pipeline = VK_NULL_HANDLE;
                VkShaderModule shader = VK_NULL_HANDLE;
                VkCommandBuffer cmdBuf = VK_NULL_HANDLE;
                VkDescriptorSet descSet = VK_NULL_HANDLE;
                VkFence fence = VK_NULL_HANDLE;
                char name[128] = {};
                bool inited = false;
                struct VBuffer
                {
                    VkBuffer buf = VK_NULL_HANDLE;
                    VkDeviceMemory mem = VK_NULL_HANDLE;
                    void *mapped = nullptr;
                    VkDeviceSize size = 0;
                    VkDeviceSize allocSize = 0;
                    bool nonCoherent = false;
                };
                VBuffer bTris, bNodes, bTriIdx, bLights, bEmi, bTexw, bTexm, bBN;
                VBuffer bPar, bTexels;
                VBuffer oDen, oVar, oStl, oStc;
                uint64_t fpTris = 0, fpNodes = 0, fpLights = 0, fpEmi = 0, fpTriIdx = 0;
                uint32_t texWordCount = 0;
                uint32_t curN = 0;
                std::vector<uint32_t> parScratch;
                std::vector<uint32_t> texelScratch;
            };

            static void *loadVulkanLib() noexcept
            {
#if defined(_WIN32)
                HMODULE h = LoadLibraryA("vulkan-1.dll");
                if (!h)
                    h = LoadLibraryA("vulkan.dll");
                return (void *)h;
#else
                void *h = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
                if (!h)
                    h = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
#if defined(__APPLE__)
                if (!h)
                    h = dlopen("libvulkan.dylib", RTLD_NOW | RTLD_LOCAL);
                if (!h)
                    h = dlopen("libMoltenVK.dylib", RTLD_NOW | RTLD_LOCAL);
#endif
                return h;
#endif
            }
            static void unloadVulkanLib(void *lib) noexcept
            {
#if defined(_WIN32)
                if (lib)
                    FreeLibrary((HMODULE)lib);
#else
                if (lib)
                    dlclose(lib);
#endif
            }
            static void *getProc(void *lib, const char *name) noexcept
            {
#if defined(_WIN32)
                return (void *)GetProcAddress((HMODULE)lib, name);
#else
                return dlsym(lib, name);
#endif
            }
            static uint32_t findMemoryType(const VulkanImpl *impl, uint32_t filter, VkMemoryPropertyFlags props) noexcept
            {
                for (uint32_t i = 0; i < impl->memProps.memoryTypeCount; ++i)
                    if ((filter & (1u << i)) && (impl->memProps.memoryTypes[i].propertyFlags & props) == props)
                        return i;
                return UINT32_MAX;
            }
            static void destroyVBuffer(VulkanImpl *impl, VulkanImpl::VBuffer &b) noexcept
            {
                if (b.mapped && b.mem)
                {
                    impl->vkUnmapMemory(impl->device, b.mem);
                    b.mapped = nullptr;
                }
                if (b.buf)
                {
                    impl->vkDestroyBuffer(impl->device, b.buf, nullptr);
                    b.buf = VK_NULL_HANDLE;
                }
                if (b.mem)
                {
                    impl->vkFreeMemory(impl->device, b.mem, nullptr);
                    b.mem = VK_NULL_HANDLE;
                }
                b.size = 0;
                b.allocSize = 0;
            }
            static bool createHostBuffer(VulkanImpl *impl, VkDeviceSize bytes, VulkanImpl::VBuffer &out) noexcept
            {
                if (bytes == 0)
                    bytes = 4;
                if (out.buf != VK_NULL_HANDLE && out.size == bytes)
                    return true;

                if (bytes > impl->limits.maxStorageBufferRange)
                {
                    std::printf("\033[91m[-] Vulkan: buffer needs %.1f MB, device storage-buffer limit is %.1f MB - scene too large for this GPU\033[0m\n",
                                static_cast<double>(bytes) / (1024.0 * 1024.0),
                                static_cast<double>(impl->limits.maxStorageBufferRange) / (1024.0 * 1024.0));
                    return false;
                }
                if (out.buf != VK_NULL_HANDLE)
                    destroyVBuffer(impl, out);
                VkBufferCreateInfo bi{};
                bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
                bi.size = bytes;
                bi.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
                bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
                if (impl->vkCreateBuffer(impl->device, &bi, nullptr, &out.buf) != VK_SUCCESS)
                    return false;
                VkMemoryRequirements req{};
                impl->vkGetBufferMemoryRequirements(impl->device, out.buf, &req);
                uint32_t idx = findMemoryType(impl, req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
                bool nonCoherent = false;
                if (idx == UINT32_MAX)
                {
                    idx = findMemoryType(impl, req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
                    if (idx == UINT32_MAX)
                    {
                        impl->vkDestroyBuffer(impl->device, out.buf, nullptr);
                        out.buf = VK_NULL_HANDLE;
                        return false;
                    }
                    nonCoherent = true;
                }
                else
                {
                    VkMemoryPropertyFlags f = impl->memProps.memoryTypes[idx].propertyFlags;
                    if (!(f & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
                        nonCoherent = true;
                }
                VkMemoryAllocateInfo ai{};
                ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
                ai.allocationSize = req.size;
                ai.memoryTypeIndex = idx;
                if (impl->vkAllocateMemory(impl->device, &ai, nullptr, &out.mem) != VK_SUCCESS)
                {
                    impl->vkDestroyBuffer(impl->device, out.buf, nullptr);
                    out.buf = VK_NULL_HANDLE;
                    return false;
                }
                if (impl->vkBindBufferMemory(impl->device, out.buf, out.mem, 0) != VK_SUCCESS)
                {
                    impl->vkFreeMemory(impl->device, out.mem, nullptr);
                    out.mem = VK_NULL_HANDLE;
                    impl->vkDestroyBuffer(impl->device, out.buf, nullptr);
                    out.buf = VK_NULL_HANDLE;
                    return false;
                }
                if (impl->vkMapMemory(impl->device, out.mem, 0, req.size, 0, &out.mapped) != VK_SUCCESS)
                {
                    impl->vkFreeMemory(impl->device, out.mem, nullptr);
                    out.mem = VK_NULL_HANDLE;
                    impl->vkDestroyBuffer(impl->device, out.buf, nullptr);
                    out.buf = VK_NULL_HANDLE;
                    return false;
                }
                out.size = bytes;
                out.allocSize = req.size;
                out.nonCoherent = nonCoherent;
                return true;
            }
            static bool uploadToBuffer(VulkanImpl *impl, VulkanImpl::VBuffer &buf, const void *data, size_t bytes) noexcept
            {
                size_t need = bytes ? bytes : 4;
                if (!createHostBuffer(impl, need, buf))
                    return false;
                if (bytes && data)
                    std::memcpy(buf.mapped, data, bytes);
                else
                    std::memset(buf.mapped, 0, need);
                if (buf.nonCoherent)
                {
                    VkMappedMemoryRange r{};
                    r.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
                    r.memory = buf.mem;
                    r.offset = 0;
                    r.size = VK_WHOLE_SIZE;
                    impl->vkFlushMappedMemoryRanges(impl->device, 1, &r);
                }
                return true;
            }
            static void pushF(std::vector<uint32_t> &w, float v) { w.push_back(std::bit_cast<uint32_t>(v)); }
            static void pushF3(std::vector<uint32_t> &w, const Vector3 &v)
            {
                pushF(w, v.x);
                pushF(w, v.y);
                pushF(w, v.z);
            }

            template <typename T>
            static uint64_t fingerprint(const T &vec) noexcept
            {
                return vec.empty() ? 0 : fnv1a64(vec.data(), vec.size() * sizeof(typename T::value_type));
            }
            enum ParWords : uint32_t
            {
                pSunDirTo = 0,
                pSunCol = 3,
                pSkyTop = 6,
                pSkyHor = 9,
                pSkyGnd = 12,
                pSkyAvg = 15,
                pSkyLevel = 18,
                pSkyNeut = 19,
                pWrap = 20,
                pDiffScale = 21,
                pSmoothEps = 22,
                pGiGain = 23,
                pGiMaxAlbedo = 24,
                pGiDesat = 25,
                pAoDist = 26,
                pBias = 27,
                pSunRadius = 28,
                pEmiArea = 29,
                pFinalMode = 30,
                pLightCount = 31,
                pEmiCount = 32,
                pSunRays = 33,
                pAoRays = 34,
                pGiRays = 35,
                pGiNee = 36,
                pSeedBase = 37,
                pTexelCount = 38,
                pRW = 39,
                pRH = 40,
                pSunT1 = 41,
                pSunT2 = 44,
                pEmiSamp = 47,
                pNodeCount = 48,
                pTexWordCnt = 49,
                pTexelBase = 50,
                pHasSun = 51,
                kParWords = 56
            };

            static bool ensureSceneBuffers(VulkanImpl *impl, const GpuJob &job) noexcept
            {
                const auto &tris = job.bvh->tris();
                if (job.bvh->trisFingerprint() != impl->fpTris || impl->bTris.buf == VK_NULL_HANDLE)
                {
                    std::vector<uint32_t> raw;
                    raw.reserve(tris.size() * 22);
                    std::vector<const Texture *> slots;
                    std::map<const Texture *, uint32_t> slotOf;
                    for (const BakeTri &t : tris)
                    {
                        uint32_t slot = 0xFFFFFFFFu;
                        if (t.tex && t.tex->data && t.tex->widthFlt() > 0.5f)
                        {
                            auto it = slotOf.find(t.tex);
                            if (it == slotOf.end())
                            {
                                slot = (uint32_t)slots.size();
                                slotOf.emplace(t.tex, slot);
                                slots.push_back(t.tex);
                            }
                            else
                                slot = it->second;
                        }
                        pushF3(raw, t.v0);
                        pushF3(raw, t.e1);
                        pushF3(raw, t.e2);
                        pushF3(raw, t.gn);
                        pushF3(raw, t.albedo);
                        raw.push_back((uint32_t)(int32_t)slot);
                        raw.push_back(std::bit_cast<uint32_t>(t.tu0));
                        raw.push_back(std::bit_cast<uint32_t>(t.tv0));
                        raw.push_back(std::bit_cast<uint32_t>(t.tu1));
                        raw.push_back(std::bit_cast<uint32_t>(t.tv1));
                        raw.push_back(std::bit_cast<uint32_t>(t.tu2));
                        raw.push_back(std::bit_cast<uint32_t>(t.tv2));
                    }
                    std::vector<uint32_t> texw, texm;
                    for (const Texture *tex : slots)
                    {
                        uint32_t words = (uint32_t)tex->widthFlt() * (uint32_t)tex->heightFlt();
                        texm.push_back((uint32_t)texw.size());
                        texm.push_back(words);
                        texm.push_back(0);
                        texm.push_back(tex->maskU());
                        texm.push_back(tex->maskV());
                        texm.push_back(tex->shiftU);
                        pushF(texm, tex->widthFlt());
                        pushF(texm, tex->heightFlt());
                        texm.push_back(0);
                        texm.push_back(0);
                        for (uint32_t i = 0; i < words; ++i)
                            texw.push_back(tex->data[i]);
                    }
                    if (raw.empty())
                    {
                        uint32_t z = 0;
                        if (!uploadToBuffer(impl, impl->bTris, &z, 4))
                            return false;
                    }
                    else if (!uploadToBuffer(impl, impl->bTris, raw.data(), raw.size() * 4))
                        return false;
                    if (texm.empty())
                    {
                        uint32_t z = 0;
                        if (!uploadToBuffer(impl, impl->bTexm, &z, 4))
                            return false;
                    }
                    else if (!uploadToBuffer(impl, impl->bTexm, texm.data(), texm.size() * 4))
                        return false;
                    if (texw.empty())
                    {
                        uint32_t z = 0;
                        if (!uploadToBuffer(impl, impl->bTexw, &z, 4))
                            return false;
                        impl->texWordCount = 0;
                    }
                    else
                    {
                        if (!uploadToBuffer(impl, impl->bTexw, texw.data(), texw.size() * 4))
                            return false;
                        impl->texWordCount = (uint32_t)texw.size();
                    }
                    impl->fpTris = job.bvh->trisFingerprint();
                }
                const auto &nodes = job.bvh->nodes();
                if (job.bvh->nodesFingerprint() != impl->fpNodes || impl->bNodes.buf == VK_NULL_HANDLE)
                {
                    std::vector<uint32_t> raw;
                    raw.reserve(nodes.size() * 10);
                    for (const auto &nd : nodes)
                    {
                        pushF3(raw, nd.bounds.mn);
                        pushF3(raw, nd.bounds.mx);
                        raw.push_back(nd.left);
                        raw.push_back(nd.right);
                        raw.push_back(nd.triStart);
                        raw.push_back(nd.triCount);
                    }
                    if (raw.empty())
                    {
                        uint32_t z = 0;
                        if (!uploadToBuffer(impl, impl->bNodes, &z, 4))
                            return false;
                    }
                    else if (!uploadToBuffer(impl, impl->bNodes, raw.data(), raw.size() * 4))
                        return false;
                    impl->fpNodes = job.bvh->nodesFingerprint();
                }
                const auto &triIdx = job.bvh->indices();
                if (job.bvh->indicesFingerprint() != impl->fpTriIdx || impl->bTriIdx.buf == VK_NULL_HANDLE)
                {
                    if (triIdx.empty())
                    {
                        uint32_t z = 0;
                        if (!uploadToBuffer(impl, impl->bTriIdx, &z, 4))
                            return false;
                    }
                    else if (!uploadToBuffer(impl, impl->bTriIdx, triIdx.data(), triIdx.size() * 4))
                        return false;
                    impl->fpTriIdx = job.bvh->indicesFingerprint();
                }
                const auto &lights = job.light->staticLights;
                if (fingerprint(lights) != impl->fpLights || impl->bLights.buf == VK_NULL_HANDLE)
                {
                    std::vector<uint32_t> raw;
                    raw.reserve(lights.size() * 8);
                    for (const auto &sl : lights)
                    {
                        pushF3(raw, sl.pos);
                        pushF3(raw, sl.color);
                        pushF(raw, sl.rangeSq);
                        pushF(raw, sl.invRangeSq);
                    }
                    if (raw.empty())
                    {
                        uint32_t z = 0;
                        if (!uploadToBuffer(impl, impl->bLights, &z, 4))
                            return false;
                    }
                    else if (!uploadToBuffer(impl, impl->bLights, raw.data(), raw.size() * 4))
                        return false;
                    impl->fpLights = fingerprint(lights);
                }
                const auto &emi = *job.emissives;
                if (fingerprint(emi) != impl->fpEmi || impl->bEmi.buf == VK_NULL_HANDLE)
                {
                    std::vector<uint32_t> raw;
                    raw.reserve(emi.size() * 16);
                    for (const auto &e : emi)
                    {
                        pushF3(raw, e.v0);
                        pushF3(raw, e.e1);
                        pushF3(raw, e.e2);
                        pushF3(raw, e.n);
                        pushF(raw, e.area);
                        pushF3(raw, e.color);
                    }
                    if (raw.empty())
                    {
                        uint32_t z = 0;
                        if (!uploadToBuffer(impl, impl->bEmi, &z, 4))
                            return false;
                    }
                    else if (!uploadToBuffer(impl, impl->bEmi, raw.data(), raw.size() * 4))
                        return false;
                    impl->fpEmi = fingerprint(emi);
                }
                if (impl->bBN.buf == VK_NULL_HANDLE)
                {
                    const Noise::BlueNoise &bn = Noise::blueNoise();
                    std::vector<uint32_t> raw;
                    raw.resize(Noise::BlueNoise::kRankSize);
                    for (uint32_t i = 0; i < Noise::BlueNoise::kRankSize; ++i)
                        raw[i] = bn.rankTable()[i];
                    if (!uploadToBuffer(impl, impl->bBN, raw.data(), raw.size() * 4))
                        return false;
                }
                return true;
            }
            static bool uploadPerPass(VulkanImpl *impl, const GpuJob &job) noexcept
            {
                const InstanceBakeData &ib = *job.ib;
                const UnwrapResult &uw = ib.meshCache->unwrap;
                const uint32_t rw = job.rw, rh = job.rh;
                const uint32_t n = rw * rh;
                const SceneLighting &L = *job.light;
                std::vector<uint32_t> &par = impl->parScratch;
                par.clear();
                par.reserve(kParWords);
                pushF3(par, L.sunDirTo);
                pushF3(par, L.sunCol);
                float tr, tg, tb;
                L.skyTop.toFloat(tr, tg, tb);
                pushF(par, tr);
                pushF(par, tg);
                pushF(par, tb);
                L.skyHor.toFloat(tr, tg, tb);
                pushF(par, tr);
                pushF(par, tg);
                pushF(par, tb);
                L.skyGnd.toFloat(tr, tg, tb);
                pushF(par, tr);
                pushF(par, tg);
                pushF(par, tb);
                pushF3(par, L.skyAvgCol);
                pushF(par, L.skyLevel);
                pushF(par, L.skyNeut);
                pushF(par, L.wrapTerm);
                pushF(par, L.diffScale);
                pushF(par, L.smoothEps);
                pushF(par, job.cfg->giGain);
                pushF(par, job.cfg->giMaxAlbedo);
                pushF(par, job.cfg->giDesat);
                pushF(par, job.cfg->aoMaxDist);
                pushF(par, job.cfg->bias);
                pushF(par, job.cfg->sunAngularRadius);
                pushF(par, job.emissiveTotalArea);
                par.push_back(job.finalMode ? 1u : 0u);
                par.push_back((uint32_t)L.staticLights.size());
                par.push_back((uint32_t)job.emissives->size());
                par.push_back(std::max(64u, job.cfg->sunRays));
                par.push_back(std::max(128u, job.cfg->aoRays));
                par.push_back(std::max(32u, job.cfg->giRays));
                par.push_back(std::max(8u, job.cfg->giSkyNeeRays));
                par.push_back(ib.meshKey * 131u + 1u);
                par.push_back(n);
                par.push_back(rw);
                par.push_back(rh);
                pushF3(par, job.sunT1);
                pushF3(par, job.sunT2);
                par.push_back(job.cfg->emissiveSamples);
                par.push_back((uint32_t)job.bvh->nodes().size());
                par.push_back(impl->texWordCount);
                par.push_back(0);
                par.push_back(L.hasSun ? 1u : 0u);
                par.resize(kParWords, 0);
                if (!uploadToBuffer(impl, impl->bPar, par.data(), par.size() * 4))
                    return false;
                std::vector<uint32_t> &texels = impl->texelScratch;
                texels.clear();
                texels.reserve((size_t)n * 8);
                for (uint32_t i = 0; i < n; ++i)
                {
                    pushF3(texels, ib.texelPos[i]);
                    pushF3(texels, ib.texelNrm[i]);
                    texels.push_back(uw.texels[i].face);
                    texels.push_back(0);
                }
                if (texels.empty())
                {
                    uint32_t z = 0;
                    if (!uploadToBuffer(impl, impl->bTexels, &z, 4))
                        return false;
                }
                else if (!uploadToBuffer(impl, impl->bTexels, texels.data(), texels.size() * 4))
                    return false;
                return true;
            }
            static bool ensureOutputs(VulkanImpl *impl, uint32_t n) noexcept
            {
                if (impl->curN == n && impl->oDen.buf != VK_NULL_HANDLE)
                    return true;
                destroyVBuffer(impl, impl->oDen);
                destroyVBuffer(impl, impl->oVar);
                destroyVBuffer(impl, impl->oStl);
                destroyVBuffer(impl, impl->oStc);
                if (!createHostBuffer(impl, (VkDeviceSize)n * 12, impl->oDen))
                    return false;
                if (!createHostBuffer(impl, (VkDeviceSize)n * 4, impl->oVar))
                    return false;
                if (!createHostBuffer(impl, (VkDeviceSize)n * 4, impl->oStl))
                    return false;
                if (!createHostBuffer(impl, (VkDeviceSize)n * 12, impl->oStc))
                    return false;
                if (impl->oDen.mapped)
                    std::memset(impl->oDen.mapped, 0, (size_t)n * 12);
                if (impl->oVar.mapped)
                    std::memset(impl->oVar.mapped, 0, (size_t)n * 4);
                if (impl->oStl.mapped)
                    std::memset(impl->oStl.mapped, 0, (size_t)n * 4);
                if (impl->oStc.mapped)
                    std::memset(impl->oStc.mapped, 0, (size_t)n * 12);
                {
                    VkMappedMemoryRange rngs[4]{};
                    uint32_t rc = 0;
                    auto pushFlush = [&](VulkanImpl::VBuffer &b)
                    {
                        if (b.nonCoherent && b.mem)
                        {
                            rngs[rc].sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
                            rngs[rc].memory = b.mem;
                            rngs[rc].size = VK_WHOLE_SIZE;
                            ++rc;
                        }
                    };
                    pushFlush(impl->oDen);
                    pushFlush(impl->oVar);
                    pushFlush(impl->oStl);
                    pushFlush(impl->oStc);
                    if (rc)
                        impl->vkFlushMappedMemoryRanges(impl->device, rc, rngs);
                }
                impl->curN = n;
                return true;
            }
            static void updateDescriptors(VulkanImpl *impl) noexcept
            {
                VkDescriptorBufferInfo infos[14]{};
                auto setInfo = [&](int idx, VulkanImpl::VBuffer &b)
                { infos[idx].buffer=b.buf; infos[idx].offset=0; infos[idx].range=VK_WHOLE_SIZE; };
                setInfo(0, impl->bTris);
                setInfo(1, impl->bNodes);
                setInfo(2, impl->bTexels);
                setInfo(3, impl->bLights);
                setInfo(4, impl->bEmi);
                setInfo(5, impl->bTexw);
                setInfo(6, impl->bTexm);
                setInfo(7, impl->bBN);
                setInfo(8, impl->bPar);
                setInfo(9, impl->bTriIdx);
                setInfo(10, impl->oDen);
                setInfo(11, impl->oVar);
                setInfo(12, impl->oStl);
                setInfo(13, impl->oStc);
                VkWriteDescriptorSet writes[14]{};
                for (int i = 0; i < 14; ++i)
                {
                    writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    writes[i].dstSet = impl->descSet;
                    writes[i].dstBinding = (uint32_t)i;
                    writes[i].descriptorCount = 1;
                    writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                    writes[i].pBufferInfo = &infos[i];
                }
                impl->vkUpdateDescriptorSets(impl->device, 14, writes, 0, nullptr);
            }

            namespace detailVk
            {

                bool createDeviceObjects(VulkanImpl *impl) noexcept
                {
                    void *lib = loadVulkanLib();
                    if (!lib)
                        return false;
                    impl->lib = lib;
                    auto pGIPA = (PFN_vkGetInstanceProcAddr)getProc(lib, "vkGetInstanceProcAddr");
                    if (!pGIPA)
                        return false;
                    impl->vkGetInstanceProcAddr = pGIPA;
                    impl->vkCreateInstance = (PFN_vkCreateInstance)pGIPA(nullptr, "vkCreateInstance");
                    if (!impl->vkCreateInstance)
                        return false;
                    VkApplicationInfo app{};
                    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
                    app.pApplicationName = "Pip3D Bake";
                    app.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
                    app.pEngineName = "Pip3D";
                    app.engineVersion = VK_MAKE_VERSION(1, 0, 0);
                    app.apiVersion = VK_API_VERSION_1_0;
                    VkInstanceCreateInfo ici{};
                    ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
                    ici.pApplicationInfo = &app;
                    VkInstance inst = VK_NULL_HANDLE;
                    if (impl->vkCreateInstance(&ici, nullptr, &inst) != VK_SUCCESS)
                        return false;
                    impl->instance = inst;
                    impl->vkDestroyInstance = (PFN_vkDestroyInstance)pGIPA(inst, "vkDestroyInstance");
                    impl->vkEnumeratePhysicalDevices = (PFN_vkEnumeratePhysicalDevices)pGIPA(inst, "vkEnumeratePhysicalDevices");
                    impl->vkGetPhysicalDeviceProperties = (PFN_vkGetPhysicalDeviceProperties)pGIPA(inst, "vkGetPhysicalDeviceProperties");
                    impl->vkGetPhysicalDeviceQueueFamilyProperties = (PFN_vkGetPhysicalDeviceQueueFamilyProperties)pGIPA(inst, "vkGetPhysicalDeviceQueueFamilyProperties");
                    impl->vkGetPhysicalDeviceMemoryProperties = (PFN_vkGetPhysicalDeviceMemoryProperties)pGIPA(inst, "vkGetPhysicalDeviceMemoryProperties");
                    impl->vkCreateDevice = (PFN_vkCreateDevice)pGIPA(inst, "vkCreateDevice");
                    impl->vkGetDeviceProcAddr = (PFN_vkGetDeviceProcAddr)pGIPA(inst, "vkGetDeviceProcAddr");
                    impl->vkDestroyDevice = (PFN_vkDestroyDevice)pGIPA(inst, "vkDestroyDevice");
                    if (!impl->vkEnumeratePhysicalDevices || !impl->vkGetPhysicalDeviceProperties || !impl->vkGetPhysicalDeviceMemoryProperties || !impl->vkCreateDevice || !impl->vkGetDeviceProcAddr)
                        return false;
                    uint32_t devCount = 0;
                    if (impl->vkEnumeratePhysicalDevices(inst, &devCount, nullptr) != VK_SUCCESS || devCount == 0)
                        return false;
                    std::vector<VkPhysicalDevice> devs(devCount);
                    impl->vkEnumeratePhysicalDevices(inst, &devCount, devs.data());
                    VkPhysicalDevice chosen = VK_NULL_HANDLE;
                    VkPhysicalDeviceProperties chosenProps{};
                    int bestScore = -1;
                    uint32_t chosenQ = UINT32_MAX;

                    for (int pass = 0; pass < 2 && chosen == VK_NULL_HANDLE; ++pass)
                    {

                        const bool needComputeOnly = (pass == 0);
                        bestScore = -1;
                        for (auto d : devs)
                        {
                            VkPhysicalDeviceProperties pr{};
                            impl->vkGetPhysicalDeviceProperties(d, &pr);
                            uint32_t qc = 0;
                            impl->vkGetPhysicalDeviceQueueFamilyProperties(d, &qc, nullptr);
                            std::vector<VkQueueFamilyProperties> qps(qc);
                            impl->vkGetPhysicalDeviceQueueFamilyProperties(d, &qc, qps.data());
                            for (uint32_t i = 0; i < qc; ++i)
                            {
                                const bool hasCompute = (qps[i].queueFlags & VK_QUEUE_COMPUTE_BIT) != 0;
                                const bool hasGraphics = (qps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
                                if (!hasCompute)
                                    continue;
                                if (needComputeOnly && hasGraphics)
                                    continue;
                                int score = 0;
                                if (pr.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
                                    score += 1000;
                                else if (pr.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)
                                    score += 500;
                                else if (pr.deviceType == VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU)
                                    score += 200;
                                else
                                    score += 10;
                                if (!hasGraphics)
                                    score += 250;
                                score += (int)qps[i].queueCount;
                                if (score > bestScore)
                                {
                                    bestScore = score;
                                    chosen = d;
                                    chosenProps = pr;
                                    chosenQ = i;
                                }
                            }
                        }
                    }

                    if (chosen == VK_NULL_HANDLE)
                        return false;
                    impl->phys = chosen;
                    impl->qFamily = chosenQ;
                    impl->limits = chosenProps.limits;
                    std::snprintf(impl->name, sizeof(impl->name), "%s", chosenProps.deviceName);
                    impl->vkGetPhysicalDeviceMemoryProperties(chosen, &impl->memProps);
                    float prio = 1.0f;
                    VkDeviceQueueCreateInfo qci{};
                    qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
                    qci.queueFamilyIndex = chosenQ;
                    qci.queueCount = 1;
                    qci.pQueuePriorities = &prio;
                    VkDeviceCreateInfo dci{};
                    dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
                    dci.queueCreateInfoCount = 1;
                    dci.pQueueCreateInfos = &qci;
                    VkDevice dev = VK_NULL_HANDLE;
                    if (impl->vkCreateDevice(chosen, &dci, nullptr, &dev) != VK_SUCCESS)
                        return false;
                    impl->device = dev;
#define LDDEV(n) impl->n = (PFN_##n)impl->vkGetDeviceProcAddr(dev, #n)
                    LDDEV(vkGetDeviceQueue);
                    LDDEV(vkCreateBuffer);
                    LDDEV(vkDestroyBuffer);
                    LDDEV(vkGetBufferMemoryRequirements);
                    LDDEV(vkAllocateMemory);
                    LDDEV(vkFreeMemory);
                    LDDEV(vkBindBufferMemory);
                    LDDEV(vkMapMemory);
                    LDDEV(vkUnmapMemory);
                    LDDEV(vkFlushMappedMemoryRanges);
                    LDDEV(vkInvalidateMappedMemoryRanges);
                    LDDEV(vkCreateShaderModule);
                    LDDEV(vkDestroyShaderModule);
                    LDDEV(vkCreateDescriptorSetLayout);
                    LDDEV(vkDestroyDescriptorSetLayout);
                    LDDEV(vkCreatePipelineLayout);
                    LDDEV(vkDestroyPipelineLayout);
                    LDDEV(vkCreateComputePipelines);
                    LDDEV(vkDestroyPipeline);
                    LDDEV(vkCreateDescriptorPool);
                    LDDEV(vkDestroyDescriptorPool);
                    LDDEV(vkAllocateDescriptorSets);
                    LDDEV(vkUpdateDescriptorSets);
                    LDDEV(vkCreateCommandPool);
                    LDDEV(vkDestroyCommandPool);
                    LDDEV(vkAllocateCommandBuffers);
                    LDDEV(vkFreeCommandBuffers);
                    LDDEV(vkBeginCommandBuffer);
                    LDDEV(vkEndCommandBuffer);
                    LDDEV(vkCmdBindPipeline);
                    LDDEV(vkCmdBindDescriptorSets);
                    LDDEV(vkCmdDispatch);
                    LDDEV(vkCreateFence);
                    LDDEV(vkDestroyFence);
                    LDDEV(vkResetFences);
                    LDDEV(vkWaitForFences);
                    LDDEV(vkQueueSubmit);
                    LDDEV(vkDeviceWaitIdle);
                    LDDEV(vkResetCommandBuffer);
#undef LDDEV
                    if (!impl->vkGetDeviceQueue || !impl->vkCreateBuffer || !impl->vkAllocateMemory || !impl->vkCreateShaderModule || !impl->vkCreateComputePipelines)
                        return false;
                    impl->vkGetDeviceQueue(dev, chosenQ, 0, &impl->queue);
                    VkCommandPoolCreateInfo cpci{};
                    cpci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
                    cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
                    cpci.queueFamilyIndex = chosenQ;
                    if (impl->vkCreateCommandPool(dev, &cpci, nullptr, &impl->cmdPool) != VK_SUCCESS)
                        return false;
                    VkDescriptorPoolSize ps{};
                    ps.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                    ps.descriptorCount = 14;
                    VkDescriptorPoolCreateInfo dpci{};
                    dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
                    dpci.maxSets = 1;
                    dpci.poolSizeCount = 1;
                    dpci.pPoolSizes = &ps;
                    if (impl->vkCreateDescriptorPool(dev, &dpci, nullptr, &impl->descPool) != VK_SUCCESS)
                        return false;
                    VkDescriptorSetLayoutBinding bds[14]{};
                    for (int i = 0; i < 14; ++i)
                    {
                        bds[i].binding = (uint32_t)i;
                        bds[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                        bds[i].descriptorCount = 1;
                        bds[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
                    }
                    VkDescriptorSetLayoutCreateInfo dsci{};
                    dsci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
                    dsci.bindingCount = 14;
                    dsci.pBindings = bds;
                    if (impl->vkCreateDescriptorSetLayout(dev, &dsci, nullptr, &impl->setLayout) != VK_SUCCESS)
                        return false;
                    VkPipelineLayoutCreateInfo plci{};
                    plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
                    plci.setLayoutCount = 1;
                    plci.pSetLayouts = &impl->setLayout;
                    if (impl->vkCreatePipelineLayout(dev, &plci, nullptr, &impl->pipeLayout) != VK_SUCCESS)
                        return false;
                    VkShaderModuleCreateInfo smci{};
                    smci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
                    smci.codeSize = kVulkanSpvSize;
                    smci.pCode = kVulkanSpv;
                    if (impl->vkCreateShaderModule(dev, &smci, nullptr, &impl->shader) != VK_SUCCESS)
                        return false;
                    VkPipelineShaderStageCreateInfo st{};
                    st.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                    st.stage = VK_SHADER_STAGE_COMPUTE_BIT;
                    st.module = impl->shader;
                    st.pName = "main";
                    VkComputePipelineCreateInfo cpci2{};
                    cpci2.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
                    cpci2.stage = st;
                    cpci2.layout = impl->pipeLayout;
                    if (impl->vkCreateComputePipelines(dev, VK_NULL_HANDLE, 1, &cpci2, nullptr, &impl->pipeline) != VK_SUCCESS)
                        return false;
                    VkDescriptorSetAllocateInfo dsai{};
                    dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
                    dsai.descriptorPool = impl->descPool;
                    dsai.descriptorSetCount = 1;
                    dsai.pSetLayouts = &impl->setLayout;
                    if (impl->vkAllocateDescriptorSets(dev, &dsai, &impl->descSet) != VK_SUCCESS)
                        return false;
                    VkCommandBufferAllocateInfo cbai{};
                    cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
                    cbai.commandPool = impl->cmdPool;
                    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
                    cbai.commandBufferCount = 1;
                    if (impl->vkAllocateCommandBuffers(dev, &cbai, &impl->cmdBuf) != VK_SUCCESS)
                        return false;
                    VkFenceCreateInfo fci{};
                    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
                    if (impl->vkCreateFence(dev, &fci, nullptr, &impl->fence) != VK_SUCCESS)
                        return false;
                    impl->inited = true;
                    return true;
                }

                void destroyInitState(VulkanImpl *impl) noexcept
                {
                    if (impl->device)
                    {
                        if (impl->vkDeviceWaitIdle)
                            impl->vkDeviceWaitIdle(impl->device);
                        if (impl->fence && impl->vkDestroyFence)
                            impl->vkDestroyFence(impl->device, impl->fence, nullptr);
                        if (impl->cmdBuf && impl->vkFreeCommandBuffers && impl->cmdPool)
                            impl->vkFreeCommandBuffers(impl->device, impl->cmdPool, 1, &impl->cmdBuf);
                        if (impl->pipeline && impl->vkDestroyPipeline)
                            impl->vkDestroyPipeline(impl->device, impl->pipeline, nullptr);
                        if (impl->shader && impl->vkDestroyShaderModule)
                            impl->vkDestroyShaderModule(impl->device, impl->shader, nullptr);
                        if (impl->pipeLayout && impl->vkDestroyPipelineLayout)
                            impl->vkDestroyPipelineLayout(impl->device, impl->pipeLayout, nullptr);
                        if (impl->setLayout && impl->vkDestroyDescriptorSetLayout)
                            impl->vkDestroyDescriptorSetLayout(impl->device, impl->setLayout, nullptr);
                        if (impl->descPool && impl->vkDestroyDescriptorPool)
                            impl->vkDestroyDescriptorPool(impl->device, impl->descPool, nullptr);
                        if (impl->cmdPool && impl->vkDestroyCommandPool)
                            impl->vkDestroyCommandPool(impl->device, impl->cmdPool, nullptr);
                        if (impl->vkDestroyDevice)
                            impl->vkDestroyDevice(impl->device, nullptr);
                    }
                    if (impl->instance && impl->vkDestroyInstance)
                        impl->vkDestroyInstance(impl->instance, nullptr);
                    if (impl->lib)
                        unloadVulkanLib(impl->lib);
                }

                bool vkInit(VulkanImpl *&out) noexcept
                {
                    out = nullptr;
                    VulkanImpl *impl = new (std::nothrow) VulkanImpl();
                    if (!impl)
                        return false;
                    if (!createDeviceObjects(impl))
                    {
                        destroyInitState(impl);
                        delete impl;
                        return false;
                    }
                    out = impl;
                    return true;
                }
                void vkShutdown(VulkanImpl *&impl) noexcept
                {
                    if (!impl)
                        return;
                    if (impl->device)
                    {
                        impl->vkDeviceWaitIdle(impl->device);
                        destroyVBuffer(impl, impl->bTris);
                        destroyVBuffer(impl, impl->bNodes);
                        destroyVBuffer(impl, impl->bTriIdx);
                        destroyVBuffer(impl, impl->bLights);
                        destroyVBuffer(impl, impl->bEmi);
                        destroyVBuffer(impl, impl->bTexw);
                        destroyVBuffer(impl, impl->bTexm);
                        destroyVBuffer(impl, impl->bBN);
                        destroyVBuffer(impl, impl->bPar);
                        destroyVBuffer(impl, impl->bTexels);
                        destroyVBuffer(impl, impl->oDen);
                        destroyVBuffer(impl, impl->oVar);
                        destroyVBuffer(impl, impl->oStl);
                        destroyVBuffer(impl, impl->oStc);
                        if (impl->fence)
                            impl->vkDestroyFence(impl->device, impl->fence, nullptr);
                        if (impl->cmdBuf)
                            impl->vkFreeCommandBuffers(impl->device, impl->cmdPool, 1, &impl->cmdBuf);
                        if (impl->pipeline)
                            impl->vkDestroyPipeline(impl->device, impl->pipeline, nullptr);
                        if (impl->shader)
                            impl->vkDestroyShaderModule(impl->device, impl->shader, nullptr);
                        if (impl->pipeLayout)
                            impl->vkDestroyPipelineLayout(impl->device, impl->pipeLayout, nullptr);
                        if (impl->setLayout)
                            impl->vkDestroyDescriptorSetLayout(impl->device, impl->setLayout, nullptr);
                        if (impl->descPool)
                            impl->vkDestroyDescriptorPool(impl->device, impl->descPool, nullptr);
                        if (impl->cmdPool)
                            impl->vkDestroyCommandPool(impl->device, impl->cmdPool, nullptr);
                        impl->vkDestroyDevice(impl->device, nullptr);
                    }
                    if (impl->instance && impl->vkDestroyInstance)
                        impl->vkDestroyInstance(impl->instance, nullptr);
                    if (impl->lib)
                        unloadVulkanLib(impl->lib);
                    delete impl;
                    impl = nullptr;
                }
                const char *vkDeviceName(const VulkanImpl *impl) noexcept { return impl ? impl->name : ""; }
            }

            bool gpuDispatchVk(VulkanImpl *impl, const GpuJob &job) noexcept
            {
                if (!impl || !impl->inited)
                    return false;
                try
                {
                    const uint32_t n = job.rw * job.rh;
                    if (n == 0)
                        return true;
                    const bool stats = std::getenv("PIP3D_BAKE_GPU_STATS") != nullptr;
                    auto t0 = std::chrono::steady_clock::now();
                    auto t1 = t0;
                    auto gpuLog = [](const char *fmt, auto... args)
                    { if(progressBar().isActive()) progressBar().logf(fmt,args...); else { std::printf(fmt,args...); std::printf("\n"); } };
                    if (!ensureSceneBuffers(impl, job))
                    {
                        gpuLog("\033[91m[-] Vulkan: scene upload failed\033[0m");
                        return false;
                    }
                    if (!uploadPerPass(impl, job))
                    {
                        gpuLog("\033[91m[-] Vulkan: per-pass upload failed\033[0m");
                        return false;
                    }
                    if (!ensureOutputs(impl, n))
                    {
                        gpuLog("\033[91m[-] Vulkan: output alloc failed (texels=%u)\033[0m", n);
                        return false;
                    }
                    updateDescriptors(impl);
                    if (stats)
                        t1 = std::chrono::steady_clock::now();

                    uint32_t kChunk = 16384;
                    const uint32_t maxChunk =
                        impl->limits.maxComputeWorkGroupCount[0] > 0u
                            ? impl->limits.maxComputeWorkGroupCount[0] * 256u
                            : kChunk;
                    kChunk = std::min(kChunk, maxChunk);
                    if (const char *ev = std::getenv("PIP3D_BAKE_GPU_CHUNK"))
                    {
                        int v = std::atoi(ev);
                        if (v >= 512 && v <= 131072)
                            kChunk = std::min<uint32_t>(static_cast<uint32_t>(v), maxChunk);
                    }
                    bool ok = true;
                    const char *failStep = "unknown";
                    VkResult failRes = VK_SUCCESS;
                    for (uint32_t base = 0; base < n && ok; base += kChunk)
                    {
                        const uint32_t cnt = std::min(kChunk, n - base);
                        impl->parScratch[pTexelBase] = base;
                        std::memcpy(impl->bPar.mapped, impl->parScratch.data(), impl->parScratch.size() * 4);
                        if (impl->bPar.nonCoherent)
                        {
                            VkMappedMemoryRange r{};
                            r.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
                            r.memory = impl->bPar.mem;
                            r.size = VK_WHOLE_SIZE;
                            impl->vkFlushMappedMemoryRanges(impl->device, 1, &r);
                        }
                        impl->vkResetCommandBuffer(impl->cmdBuf, 0);
                        VkCommandBufferBeginInfo bi{};
                        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
                        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
                        failStep = "beginCommandBuffer";
                        failRes = impl->vkBeginCommandBuffer(impl->cmdBuf, &bi);
                        if (failRes != VK_SUCCESS)
                        {
                            ok = false;
                            break;
                        }
                        impl->vkCmdBindPipeline(impl->cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, impl->pipeline);
                        impl->vkCmdBindDescriptorSets(impl->cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, impl->pipeLayout, 0, 1, &impl->descSet, 0, nullptr);
                        uint32_t groups = (cnt + 255) / 256;
                        impl->vkCmdDispatch(impl->cmdBuf, groups, 1, 1);
                        failStep = "endCommandBuffer";
                        failRes = impl->vkEndCommandBuffer(impl->cmdBuf);
                        if (failRes != VK_SUCCESS)
                        {
                            ok = false;
                            break;
                        }
                        impl->vkResetFences(impl->device, 1, &impl->fence);
                        VkSubmitInfo si{};
                        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
                        si.commandBufferCount = 1;
                        si.pCommandBuffers = &impl->cmdBuf;
                        failStep = "queueSubmit";
                        failRes = impl->vkQueueSubmit(impl->queue, 1, &si, impl->fence);
                        if (failRes != VK_SUCCESS)
                        {
                            ok = false;
                            break;
                        }
                        failStep = "waitForFences";
                        failRes = impl->vkWaitForFences(impl->device, 1, &impl->fence, VK_TRUE, UINT64_MAX);
                        if (failRes != VK_SUCCESS)
                        {
                            ok = false;
                            break;
                        }
                        if (job.progressStatus)
                            progressBar().advance(cnt, job.progressStatus);
                    }
                    if (!ok)
                    {

                        impl->vkDeviceWaitIdle(impl->device);
                        impl->vkResetCommandBuffer(impl->cmdBuf, 0);
                        gpuLog("\033[91m[-] Vulkan: dispatch failed at %s (VkResult %d, texels=%u)\033[0m", failStep, static_cast<int>(failRes), n);
                        return false;
                    }
                    auto t2 = std::chrono::steady_clock::now();
                    {
                        VkMappedMemoryRange rngs[4]{};
                        uint32_t rc = 0;
                        auto pushInv = [&](VulkanImpl::VBuffer &b)
                        {
                            if (b.nonCoherent && b.mem)
                            {
                                rngs[rc].sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
                                rngs[rc].memory = b.mem;
                                rngs[rc].size = VK_WHOLE_SIZE;
                                ++rc;
                            }
                        };
                        pushInv(impl->oDen);
                        pushInv(impl->oVar);
                        pushInv(impl->oStl);
                        pushInv(impl->oStc);
                        if (rc)
                            impl->vkInvalidateMappedMemoryRanges(impl->device, rc, rngs);
                    }
                    InstanceBakeData &ib = *job.ib;
                    std::memcpy(ib.den.data(), impl->oDen.mapped, (size_t)n * 12);
                    std::memcpy(ib.varLuma.data(), impl->oVar.mapped, (size_t)n * 4);
                    std::memcpy(ib.staticLuma.data(), impl->oStl.mapped, (size_t)n * 4);
                    std::memcpy(ib.staticCol.data(), impl->oStc.mapped, (size_t)n * 12);
                    auto tEnd = std::chrono::steady_clock::now();
                    if (stats)
                    {
                        double ms0 = std::chrono::duration<double, std::milli>(t1 - t0).count();
                        double ms1 = std::chrono::duration<double, std::milli>(t2 - t1).count();
                        double ms2 = std::chrono::duration<double, std::milli>(tEnd - t2).count();
                        double tot = std::chrono::duration<double, std::milli>(tEnd - t0).count();
                        if (progressBar().isActive())
                            progressBar().logf("[GPU STATS Vulkan] texels=%u upload=%.1fms dispatch=%.1fms readback=%.1fms total=%.1fms", n, ms0, ms1, ms2, tot);
                        else
                            std::printf("[GPU STATS Vulkan] texels=%u upload=%.1fms dispatch=%.1fms readback=%.1fms total=%.1fms\n", n, ms0, ms1, ms2, tot);
                    }
                    return true;
                }
                catch (const std::exception &e)
                {
                    if (progressBar().isActive())
                        progressBar().logf("\033[91m[-] Vulkan: exception %s\033[0m", e.what());
                    else
                        std::printf("\033[91m[-] Vulkan: exception %s\033[0m\n", e.what());
                    return false;
                }
                catch (...)
                {
                    if (progressBar().isActive())
                        progressBar().logf("\033[91m[-] Vulkan: unknown exception\033[0m");
                    else
                        std::printf("\033[91m[-] Vulkan: unknown exception\033[0m\n");
                    return false;
                }
            }

        }
    }
}
