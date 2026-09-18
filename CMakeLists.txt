cmake_minimum_required(VERSION 3.25)

project(DawIA
    VERSION 0.1.0
    DESCRIPTION "AI-assisted DAW"
    LANGUAGES C CXX)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
# DAW_BUILD_APP=OFF builds only the pure C++ domain (no JUCE, no Tracktion):
# fast, no submodules needed. Useful for domain work and quick CI feedback.
option(DAW_BUILD_APP   "Build the JUCE/Tracktion application" ON)
option(DAW_BUILD_TESTS "Build the core test suite"            ON)
option(DAW_WARNINGS_AS_ERRORS "Treat warnings as errors in project code" ON)

# ---------------------------------------------------------------------------
# Global settings (must be set before any target is created)
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Static MSVC runtime, same as Tracktion's own builds.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
# Embedded debug info (/Z7) so compiler caches (sccache) work with MSVC.
set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Debug,RelWithDebInfo>:Embedded>")
# JUCE requires CMake 3.22, so CMP0141 is unset in its scope and
# juce_recommended_config_flags adds /Zi, which overrides /Z7 (warning D9025).
set(CMAKE_POLICY_DEFAULT_CMP0141 NEW)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(DawWarnings)
include(DawGuards)

if(DAW_BUILD_TESTS)
    enable_testing()
endif()

# ---------------------------------------------------------------------------
# Third-party (fetched archives: nlohmann/json, doctest)
# ---------------------------------------------------------------------------
include(DawThirdParty)

# ---------------------------------------------------------------------------
# Third-party (git submodules in external/)
# ---------------------------------------------------------------------------
if(DAW_BUILD_APP)
    include(DawExternal)
endif()

# ---------------------------------------------------------------------------
# Modules
# ---------------------------------------------------------------------------
# services/ (Python) and workspaces/ (JSON manifests) are not CMake modules.
# They have their own tooling and CI jobs.
add_subdirectory(core)
