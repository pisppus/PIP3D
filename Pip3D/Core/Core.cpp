#include "Core.h"

namespace pip3D
{
    EventSystem::Listener EventSystem::listeners[MAX_LISTENERS];
    int EventSystem::listenerCount = 0;

    Profiler::Section Profiler::sections[MAX_SECTIONS];
    int Profiler::sectionCount = 0;
    int Profiler::currentSection = -1;

    ResourceManager::Resource ResourceManager::resources[MAX_RESOURCES];
    int ResourceManager::resourceCount = 0;
    size_t ResourceManager::totalMemory = 0;
    size_t ResourceManager::maxMemory = 1024 * 1024;

}
