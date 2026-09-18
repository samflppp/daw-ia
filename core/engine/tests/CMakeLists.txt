# Engine test suite. Unlike the domain suite it needs JUCE, Tracktion and an
# audio device, so it is built everywhere but run only where a device exists.
#
# The ctest label "audio" is what CI excludes. Run them locally with:
#   ctest --preset windows-msvc -L audio

juce_add_console_app(daw_engine_tests
    PRODUCT_NAME "daw_engine_tests"
    NEEDS_CURL FALSE
    NEEDS_WEB_BROWSER FALSE)

target_sources(daw_engine_tests PRIVATE
    EngineTestMain.cpp
    ProjectionTests.cpp
    TransportProjectionTests.cpp)

target_compile_definitions(daw_engine_tests PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_LADSPA=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_MODAL_LOOPS_PERMITTED=1
    TRACKTION_ENABLE_TIMESTRETCH_SOUNDTOUCH=1)

target_link_libraries(daw_engine_tests PRIVATE
    daw::engine
    doctest::doctest
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

# JUCE and Tracktion sources compile inside this target, so project-level
# -Werror is not applied here, exactly as for daw_app.

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(daw_engine_tests PRIVATE atomic)
endif()

doctest_discover_tests(daw_engine_tests
    TEST_PREFIX "engine."
    PROPERTIES LABELS "audio")
