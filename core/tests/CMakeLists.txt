# Domain test suite. Runs without JUCE, without Tracktion and without an audio
# device, so it stays fast enough to be run on every save.
add_executable(daw_domain_tests
    TestMain.cpp
    BuildInfoTests.cpp
    ValueTests.cpp
    IdsTests.cpp
    ProjectStateTests.cpp
    PluginStateTests.cpp
    SerializationRoundTripTests.cpp
    CommandBusTests.cpp
    UndoRedoTests.cpp
    CoalescingTests.cpp
    ReplayTests.cpp
    ProvenanceTests.cpp
    TrackCommandTests.cpp
    NoteCommandTests.cpp
    NoteEditCommandTests.cpp
    TempoCommandTests.cpp
    TransportTests.cpp
    ThreadingTests.cpp
    WorkspaceLayoutTests.cpp
    HistoryLogTests.cpp)

target_link_libraries(daw_domain_tests PRIVATE daw::domain daw::ui_workspace doctest::doctest)
daw_set_warnings(daw_domain_tests)

# One ctest entry per TEST_CASE, so a failure names the case, not the binary.
doctest_discover_tests(daw_domain_tests
    TEST_PREFIX "domain."
    ADD_LABELS 0)
