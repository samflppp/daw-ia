cmake_minimum_required(VERSION 3.25)

project(DawIA
    VERSION 0.1.0
    DESCRIPTION "AI-assisted DAW"
    LANGUAGES C CXX)

# The version as people read it: the alpha of the prototype (decided on
# 9 October 2026). Windows takes numbers only in the executable's own
# version, which stays PROJECT_VERSION.
set(DAW_VERSION_LABEL "${PROJECT_VERSION}-alpha")

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

# A compiler that does not speak English writes its /showIncludes prefix with
# characters outside ASCII ("Remarque : inclusion du fichier :", non-breaking
# spaces). Ninja then records header dependencies only when the build runs
# under the code page CMake read the prefix in (S22, see scripts/build.cmd):
# said once, at configure time, rather than found as a stale object.
if(MSVC AND CMAKE_GENERATOR MATCHES "Ninja" AND CMAKE_CXX_CL_SHOWINCLUDES_PREFIX MATCHES "[^ -~]")
    message(STATUS
        "MSVC is localised: build with scripts\\build.cmd (code page 65001), or Ninja loses the "
        "header dependencies of what it compiles")
endif()

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
