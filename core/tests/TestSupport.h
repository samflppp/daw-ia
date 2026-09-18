#pragma once

#include "daw/domain/command/CommandBus.h"
#include "daw/domain/command/CommandRegistry.h"
#include "daw/domain/commands/AddNote.h"
#include "daw/domain/commands/CreateMidiClip.h"
#include "daw/domain/commands/SetTrackVolume.h"
#include "daw/domain/project/ProjectState.h"

// <ostream> before doctest: doctest prints a failed comparison through
// operator<<, and MSVC only defines the one for std::string_view when the
// stream header is already there.
#include <memory>
#include <ostream>
#include <string>
#include <vector>

#include <doctest/doctest.h>

namespace daw::testing
{

using namespace daw::domain;

// A project with one track, a registry holding the three built-in commands and
// a bus wired to both. Every bus test starts here.
struct Harness
{
    explicit Harness(BusLimits limits = {})
        : registry{CommandRegistry::withBuiltinCommands()}
        , bus{state, registry, limits}
    {
        Track track{};
        track.id = trackId;
        track.name = "Piste 1";
        track.volumeDb = 0.0;
        REQUIRE(state.addTrack(track).ok());
    }

    [[nodiscard]] std::unique_ptr<Command> createClip(ClipId id, double start = 0.0, double length = 4.0)
    {
        return std::make_unique<CreateMidiClip>(trackId, id, start, length);
    }

    [[nodiscard]] static std::unique_ptr<Command> addNote(ClipId clipId, NoteId noteId, int pitch = 60)
    {
        Note note{};
        note.id = noteId;
        note.pitch = pitch;
        note.velocity = 100;
        note.startBeats = 0.0;
        note.lengthBeats = 0.25;
        return std::make_unique<AddNote>(clipId, note);
    }

    [[nodiscard]] std::unique_ptr<Command> setVolume(double volumeDb) const
    {
        return std::make_unique<SetTrackVolume>(trackId, volumeDb);
    }

    ProjectState state;
    CommandRegistry registry;
    CommandBus bus;
    TrackId trackId{TrackId::generate()};
};

// Records what the bus reported, in order, so a test can assert on
// notifications instead of on internals.
class RecordingObserver final : public BusObserver
{
public:
    void onExecuted(const Receipt& receipt) override { executed.push_back(receipt); }
    void onCoalesced(const Receipt& receipt) override { coalesced.push_back(receipt); }
    void onUndone(const Receipt& receipt) override { undone.push_back(receipt); }
    void onRedone(const Receipt& receipt) override { redone.push_back(receipt); }
    void onHistoryTruncated(std::size_t dropped) override { truncated += dropped; }

    std::vector<Receipt> executed;
    std::vector<Receipt> coalesced;
    std::vector<Receipt> undone;
    std::vector<Receipt> redone;
    std::size_t truncated{0};
};

} // namespace daw::testing
