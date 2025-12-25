#include "Logging.h"
#include <Arduino.h>
#include <stdio.h>

#if ENABLE_LOGGING

namespace pip3D
{
    namespace Debug
    {

        struct LoggerState
        {
            LogLevel level;
            uint16_t modules;
            bool timestamps;
            uint32_t startMicros;
            bool initialized;
            LogLevel moduleLevels[8];
        };

        static LoggerState g_state = {static_cast<LogLevel>(LOG_DEFAULT_LEVEL), LOG_MODULE_ALL, true, 0u, false, {LOG_LEVEL_OFF, LOG_LEVEL_OFF, LOG_LEVEL_OFF, LOG_LEVEL_OFF, LOG_LEVEL_OFF, LOG_LEVEL_OFF, LOG_LEVEL_OFF, LOG_LEVEL_OFF}};

        static const char *levelToString(LogLevel level)
        {
            switch (level)
            {
            case LOG_LEVEL_ERROR:
                return "ERR";
            case LOG_LEVEL_WARNING:
                return "WRN";
            case LOG_LEVEL_INFO:
                return "INF";
            case LOG_LEVEL_DEBUG:
                return "DBG";
            case LOG_LEVEL_TRACE:
                return "TRC";
            default:
                return "OFF";
            }
        }

        static const char *moduleToString(uint16_t module)
        {
            if (module & LOG_MODULE_RENDER)
                return "RDR";
            if (module & LOG_MODULE_PHYSICS)
                return "PHY";
            if (module & LOG_MODULE_CAMERA)
                return "CAM";
            if (module & LOG_MODULE_SCENE)
                return "SCN";
            if (module & LOG_MODULE_RESOURCES)
                return "RES";
            if (module & LOG_MODULE_PERFORMANCE)
                return "PRF";
            if (module & LOG_MODULE_CORE)
                return "COR";
            if (module & LOG_MODULE_USER)
                return "USR";
            return "GEN";
        }

        void Logger::init(LogLevel level, uint16_t modulesMask, bool timestamps)
        {
            g_state.level = level;
            g_state.modules = modulesMask;
            g_state.timestamps = timestamps;
            if (!g_state.initialized)
            {
                g_state.startMicros = micros();
                g_state.initialized = true;
            }
        }

        void Logger::setLevel(LogLevel level)
        {
            g_state.level = level;
        }

        LogLevel Logger::getLevel()
        {
            return g_state.level;
        }

        void Logger::setModules(uint16_t mask)
        {
            g_state.modules = mask;
        }

        void Logger::enableModule(uint16_t module)
        {
            g_state.modules |= module;
        }

        void Logger::disableModule(uint16_t module)
        {
            g_state.modules &= static_cast<uint16_t>(~module);
        }

        uint16_t Logger::getModules()
        {
            return g_state.modules;
        }

        static int moduleIndex(uint16_t module)
        {
            if (module == 0)
                return -1;

#if defined(__GNUC__) || defined(__clang__)
            return __builtin_ctz(static_cast<unsigned int>(module));
#else
            for (int i = 0; i < 8; ++i)
            {
                if (module & (1u << i))
                    return i;
            }
            return -1;
#endif
        }

        void Logger::setModuleLevel(uint16_t module, LogLevel level)
        {
            int idx = moduleIndex(module);
            if (idx < 0)
                return;
            g_state.moduleLevels[idx] = level;
        }

        LogLevel Logger::getModuleLevel(uint16_t module)
        {
            int idx = moduleIndex(module);
            if (idx < 0)
                return g_state.level;
            return g_state.moduleLevels[idx];
        }

        void Logger::clearModuleLevels()
        {
            for (int i = 0; i < 8; ++i)
            {
                g_state.moduleLevels[i] = LOG_LEVEL_OFF;
            }
        }

        void Logger::setProfileSilent()
        {
            clearModuleLevels();
            g_state.level = LOG_LEVEL_WARNING;
            g_state.modules = LOG_MODULE_ALL;
        }

        void Logger::setProfilePerformance()
        {
            clearModuleLevels();
            g_state.level = LOG_LEVEL_WARNING;
            g_state.modules = LOG_MODULE_ALL;
            setModuleLevel(LOG_MODULE_PERFORMANCE, LOG_LEVEL_INFO);
            setModuleLevel(LOG_MODULE_CORE, LOG_LEVEL_INFO);
        }

        void Logger::setProfileVerboseAll()
        {
            clearModuleLevels();
            g_state.level = LOG_LEVEL_TRACE;
            g_state.modules = LOG_MODULE_ALL;
        }

        void Logger::setTimestampsEnabled(bool enabled)
        {
            g_state.timestamps = enabled;
        }

        bool Logger::getTimestampsEnabled()
        {
            return g_state.timestamps;
        }

        bool Logger::isEnabled(uint16_t module, LogLevel level)
        {
            if (level == LOG_LEVEL_OFF)
                return false;
            LogLevel effectiveLevel = g_state.level;
            int idx = moduleIndex(module);
            if (idx >= 0)
            {
                LogLevel perModule = g_state.moduleLevels[idx];
                if (perModule != LOG_LEVEL_OFF)
                {
                    effectiveLevel = perModule;
                }
            }

            if (level > effectiveLevel)
                return false;
            if ((g_state.modules & module) == 0)
                return false;
            return true;
        }

        void Logger::log(uint16_t module, LogLevel level, const char *fmt, ...)
        {
            if (!isEnabled(module, level))
                return;
            if (!g_state.initialized)
                init(g_state.level, g_state.modules, g_state.timestamps);
            if (g_state.timestamps)
            {
                uint32_t now = micros();
                uint32_t dt = g_state.startMicros == 0u ? 0u : now - g_state.startMicros;
                uint32_t ms = dt / 1000u;
                uint32_t s = ms / 1000u;
                uint16_t msRem = static_cast<uint16_t>(ms % 1000u);
                Serial.print('[');
                Serial.print(static_cast<unsigned long>(s));
                Serial.print('.');
                if (msRem < 100u)
                    Serial.print('0');
                if (msRem < 10u)
                    Serial.print('0');
                Serial.print(static_cast<unsigned long>(msRem));
                Serial.print("] ");
            }
            Serial.print(levelToString(level));
            Serial.print(' ');
            Serial.print(moduleToString(module));
            Serial.print(": ");
            char buffer[256];
            va_list args;
            va_start(args, fmt);
            vsnprintf(buffer, sizeof(buffer), fmt, args);
            va_end(args);
            Serial.println(buffer);
        }

    }
}

#endif
