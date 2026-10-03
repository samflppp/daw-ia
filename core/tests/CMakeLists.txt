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
    GroupTests.cpp
    CommandQueueTests.cpp
    CopilotViewTests.cpp
    ReplayTests.cpp
    ProvenanceTests.cpp
    TrackCommandTests.cpp
    NoteCommandTests.cpp
    NoteEditCommandTests.cpp
    LaneEditingTests.cpp
    LaneTests.cpp
    MotionTests.cpp
    ReachTests.cpp
    InternalEffectsTests.cpp
    PatternTests.cpp
    RoleTests.cpp
    ZoneProposalTests.cpp
    SampleTests.cpp
    TempoCommandTests.cpp
    TransportTests.cpp
    ThreadingTests.cpp
    WorkspaceLayoutTests.cpp
    HistoryLogTests.cpp
    PreviewTests.cpp
    MixTests.cpp
    MixingReadinessTests.cpp
    NoteClipboardTests.cpp
    TempoEditingTests.cpp
    AutomationTests.cpp
    AutomationEditingTests.cpp
    CanvasTests.cpp
    ViewFramingTests.cpp
    SampleSearchTests.cpp
    CopilotRequestTests.cpp
    FormTests.cpp
    GenerationTests.cpp
    GhostProposalTests.cpp
    LearningTests.cpp
    PhraseTests.cpp
    PromptReaderTests.cpp
    TokenTableTests.cpp
    TransformTests.cpp)

target_link_libraries(daw_domain_tests PRIVATE daw::domain daw::ui_workspace doctest::doctest)
daw_set_warnings(daw_domain_tests)

# The manifests the application embeds, read from disk by the one test that
# checks the shipped beatmaker layout.
target_compile_definitions(daw_domain_tests PRIVATE
    DAW_WORKSPACES_DIR="${PROJECT_SOURCE_DIR}/workspaces"
    # The tokens the interface ships, each read back through the table.
    DAW_TOKENS_FILE="${PROJECT_SOURCE_DIR}/core/ui/tokens/tokens.json"
    # The style model the corpus pipeline writes from its own test corpus: the
    # contract between services/harmony and the generator, pinned on both sides.
    DAW_SERVICES_FIXTURES="${PROJECT_SOURCE_DIR}/services/tests/fixtures")

# One ctest entry per TEST_CASE, so a failure names the case, not the binary.
doctest_discover_tests(daw_domain_tests
    TEST_PREFIX "domain."
    ADD_LABELS 0)
