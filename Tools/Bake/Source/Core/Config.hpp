#pragma once

#include <cstdint>
#include <vector>

#include <Pip3D.hpp>

namespace pip3D
{
    namespace Bake
    {
        struct BakeConfig
        {
            BakedLightMode mode = BakedLightMode::FACTORED;
            bool modeAuto = true;

            uint32_t lmMin = 32;
            uint32_t lmMax = 512;
            float texelsPerMeter = 16.0f;
            float minTexelsPerMeter = 8.0f;

            bool adaptiveTexels = true;

            uint32_t sunRays = 128;
            uint32_t aoRays = 256;
            uint32_t giRays = 128;
            uint32_t giSkyNeeRays = 8;
            float giGain = 1.1f;

            float giMaxAlbedo = 0.50f;
            float giDesat = 0.35f;
            float sunAngularRadius = 0.025f;
            float aoMaxDist = 16.0f;
            float bias = 0.04f;

            uint32_t denoisePasses = 3;
            uint32_t lightmapBlurPasses = 1;
            float lightmapBlurSigma = 1.2f;
            float floorTexelBoost = 1.6f;

            bool dither = true;

            float saturation = 0.0f;

            float skyNeutralize = 0.35f;

            bool bakeProbes = true;
            float probeCell = 2.0f;
            uint32_t probeSkyRays = 48;

            uint32_t emissiveSamples = 32;
            float emissiveGain = 1.0f;

            const char *sceneName = "Scene";
            const char *outDir = "Tools/Bake/Output";
            bool applyToInstances = true;
            bool writeHeader = true;
        };

        void configureFromEnv(BakeConfig &cfg);

        int runBake(Renderer &r, std::vector<MeshInstance *> &instances,
                    const BakeConfig &cfg);

        inline int g_bakeResult = 0;
    }
}
