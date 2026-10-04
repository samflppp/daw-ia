#pragma once

#include "TestSettings.h"
#include "daw/domain/command/CommandBus.h"
#include "daw/domain/command/CommandRegistry.h"
#include "daw/domain/commands/AddNote.h"
#include "daw/domain/commands/CreateMidiClip.h"
#include "daw/domain/commands/SetTrackVolume.h"
#include "daw/domain/commands/TransportCommands.h"
#include "daw/domain/project/ProjectState.h"
#include "daw/engine/EngineHost.h"
#include "daw/engine/ProjectProjector.h"

#include <memory>
#include <ostream>
#include <string>

#include <doctest/doctest.h>

namespace daw::testing
{

using namespace daw::domain;

// A real Engine, a real Edit, a real bus, and the projector wired between them.
// Nothing is mocked: the point of this suite is to prove the projection against
// Tracktion itself, not against a stand-in.
struct EngineHarness
{
    EngineHarness()
        : registry{CommandRegistry::withBuiltinCommands()}
        , bus{state, registry}
        , host{"daw_engine_tests", daw::testing::engineSettingsFolder()}
        , projector{host.edit(), state}
    {
        bus.addObserver(projector);

        Track track{};
        track.id = trackId;
        track.name = "Piste 1";
        track.volumeDb = 0.0;
        REQUIRE(state.addTrack(track).ok());

        // The track was added straight to the state, not through a command, so
        // the projector has not seen it yet.
        projector.reconcile();
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
        note.lengthBeats = 1.0;
        return std::make_unique<AddNote>(clipId, note);
    }

    [[nodiscard]] std::unique_ptr<Command> setVolume(double volumeDb) const
    {
        return std::make_unique<SetTrackVolume>(trackId, volumeDb);
    }

    [[nodiscard]] tracktion::AudioTrack* firstAudioTrack()
    {
        const auto tracks = tracktion::getAudioTracks(host.edit());
        return tracks.isEmpty() ? nullptr : tracks.getFirst();
    }

    ProjectState state;
    CommandRegistry registry;
    CommandBus bus;
    engine::EngineHost host;
    engine::ProjectProjector projector;
    TrackId trackId{TrackId::generate()};
};

} // namespace daw::testing
