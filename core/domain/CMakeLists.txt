add_library(daw_domain STATIC
    src/BlobRef.cpp
    src/BuildInfo.cpp
    src/Ids.cpp
    src/Result.cpp
    src/Timestamp.cpp
    src/Value.cpp
    src/command/CommandBus.cpp
    src/command/CommandEnvelope.cpp
    src/command/CommandGroup.cpp
    src/command/CommandQueue.cpp
    src/command/CommandRegistry.cpp
    src/command/Provenance.cpp
    src/copilot/MixingReadiness.cpp
    src/copilot/StateView.cpp
    src/copilot/Tools.cpp
    src/copilot/Usage.cpp
    src/stems/Laying.cpp
    src/direction/Reading.cpp
    src/generation/Constraints.cpp
    src/generation/Form.cpp
    src/generation/Generator.cpp
    src/generation/Harmony.cpp
    src/generation/Learning.cpp
    src/generation/Phrase.cpp
    src/generation/Transform.cpp
    src/tidy/Roles.cpp
    src/generation/StyleModel.cpp
    src/commands/AddNote.cpp
    src/commands/AutomationCommands.cpp
    src/commands/NoteCommands.cpp
    src/commands/NoteEditCommands.cpp
    src/commands/LaneCommands.cpp
    src/commands/PatternCommands.cpp
    src/commands/SampleCommands.cpp
    src/commands/CreateMidiClip.cpp
    src/commands/MixCommands.cpp
    src/commands/PluginCommands.cpp
    src/commands/SetTrackVolume.cpp
    src/commands/TempoCommands.cpp
    src/commands/TrackCommands.cpp
    src/commands/TransportCommands.cpp
    src/project/ProjectState.cpp
    src/project/Automation.cpp
    src/project/InternalEffects.cpp
    src/mix/Measurement.cpp
    src/mix/Decision.cpp
    src/serialization/Json.cpp)
add_library(daw::domain ALIAS daw_domain)

target_include_directories(daw_domain PUBLIC include)
target_compile_features(daw_domain PUBLIC cxx_std_20)
target_compile_definitions(daw_domain PRIVATE
    DAW_VERSION_STRING="${PROJECT_VERSION}")

# PRIVATE, and only used by src/serialization/Json.cpp: nlohmann appears in no
# header, so nothing outside this library ever sees it.
target_link_libraries(daw_domain PRIVATE nlohmann_json::nlohmann_json)

daw_set_warnings(daw_domain)

# Rule 3: must be the last call, after every target_link_libraries on daw_domain.
daw_forbid_ui_dependencies(daw_domain)
