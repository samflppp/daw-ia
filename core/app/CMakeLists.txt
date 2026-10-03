juce_add_gui_app(daw_app
    PRODUCT_NAME "DAW IA"
    COMPANY_NAME "DAW IA"
    BUNDLE_ID "com.dawia.app"
    VERSION "${PROJECT_VERSION}"
    MICROPHONE_PERMISSION_ENABLED TRUE
    NEEDS_CURL FALSE
    NEEDS_WEB_BROWSER FALSE)

target_sources(daw_app PRIVATE
    src/Main.cpp
    src/MainWindow.cpp
    src/AppShellView.cpp
    src/CopilotBridge.cpp
    src/DisplayMode.cpp
    src/PromptReading.cpp
    src/Listening.cpp
    src/PluginWindow.cpp
    src/PluginRack.cpp
    src/EditClock.cpp
    src/LevelMonitor.cpp
    src/MixComparison.cpp
    src/MixSession.cpp
    src/TransportSync.cpp
    src/PlaybackProbe.cpp
    src/NativeWindow.cpp
    src/QuitWatchdog.cpp
    src/Verification.cpp
    src/VerificationAutomation.cpp
    src/VerificationFluidity.cpp
    src/VerificationForm.cpp
    src/VerificationGeneration.cpp
    src/VerificationZone.cpp
    src/VerificationCanvas.cpp
    src/VerificationLearning.cpp
    src/VerificationMix.cpp
    src/VerificationPlayback.cpp
    src/VerificationTiming.cpp
    src/SampleLibrary.cpp
    src/SamplePreview.cpp
    src/SongExporter.cpp
    src/WorkspaceSwitch.cpp)

target_include_directories(daw_app PRIVATE src)

target_compile_definitions(daw_app PRIVATE
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:daw_app,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:daw_app,JUCE_VERSION>"
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_LADSPA=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_MODAL_LOOPS_PERMITTED=1
    TRACKTION_ENABLE_TIMESTRETCH_SOUNDTOUCH=1)

target_link_libraries(daw_app PRIVATE
    daw::domain
    daw::engine
    daw::persistence
    daw::ui
    tracktion::tracktion_core
    tracktion::tracktion_engine
    tracktion::tracktion_graph
    juce::juce_audio_devices
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

# JUCE and Tracktion sources compile inside this target, so project-level
# -Werror is not applied here. Warnings stay visible in the build log.

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(daw_app PRIVATE atomic)
endif()

# Our own sources are UTF-8; see cmake/DawWarnings.cmake.
daw_use_utf8_sources(daw_app)
