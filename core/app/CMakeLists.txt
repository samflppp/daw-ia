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
    src/About.cpp
    src/CrashGuard.cpp
    src/ProcessTree.cpp
    src/ApiKey.cpp
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
    src/LivePlay.cpp
    src/SoundCard.cpp
    src/KitSession.cpp
    src/BusSession.cpp
    src/RawKeyboard.cpp
    src/TakeRecorder.cpp
    src/MidiKeyboards.cpp
    src/MixComparison.cpp
    src/MixOnce.cpp
    src/MixSession.cpp
    src/TransportSync.cpp
    src/PlaybackProbe.cpp
    src/NativeWindow.cpp
    src/NativeAudio.cpp
    src/Microphone.cpp
    src/VoiceService.cpp
    src/VoiceInput.cpp
    src/QuitWatchdog.cpp
    src/Verification.cpp
    src/VerificationKit.cpp
    src/VerificationVoice.cpp
    src/VerificationKey.cpp
    src/VerificationRights.cpp
    src/VerificationCrash.cpp
    src/VerificationAutomation.cpp
    src/VerificationFluidity.cpp
    src/VerificationForm.cpp
    src/VerificationGeneration.cpp
    src/VerificationZone.cpp
    src/VerificationCanvas.cpp
    src/VerificationLearning.cpp
    src/VerificationMix.cpp
    src/VerificationStems.cpp
    src/VerificationPlayback.cpp
    src/VerificationAudio.cpp
    src/VerificationFlux.cpp
    src/VerificationLive.cpp
    src/VerificationTiming.cpp
    src/VerificationWindows.cpp
    src/SampleLibrary.cpp
    src/SamplePreview.cpp
    src/SongExporter.cpp
    src/StemSeparation.cpp
    src/StemSession.cpp
    src/DirectionSession.cpp
    src/WorkspaceSwitch.cpp)

target_include_directories(daw_app PRIVATE src)

# The commit this binary was built from, read at each build (S26).
add_custom_target(daw_build_info
    COMMAND "${CMAKE_COMMAND}"
        -DSOURCE=${PROJECT_SOURCE_DIR}
        -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/generated/BuildInfo.h
        -P ${PROJECT_SOURCE_DIR}/cmake/DawBuildInfo.cmake
    BYPRODUCTS ${CMAKE_CURRENT_BINARY_DIR}/generated/BuildInfo.h
    VERBATIM)
add_dependencies(daw_app daw_build_info)
target_include_directories(daw_app PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)

target_compile_definitions(daw_app PRIVATE
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:daw_app,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:daw_app,JUCE_VERSION>"
    DAW_VERSION_LABEL="${DAW_VERSION_LABEL}"
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_LADSPA=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_MODAL_LOOPS_PERMITTED=1
    # No time-stretcher (S26): the DAW never stretches a clip, and SoundTouch,
    # Tracktion's default once compiled, is LGPL-2.1 - linked statically into a
    # closed binary, it would oblige us to ship what relinks it.
    TRACKTION_ENABLE_TIMESTRETCH_SOUNDTOUCH=0)

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
