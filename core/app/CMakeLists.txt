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
    src/MainWindow.cpp)

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
