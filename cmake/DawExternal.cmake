# Third-party code from external/ (git submodules, pinned commits).
include_guard(GLOBAL)

set(DAW_EXTERNAL_DIR "${PROJECT_SOURCE_DIR}/external")

foreach(dep JUCE tracktion_engine clap vst3sdk)
    if(NOT EXISTS "${DAW_EXTERNAL_DIR}/${dep}/CMakeLists.txt")
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

# --- VST3 SDK (interfaces only) ---------------------------------------------
# JUCE compiles VST3 hosting with its own embedded copy of the SDK. This target
# exposes the official SDK headers for project code that needs them directly.
add_library(daw_vst3_sdk INTERFACE)
target_include_directories(daw_vst3_sdk SYSTEM INTERFACE "${DAW_EXTERNAL_DIR}/vst3sdk")
add_library(daw::vst3_sdk ALIAS daw_vst3_sdk)
