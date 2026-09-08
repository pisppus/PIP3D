cmake_minimum_required(VERSION 3.20)
project(pip3d_bake CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT DEFINED BAKE_ROOT)
    get_filename_component(BAKE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}" DIRECTORY)
    get_filename_component(BAKE_ROOT "${BAKE_ROOT}" DIRECTORY)
    get_filename_component(BAKE_ROOT "${BAKE_ROOT}" DIRECTORY)
endif()

set(PIP3D_SOURCE_DIR "${BAKE_ROOT}/lib/Pip3D")

set(PIP3D_SCREEN_WIDTH "480" CACHE STRING "Screen width the bake previews render at")
set(PIP3D_SCREEN_HEIGHT "320" CACHE STRING "Screen height the bake previews render at")
set(PIP3D_SCREEN_BAND_COUNT "8" CACHE STRING "Screen band count the bake previews render at")

if(NOT EXISTS "${PIP3D_SOURCE_DIR}/Pip3D.hpp")
    message(FATAL_ERROR "Pip3D engine not found at ${PIP3D_SOURCE_DIR}")
endif()

set(BAKE_SHIMS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Platform")

file(GLOB_RECURSE BAKE_ENGINE_SOURCES CONFIGURE_DEPENDS
    "${PIP3D_SOURCE_DIR}/*.cpp"
)
if(EXISTS "${BAKE_ROOT}/src/main.cpp")
    list(APPEND BAKE_ENGINE_SOURCES "${BAKE_ROOT}/src/main.cpp")
else()
    message(WARNING "src/main.cpp not found — bake may miss scene definition")
endif()

list(FILTER BAKE_ENGINE_SOURCES EXCLUDE REGEX ".*/Platforms/Desktop/Host\\.cpp$")
list(FILTER BAKE_ENGINE_SOURCES EXCLUDE REGEX ".*/Platforms/Desktop/Runtime\\.cpp$")
list(FILTER BAKE_ENGINE_SOURCES EXCLUDE REGEX ".*/Platforms/Desktop/Platform\\.cpp$")
list(FILTER BAKE_ENGINE_SOURCES EXCLUDE REGEX ".*/Platforms/Desktop/Audio\\.cpp$")
list(FILTER BAKE_ENGINE_SOURCES EXCLUDE REGEX ".*/Displays/Simulator/Display\\.cpp$")
list(FILTER BAKE_ENGINE_SOURCES EXCLUDE REGEX ".*/Platforms/ESP32/.*$")

list(LENGTH BAKE_ENGINE_SOURCES BAKE_SOURCES_COUNT)
message(STATUS "Pip3D bake: engine .cpp sources: ${BAKE_SOURCES_COUNT} files")

add_executable(bake
    ${BAKE_ENGINE_SOURCES}
    Entry.cpp
    Core/Run.cpp
    Platform/Runtime.cpp
    Core/Config.cpp
    Core/Progress.cpp
    Scene/Collect.cpp
    Geometry/Plan.cpp
    Geometry/Unwrap.cpp
    Trace/Bvh.cpp
    Trace/Lightmap.cpp
    Trace/Probes.cpp
    Encode/Atlas.cpp
    Encode/Palette.cpp
    Encode/Preview.cpp
    GPU/Dispatch.cpp
    GPU/Vulkan.cpp
)

target_compile_definitions(bake PRIVATE
    PIPCORE_TARGET_DESKTOP=1
    PIPCORE_TARGET_ESP32=0
    PIPCORE_PLATFORM=2
    PIPCORE_DISPLAY=3
    PIP3D_PC=1
    PIP3D_TARGET_PC=1
    PIP3D_BAKE=1
    PIP3D_SCREEN_WIDTH=${PIP3D_SCREEN_WIDTH}
    PIP3D_SCREEN_HEIGHT=${PIP3D_SCREEN_HEIGHT}
    PIP3D_SCREEN_BAND_COUNT=${PIP3D_SCREEN_BAND_COUNT}
    PIP3D_DISPLAY_ROTATION=0
    PIPCORE_ENABLE_AUDIO=0
    PIPCORE_ENABLE_TOUCH=0
    PIPCORE_ENABLE_WIFI=0
    PIPCORE_ENABLE_OTA=0
    PIPCORE_ENABLE_DEBUG=0
    PIPCORE_ENABLE_PREFS=0
    PIPGUI_SIM_USE_WX=0
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    _CRT_SECURE_NO_WARNINGS
)

find_package(Threads REQUIRED)
target_link_libraries(bake PRIVATE Threads::Threads)

if(WIN32)
    target_link_libraries(bake PRIVATE kernel32 user32)
else()
    find_library(DL_LIB dl)
    if(DL_LIB)
        target_link_libraries(bake PRIVATE ${DL_LIB})
    endif()
endif()

target_include_directories(bake PRIVATE
    ${BAKE_SHIMS_DIR}
    ${PIP3D_SOURCE_DIR}
    ${PIP3D_SOURCE_DIR}/Pip3D
    ${PIP3D_SOURCE_DIR}/PipCore
    ${BAKE_ROOT}/src
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/external/vulkan/include
)

set_target_properties(bake PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${BAKE_ROOT}/Tools/Bake/Build"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${BAKE_ROOT}/Tools/Bake/Build"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${BAKE_ROOT}/Tools/Bake/Build"
)

if(MSVC)
    target_compile_options(bake PRIVATE 
        /utf-8 
        /W3 
        /permissive- 
        /Zc:preprocessor 
        /constexpr:steps2147483647 
        /constexpr:depth2048
    )
    target_link_options(bake PRIVATE "LINKER:/STACK:67108864" "LINKER:/MAP")
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_compile_options(bake PRIVATE /Od /Zi /RTC1)
    else()
        target_compile_options(bake PRIVATE /O2)
    endif()
else()
    target_compile_options(bake PRIVATE -O2 -fconstexpr-ops-limit=2147483647)
endif()