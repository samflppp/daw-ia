# Persistence test suite. No JUCE, no audio device: it runs in the
# `domain-only` preset like the domain suite.
#
# It launches itself as a child process, so the binary has two jobs and its
# own main(). Measuring persistence means closing a process and opening
# another one; a loader called twice in one process proves nothing.
add_executable(daw_persistence_tests
    PersistenceTestMain.cpp
    PersistenceTestSupport.cpp
    ProjectStoreTests.cpp)

target_link_libraries(daw_persistence_tests PRIVATE daw::persistence doctest::doctest)
daw_set_warnings(daw_persistence_tests)

doctest_discover_tests(daw_persistence_tests
    TEST_PREFIX "persistence."
    ADD_LABELS 0)
