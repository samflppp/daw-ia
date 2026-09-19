# Third-party code from external/ (git submodules, pinned commits).
include_guard(GLOBAL)

set(DAW_EXTERNAL_DIR "${PROJECT_SOURCE_DIR}/external")

foreach(dep JUCE/CMakeLists.txt tracktion_engine/CMakeLists.txt clap/CMakeLists.txt
        vst3sdk/CMakeLists.txt BLAKE3/c/CMakeLists.txt)
    if(NOT EXISTS "${DAW_EXTERNAL_DIR}/${dep}")
        message(FATAL_ERROR
            "external/${dep} is missing. Run ./scripts/bootstrap-submodules.sh, "
            "or configure with -DDAW_BUILD_APP=OFF to build the domain only.")
    endif()
endforeach()

# --- JUCE ------------------------------------------------------------------
add_subdirectory("${DAW_EXTERNAL_DIR}/JUCE" "${CMAKE_BINARY_DIR}/external/JUCE" EXCLUDE_FROM_ALL)

# --- Tracktion Engine --------------------------------------------------------
# Only the modules folder: the repository root would add its own JUCE copy
# (modules/juce) and all examples.
add_subdirectory("${DAW_EXTERNAL_DIR}/tracktion_engine/modules"
    "${CMAKE_BINARY_DIR}/external/tracktion" EXCLUDE_FROM_ALL)

# --- CLAP (header-only) ------------------------------------------------------
add_library(daw_clap_sdk INTERFACE)
target_include_directories(daw_clap_sdk SYSTEM INTERFACE "${DAW_EXTERNAL_DIR}/clap/include")
add_library(daw::clap_sdk ALIAS daw_clap_sdk)

# --- BLAKE3 ------------------------------------------------------------------
# Content addressing for plugin state blobs: the digest is the key, so two
# identical states are stored once and a damaged blob is detected on read.
#
# SIMD is pinned to the intrinsics path on purpose. The default on x86-64 is
# hand-written assembly, which would add the MASM language on Windows and make
# the build depend on one more assembler; the intrinsics path hashes a plugin
# state in well under a millisecond, which is all this project asks of it.
set(BLAKE3_SIMD_TYPE "x86-intrinsics" CACHE STRING "" FORCE)
set(BLAKE3_USE_TBB OFF CACHE BOOL "" FORCE)
set(BLAKE3_FETCH_TBB OFF CACHE BOOL "" FORCE)
set(BLAKE3_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory("${DAW_EXTERNAL_DIR}/BLAKE3/c" "${CMAKE_BINARY_DIR}/external/BLAKE3" EXCLUDE_FROM_ALL)

# --- VST3 SDK (interfaces only) ---------------------------------------------
# JUCE compiles VST3 hosting with its own embedded copy of the SDK. This target
# exposes the official SDK headers for project code that needs them directly.
add_library(daw_vst3_sdk INTERFACE)
target_include_directories(daw_vst3_sdk SYSTEM INTERFACE "${DAW_EXTERNAL_DIR}/vst3sdk")
add_library(daw::vst3_sdk ALIAS daw_vst3_sdk)
