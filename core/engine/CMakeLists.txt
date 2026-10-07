# daw_engine — adapter between core/domain and Tracktion Engine.
#
# INTERFACE library, for the same reason as daw_ui: its sources compile inside
# the consuming target, so Tracktion's module code is compiled exactly once per
# binary and no symbol is duplicated.
#
# The dependency goes one way only. This target links daw::domain; the domain
# never links this one, and daw_forbid_ui_dependencies() keeps it that way.

add_library(daw_engine INTERFACE)
add_library(daw::engine ALIAS daw_engine)

target_sources(daw_engine INTERFACE
    src/AudioOutputKeeper.cpp
    src/AudioSettings.cpp
    src/ClapPluginFormat.cpp
    src/EngineHost.cpp
    src/Export.cpp
    src/FluxTap.cpp
    src/FluxTaps.cpp
    src/HostedParameters.cpp
    src/LevelMeters.cpp
    src/LiveInput.cpp
    src/MediaFoundation.cpp
    src/MeterTap.cpp
    src/MixRender.cpp
    src/MixTap.cpp
    src/ParameterBridge.cpp
    src/PluginCatalogue.cpp
    src/ContentStore.cpp
    src/ProjectProjector.cpp
    src/Rendering.cpp
    src/TransportController.cpp
    src/clap/ClapPluginEditor.cpp
    src/clap/ClapPluginInstance.cpp)

target_include_directories(daw_engine INTERFACE include src)

target_link_libraries(daw_engine INTERFACE
    daw::domain
    daw::clap_sdk
    BLAKE3::blake3
    tracktion::tracktion_core
    tracktion::tracktion_engine
    tracktion::tracktion_graph
    juce::juce_audio_devices
    juce::juce_audio_processors
    juce::juce_audio_utils)

# MP3 and AAC go through Windows's own Media Foundation encoders.
if(WIN32)
    target_link_libraries(daw_engine INTERFACE mf mfplat mfreadwrite mfuuid)
endif()

if(DAW_BUILD_TESTS)
    add_subdirectory(tests)
endif()
