#ifndef DEBUG_CONFIG_H
#define DEBUG_CONFIG_H

#if defined(PIP3D_DEBUG) || !defined(NDEBUG)
#define PIP3D_ENABLE_DEBUG 1
#else
#define PIP3D_ENABLE_DEBUG 0
#endif

#ifndef ENABLE_LOGGING
#define ENABLE_LOGGING 1
#endif

#ifndef LOG_DEFAULT_LEVEL
#define LOG_DEFAULT_LEVEL 1
#endif

#ifndef ENABLE_DEBUG_DRAW
#define ENABLE_DEBUG_DRAW 0
#endif

#endif
