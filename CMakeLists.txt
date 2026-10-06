cmake_minimum_required(VERSION 3.22)

# Universal binaries + a sensible deployment target on macOS (must be set before project()).
if(APPLE)
    if(NOT CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures" FORCE)
    endif()
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum macOS version")
endif()

project(AdventurePack VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

if(MSVC)
    # Static runtime: the plugins don't depend on the user having the VC++ redistributable installed.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# ---------------------------------------------------------------------------------------------
# JUCE. Fetched automatically; pass -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE to use a local copy.
# ---------------------------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 9.0.3
    GIT_SHALLOW TRUE
    GIT_PROGRESS TRUE)
FetchContent_MakeAvailable(JUCE)

# ---------------------------------------------------------------------------------------------
# Which plugin formats to build. Ableton Live loads VST3 everywhere and AU on macOS.
# ---------------------------------------------------------------------------------------------
if(APPLE)
    set(AA_DEFAULT_FORMATS "VST3;AU")
else()
    set(AA_DEFAULT_FORMATS "VST3")
endif()
set(AA_FORMATS "${AA_DEFAULT_FORMATS}" CACHE STRING "Plugin formats to build (e.g. VST3;AU;Standalone)")
set(AA_ONLY "" CACHE STRING "Optional: build only this plugin folder (e.g. Stardust) for faster iteration")
option(AA_BUILD_HARNESS "Build the offline test harness (tools/harness)" OFF)

include(cmake/AdventurePlugin.cmake)

add_subdirectory(kit)

set(AA_PLUGINS
    Stardust
    CritterKit
    Orrery
    Babble
    Boing
    Gremlin
    TapeDreams
    CrystalCave
    Dandelion)

# Plugins still being finished: only built when asked for explicitly with -DAA_ONLY=<Name>.
set(AA_WIP_PLUGINS Stardust CritterKit Orrery Babble)

foreach(plugin IN LISTS AA_PLUGINS)
    if((AA_ONLY STREQUAL "" AND NOT plugin IN_LIST AA_WIP_PLUGINS) OR AA_ONLY STREQUAL plugin)
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/plugins/${plugin}/CMakeLists.txt")
            add_subdirectory(plugins/${plugin})
        endif()
    endif()
endforeach()

if(AA_BUILD_HARNESS)
    add_subdirectory(tools/harness)
endif()
