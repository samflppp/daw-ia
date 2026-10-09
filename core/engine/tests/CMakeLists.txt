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
    EngineHostTests.cpp
    ExportTests.cpp
    ProjectionTests.cpp
    PatternProjectionTests.cpp
    ArrangementTests.cpp
    TransportProjectionTests.cpp
    TempoProjectionTests.cpp
    PanProjectionTests.cpp
    AutomationProjectionTests.cpp
    MeterTests.cpp
    MixProjectionTests.cpp
    SampleProjectionTests.cpp
    PluginHostingTests.cpp
    PluginPersistenceTests.cpp
    GenerationRenderTests.cpp
    InternalEffectsRenderTests.cpp
    MixRenderTests.cpp
    SmartBusSendTests.cpp
    FluxTapTests.cpp
    SampleIndexTests.cpp
    LivePlayTests.cpp)

target_compile_definitions(daw_engine_tests PRIVATE
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

target_link_libraries(daw_engine_tests PRIVATE
    daw::engine
    daw::persistence
    daw::ui_workspace
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

# ---------------------------------------------------------------------------
# A real CLAP plugin, built here, used to prove the CLAP host.
#
# A host cannot be tested against a mock: the question is whether a real shared
# library, loaded through the real CLAP entry point, produces sound in this
# project. A commercial plugin proves that too, but it cannot be committed, it
# is not installed on every machine, and many are silent until their own content
# is loaded ??? which would make a failing test say nothing at all.
# ---------------------------------------------------------------------------
add_library(daw_test_clap MODULE clap_fixture/DawTestClapPlugin.cpp)
set_target_properties(daw_test_clap PROPERTIES
    PREFIX ""
    SUFFIX ".clap"
    POSITION_INDEPENDENT_CODE ON)
target_link_libraries(daw_test_clap PRIVATE daw::clap_sdk)
target_compile_features(daw_test_clap PRIVATE cxx_std_20)
daw_set_warnings(daw_test_clap)

add_dependencies(daw_engine_tests daw_test_clap)
target_compile_definitions(daw_engine_tests PRIVATE
    DAW_TEST_CLAP_FIXTURE="$<TARGET_FILE:daw_test_clap>")

# Our own sources are UTF-8; see cmake/DawWarnings.cmake.
daw_use_utf8_sources(daw_engine_tests)
