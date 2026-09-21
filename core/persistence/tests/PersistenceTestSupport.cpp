#include "PersistenceTestSupport.h"

#include "daw/domain/command/CommandBus.h"
#include "daw/domain/command/CommandRegistry.h"
#include "daw/domain/commands/AddNote.h"
#include "daw/domain/commands/CreateMidiClip.h"
#include "daw/domain/commands/PluginCommands.h"
#include "daw/domain/commands/SetTrackVolume.h"
#include "daw/domain/commands/TrackCommands.h"
#include "daw/domain/project/ProjectState.h"
#include "daw/domain/serialization/Json.h"
#include "daw/persistence/ProjectStore.h"

#include <atomic>
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

} // namespace scenarios
} // namespace daw::testing
