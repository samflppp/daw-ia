#include "PersistenceTestSupport.h"

#include "daw/domain/command/CommandBus.h"
#include "daw/domain/command/CommandRegistry.h"
#include "daw/domain/commands/AddNote.h"
#include "daw/domain/commands/CreateMidiClip.h"
#include "daw/domain/commands/DirectionCommands.h"
#include "daw/domain/commands/PatternCommands.h"
#include "daw/domain/commands/PluginCommands.h"
#include "daw/domain/commands/SampleCommands.h"
#include "daw/domain/commands/SetTrackVolume.h"
#include "daw/domain/commands/TrackCommands.h"
#include "daw/domain/project/ProjectState.h"
#include "daw/domain/serialization/Json.h"
#include "daw/persistence/Database.h"
#include "daw/persistence/ProjectStore.h"

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <random>
#include <sstream>
#include <system_error>

namespace daw::testing
{
namespace
{

std::filesystem::path executable;
std::atomic<int> folderCounter{0};

// Identifiers are fixed, not generated: the parent process has to name the
// same track and the same clip as the child, and a ULID generated on each side
// would name two different things.
constexpr std::string_view trackIdText = "01JBWQ7Z0000000000000TRACK";
constexpr std::string_view clipIdText = "01JBWQ7Z0000000000000CL1P0";

// The pattern the rack of S9 would have made, named the same way on both
// sides for the same reason.
constexpr std::string_view rackPatternIdText = "01JBWQ7Z0000000000PATTERN0";
constexpr std::string_view rackRowIdText = "01JBWQ7Z00000000000R0W0000";
constexpr std::string_view rackPlacementIdText = "01JBWQ7Z0000000000P0SE0000";

} // namespace

void setExecutablePath(const char* path)
{
    if (path != nullptr)
        executable = std::filesystem::absolute(std::filesystem::path{path});
}

const std::filesystem::path& executablePath()
{
    return executable;
}

int runChildProcess(const std::vector<std::string>& arguments)
{
    if (executable.empty())
        return -1;

    std::ostringstream command;
    command << '"' << executable.string() << '"';
    for (const auto& argument : arguments)
        command << " \"" << argument << '"';

        // On Windows the whole line goes to cmd.exe, which strips one layer of
        // quotes; wrapping it again is what keeps a path with a space intact.
#ifdef _WIN32
    const auto line = "\"" + command.str() + "\"";
#else
    const auto line = command.str();
#endif

    return std::system(line.c_str());
}

TemporaryFolder::TemporaryFolder(const std::string& label)
{
    const auto unique = label + "-" + std::to_string(folderCounter.fetch_add(1)) + "-" +
                        std::to_string(static_cast<long long>(std::random_device{}()));
    path_ = std::filesystem::temp_directory_path() / ("daw-tests-" + unique);

    std::error_code code;
    std::filesystem::create_directories(path_, code);
}

TemporaryFolder::~TemporaryFolder()
{
    std::error_code code;
    std::filesystem::remove_all(path_, code);
}

std::filesystem::path TemporaryFolder::child(std::string_view name) const
{
    return path_ / std::filesystem::path{name};
}

std::string readTextFile(const std::filesystem::path& file)
{
    std::ifstream stream{file, std::ios::binary};
    if (!stream)
        return {};

    std::ostringstream text;
    text << stream.rdbuf();
    return text.str();
}

bool writeTextFile(const std::filesystem::path& file, const std::string& text)
{
    std::ofstream stream{file, std::ios::binary | std::ios::trunc};
    if (!stream)
        return false;

    stream << text;
    return stream.good();
}

namespace scenarios
{
namespace
{

using namespace daw::domain;

struct Session
{
    ProjectState state;
    CommandRegistry registry{CommandRegistry::withBuiltinCommands()};
    CommandBus bus{state, registry};
};

[[nodiscard]] TrackId fixedTrackId()
{
    return TrackId::parse(trackIdText).value();
}

[[nodiscard]] ClipId fixedClipId()
{
    return ClipId::parse(clipIdText).value();
}

} // namespace

int writeGroupSession(const std::filesystem::path& projectFolder)
{
    auto store = persistence::ProjectStore::open(persistence::ProjectFolder{projectFolder});
    if (!store)
        return 2;

    Session session;
    store.value()->startRecording(session.bus);

    const auto trackId = fixedTrackId();
    if (!session.bus.execute(std::make_unique<AddTrack>(trackId, "Basse", 0.0)))
        return 3;

    const auto groupedTrack = TrackId::parse("01JBWQ7Z000000000000TRACK4").value();

    PluginInstance vital{};
    vital.id = PluginId::parse("01JBWQ7Z00000000000PG1N000").value();
    vital.ref.format = std::string{PluginRef::clapFormat};
    vital.ref.identifier = "audio.vital.synth";
    vital.ref.name = "Vital";

    std::vector<std::unique_ptr<Command>> commands;
    commands.push_back(std::make_unique<AddTrack>(groupedTrack, "Copilote", 0.0));
    commands.push_back(std::make_unique<InsertPlugin>(groupedTrack, vital, 0));

    GroupOptions options{};
    options.label = "ajoute une piste Basse et mets-y Vital";
    options.origin.actor = Actor::copilot;

    if (!session.bus.executeGroup(std::move(commands), std::move(options)))
        return 4;

    store.value()->stopRecording();
    if (!store.value()->close())
        return 5;

    return 0;
}

int writeLegacySession(const std::filesystem::path& projectFolder)
{
    {
        auto store = persistence::ProjectStore::open(persistence::ProjectFolder{projectFolder});
        if (!store)
            return 2;

        Session session;
        store.value()->startRecording(session.bus);

        const auto trackId = fixedTrackId();
        const auto clipId = fixedClipId();

        if (!session.bus.execute(std::make_unique<AddTrack>(trackId, "Kick", -3.0)))
            return 3;

        // The payload eight weeks of journals hold: a track, a clip, a start
        // and a length. Not one character of it changed when the pattern model
        // landed, which is the whole reason this file needs no conversion.
        if (!session.bus.execute(std::make_unique<CreateMidiClip>(trackId, clipId, 8.0, 4.0)))
            return 4;

        int beat = 0;
        for (const int pitch : {36, 36, 38, 36})
        {
            Note note{};
            note.id = NoteId::parse("01JBWQ7Z00000000000L3G4CY" + std::to_string(beat)).value();
            note.pitch = pitch;
            note.velocity = 100;
            note.startBeats = static_cast<double>(beat);
            note.lengthBeats = 0.25;
            ++beat;

            if (!session.bus.execute(std::make_unique<AddNote>(clipId, note)))
                return 5;
        }

        store.value()->stopRecording();
        if (!store.value()->close())
            return 6;
    }

    // Back to the version that build wrote. The rows are untouched: only the
    // number changes, so the parent opens a file that really is one version
    // behind and really has to be migrated.
    auto database = persistence::Database::open(persistence::ProjectFolder{projectFolder}.databaseFile());
    if (!database)
        return 7;

    if (!database.value().execute("UPDATE meta SET value = '3' WHERE key = 'schema_version'"))
        return 8;

    return 0;
}

int writeSession(const std::filesystem::path& projectFolder, const std::filesystem::path& stateFile)
{
    auto store = persistence::ProjectStore::open(persistence::ProjectFolder{projectFolder});
    if (!store)
        return 2;

    Session session;
    store.value()->startRecording(session.bus);

    const auto trackId = fixedTrackId();
    const auto clipId = fixedClipId();

    if (!session.bus.execute(std::make_unique<AddTrack>(trackId, "Basse", 0.0)))
        return 3;

    if (!session.bus.execute(std::make_unique<CreateMidiClip>(trackId, clipId, 0.0, 8.0)))
        return 4;

    int beat = 0;
    for (const int pitch : {48, 52, 55, 60})
    {
        Note note{};
        note.id = NoteId::parse("01JBWQ7Z000000000000N0TE0" + std::to_string(beat)).value();
        note.pitch = pitch;
        note.velocity = 100;
        note.startBeats = static_cast<double>(beat);
        note.lengthBeats = 0.5;
        ++beat;

        if (!session.bus.execute(std::make_unique<AddNote>(clipId, note)))
            return 5;
    }

    // A fader sweep: one history entry, many journal rows.
    const auto gesture = session.bus.beginGesture("fader volume");
    for (int frame = 0; frame < 20; ++frame)
    {
        if (!session.bus.execute(
                std::make_unique<SetTrackVolume>(trackId, -0.25 * static_cast<double>(frame)),
                ExecuteOptions{gesture}))
            return 6;
    }
    if (!session.bus.endGesture(gesture))
        return 7;

    // A command from the copilot, with the context it acted upon.
    BlobRef context{};
    context.digest = std::string(BlobRef::digestLength, 'b');
    context.byteCount = 2048;

    // A direction by references (S22): read numbers, a correction, an amount.
    // It must come back whole in the process that reopens.
    direction::Direction wanted;
    direction::Reference reference;
    reference.reading.name = "Reference.wav";
    reference.reading.digest = std::string(64, 'f');
    reference.reading.seconds = 184.5;
    reference.reading.bpm = 121.5;
    reference.reading.bpmConfidence = 0.8;
    reference.reading.key = generation::Key{9, generation::Mode::minor};
    reference.reading.keyConfidence = 0.3;
    reference.reading.crestDb = 11.2;
    reference.reading.stems["vocals"] = direction::StemReading{-15.0, -4.5, 0.6};
    reference.weight = 2.0;
    wanted.references.push_back(reference);
    wanted.corrections.bpm = 120.0;
    wanted.amount = 0.8;
    if (!session.bus.execute(std::make_unique<SetDirection>(wanted)))
        return 13;

    Provenance copilot{};
    copilot.actor = Actor::copilot;
    copilot.context = context;

    const auto secondTrack = TrackId::parse("01JBWQ7Z000000000000TRACK2").value();
    if (!session.bus.execute(std::make_unique<AddTrack>(secondTrack, "Copilote", -6.0),
                             ExecuteOptions{{}, copilot}))
        return 8;

    // And one command from the generative engine, undone right after: the
    // journal keeps both rows, and reopening must not bring the track back.
    Provenance generator{};
    generator.actor = Actor::generator;

    const auto thirdTrack = TrackId::parse("01JBWQ7Z000000000000TRACK3").value();
    if (!session.bus.execute(std::make_unique<AddTrack>(thirdTrack, "Generee", 0.0),
                             ExecuteOptions{{}, generator}))
        return 9;

    if (!session.bus.undo())
        return 10;

    if (!writeTextFile(stateFile, json::write(session.state.toValue())))
        return 11;

    store.value()->stopRecording();
    if (!store.value()->close())
        return 12;

    return 0;
}

int writeLargeSession(const std::filesystem::path& projectFolder,
                      const std::filesystem::path& stateFile,
                      int commands)
{
    auto store = persistence::ProjectStore::open(persistence::ProjectFolder{projectFolder});
    if (!store)
        return 2;

    Session session;
    store.value()->startRecording(session.bus);

    const auto trackId = fixedTrackId();
    const auto clipId = fixedClipId();

    if (!session.bus.execute(std::make_unique<AddTrack>(trackId, "Volumineux", 0.0)))
        return 3;

    if (!session.bus.execute(
            std::make_unique<CreateMidiClip>(trackId, clipId, 0.0, static_cast<double>(commands))))
        return 4;

    for (int index = 0; index < commands; ++index)
    {
        Note note{};
        note.id = NoteId::generate();
        note.pitch = 36 + (index % 48);
        note.velocity = 100;
        note.startBeats = static_cast<double>(index) * 0.25;
        note.lengthBeats = 0.25;

        if (!session.bus.execute(std::make_unique<AddNote>(clipId, note)))
            return 5;
    }

    if (!writeTextFile(stateFile, json::write(session.state.toValue())))
        return 6;

    store.value()->stopRecording();
    if (!store.value()->close())
        return 7;

    return 0;
}

int extendAsRackSession(const std::filesystem::path& projectFolder)
{
    auto store = persistence::ProjectStore::open(persistence::ProjectFolder{projectFolder});
    if (!store)
        return 2;

    Session session;
    if (!store.value()->replayInto(session.bus))
        return 3;

    store.value()->startRecording(session.bus);

    const auto trackId = fixedTrackId();
    const auto patternId = PatternId::parse(rackPatternIdText).value();
    const auto rowId = ClipId::parse(rackRowIdText).value();
    const auto placementId = PlacementId::parse(rackPlacementIdText).value();

    // "+ Pattern" as the rack of S9 did it: the pattern and a placement after
    // everything already laid down, in one group.
    {
        std::vector<std::unique_ptr<Command>> commands;
        commands.push_back(std::make_unique<CreatePattern>(patternId, std::string{}, 4.0));
        commands.push_back(std::make_unique<PlacePattern>(placementId, patternId, 12.0));

        GroupOptions options{};
        options.label = "créer un pattern";
        if (!session.bus.executeGroup(std::move(commands), std::move(options)))
            return 4;
    }

    // The first lit cell opens the row, in the same group.
    const auto hit = [](double beat)
    {
        Note note{};
        note.id = NoteId::generate();
        note.pitch = 42;
        note.velocity = 90;
        note.startBeats = beat;
        note.lengthBeats = 0.25;
        return note;
    };

    {
        std::vector<std::unique_ptr<Command>> commands;
        commands.push_back(std::make_unique<AddPatternTrack>(patternId, rowId, trackId));
        commands.push_back(std::make_unique<AddNote>(rowId, hit(0.0)));

        GroupOptions options{};
        options.label = "allumer un pas";
        if (!session.bus.executeGroup(std::move(commands), std::move(options)))
            return 5;
    }

    // And a stroke across three more cells: one gesture.
    const auto gesture = session.bus.beginGesture("peindre des pas");
    for (const double beat : {1.0, 2.0, 3.0})
    {
        if (!session.bus.execute(std::make_unique<AddNote>(rowId, hit(beat)), ExecuteOptions{gesture}))
            return 6;
    }
    if (!session.bus.endGesture(gesture))
        return 7;

    store.value()->stopRecording();
    if (!store.value()->close())
        return 8;

    return 0;
}

int writeS16Session(const std::filesystem::path& projectFolder, const std::filesystem::path& stateFile)
{
    {
        auto store = persistence::ProjectStore::open(persistence::ProjectFolder{projectFolder});
        if (!store)
            return 2;

        Session session;
        store.value()->startRecording(session.bus);

        const auto kick = TrackId::parse(s16::kickTrack).value();
        const auto bass = TrackId::parse(s16::bassTrack).value();
        const auto drums = PatternId::parse(s16::drumPattern).value();
        const auto line = PatternId::parse(s16::bassPattern).value();
        const auto drumRow = ClipId::parse(s16::drumRow).value();
        const auto bassRow = ClipId::parse(s16::bassRow).value();

        // Each command below is built without a line, so its payload is the
        // one S16 wrote, byte for byte: the fields S17 added are written only
        // when they say something.
        std::vector<std::unique_ptr<Command>> commands;
        commands.push_back(std::make_unique<AddTrack>(kick, "Kick", 0.0));
        commands.push_back(std::make_unique<AddTrack>(bass, "Bass", -3.0));
        commands.push_back(std::make_unique<CreatePattern>(drums, "Beat", 4.0));
        commands.push_back(std::make_unique<AddPatternTrack>(drums, drumRow, kick));
        commands.push_back(std::make_unique<CreatePattern>(line, "Bass", 4.0));
        commands.push_back(std::make_unique<AddPatternTrack>(line, bassRow, bass));
        commands.push_back(
            std::make_unique<PlacePattern>(PlacementId::parse(s16::drumAt0).value(), drums, 0.0));
        commands.push_back(
            std::make_unique<PlacePattern>(PlacementId::parse(s16::drumAt4).value(), drums, 4.0));
        commands.push_back(
            std::make_unique<PlacePattern>(PlacementId::parse(s16::bassAt0).value(), line, 0.0));

        SampleRef sample{};
        sample.blob.digest = std::string(BlobRef::digestLength, 'b');
        sample.blob.byteCount = 88200;
        sample.name = "Crash.wav";
        sample.format = "wav";
        sample.seconds = 1.0;
        commands.push_back(
            std::make_unique<PlaceAudio>(AudioClipId::parse(s16::sampleClip).value(), kick, sample, 8.0));

        for (auto& command : commands)
        {
            if (!session.bus.execute(std::move(command)))
                return 3;
        }

        int beat = 0;
        for (const int pitch : {36, 38, 36, 38})
        {
            Note note{};
            note.id = NoteId::parse("01JBWQ7Z0000000000S16N0TE" + std::to_string(beat)).value();
            note.pitch = pitch;
            note.velocity = 100;
            note.startBeats = static_cast<double>(beat);
            note.lengthBeats = 0.25;
            ++beat;
            if (!session.bus.execute(std::make_unique<AddNote>(drumRow, note)))
                return 4;
        }

        // The state an S16 build serialised: S17 leaves the lines out when
        // they are the ones S16 drew, so this is the same text.
        if (!writeTextFile(stateFile, json::write(session.state.toValue())))
            return 5;

        store.value()->stopRecording();
        if (!store.value()->close())
            return 6;
    }

    // Back to the version S16 wrote, as writeLegacySession does for S8.
    auto database = persistence::Database::open(persistence::ProjectFolder{projectFolder}.databaseFile());
    if (!database)
        return 7;
    if (!database.value().execute("UPDATE meta SET value = '4' WHERE key = 'schema_version'"))
        return 8;
    return 0;
}

int dumpSession(const std::filesystem::path& projectFolder, const std::filesystem::path& stateFile)
{
    auto store = persistence::ProjectStore::open(persistence::ProjectFolder{projectFolder});
    if (!store)
        return 2;

    Session session;
    if (!store.value()->replayInto(session.bus))
        return 3;

    const auto dumped =
        Value::object({{"state", session.state.toValue()},
                       {"undoDepth", Value{static_cast<std::int64_t>(session.bus.undoDepth())}}});
    if (!writeTextFile(stateFile, json::write(dumped)))
        return 4;

    if (!store.value()->close())
        return 5;

    return 0;
}

} // namespace scenarios
} // namespace daw::testing
