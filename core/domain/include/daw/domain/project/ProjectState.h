#pragma once

#include "daw/domain/Ids.h"
#include "daw/domain/Result.h"
#include "daw/domain/Value.h"

#include <string>
#include <vector>

namespace daw::domain
{

// Minimal project model: exactly what the three commands of S2 need, and
// nothing else. It grows command by command, never ahead of them.
//
// Every mutating method validates first and mutates second, so a refused call
// leaves the state untouched. The Command Bus relies on that to guarantee that
// a failed command creates no history entry.

struct Note
{
    static constexpr int lowestPitch = 0;
    static constexpr int highestPitch = 127;
    static constexpr int lowestVelocity = 1;
    static constexpr int highestVelocity = 127;

    NoteId id{};
    int pitch{60};
    int velocity{100};
    double startBeats{0.0};
    double lengthBeats{1.0};

    [[nodiscard]] Result<void> validate() const;
    [[nodiscard]] Value toValue() const;
    [[nodiscard]] static Result<Note> fromValue(const Value& value);

    friend bool operator==(const Note& lhs, const Note& rhs);
};

struct Clip
{
    ClipId id{};
    double startBeats{0.0};
    double lengthBeats{4.0};
    std::vector<Note> notes;

    [[nodiscard]] Result<void> validate() const;
    [[nodiscard]] Value toValue() const;
    [[nodiscard]] static Result<Clip> fromValue(const Value& value);

    friend bool operator==(const Clip& lhs, const Clip& rhs);
};

struct Track
{
    TrackId id{};
    std::string name;
    double volumeDb{0.0};
    std::vector<Clip> clips;

    [[nodiscard]] Result<void> validate() const;
    [[nodiscard]] Value toValue() const;
    [[nodiscard]] static Result<Track> fromValue(const Value& value);

    friend bool operator==(const Track& lhs, const Track& rhs);
};

// Playback state. It is *not* project state: it is never serialized, never
// compared, never journalled, and the commands that change it are transient.
// A project file does not remember that it was playing.
struct TransportState
{
    bool playing{false};
    double positionBeats{0.0};
};

class ProjectState
{
public:
    // Both ranges come from Tracktion, not from taste.
    //   volume: volumeFaderPositionToDB() maps the fader onto [-100, +6] dB;
    //           above +6 the value is clamped, below -100 it is silence.
    //   tempo:  TempoSetting::minBPM and maxBPM.
    static constexpr double minVolumeDb = -100.0;
    static constexpr double maxVolumeDb = 6.0;
    static constexpr double minTempo = 20.0;
    static constexpr double maxTempo = 300.0;

    [[nodiscard]] double tempo() const noexcept { return tempo_; }
    Result<void> setTempo(double beatsPerMinute);

    [[nodiscard]] const std::vector<Track>& tracks() const noexcept { return tracks_; }
    [[nodiscard]] const Track* findTrack(TrackId id) const noexcept;
    [[nodiscard]] const Clip* findClip(ClipId id) const noexcept;

    Result<void> addTrack(Track track);
    Result<void> removeTrack(TrackId id);

    [[nodiscard]] Result<double> trackVolume(TrackId id) const;
    Result<void> setTrackVolume(TrackId id, double volumeDb);

    Result<void> addClip(TrackId trackId, Clip clip);
    Result<void> removeClip(ClipId id);

    Result<void> addNote(ClipId clipId, Note note);
    Result<void> removeNote(ClipId clipId, NoteId noteId);

    // --- transport (session state, outside toValue/fromValue and operator==)
    [[nodiscard]] const TransportState& transport() const noexcept { return transport_; }
    Result<void> setPlaying(bool playing);
    Result<void> setPositionBeats(double positionBeats);

    // Whole-state serialization. Tests compare two states through it, and the
    // versioning layer of a later week will hash it. Transport is excluded on
    // purpose: an undo must not rewind the playhead.
    [[nodiscard]] Value toValue() const;
    [[nodiscard]] static Result<ProjectState> fromValue(const Value& value);

    friend bool operator==(const ProjectState& lhs, const ProjectState& rhs);

private:
    [[nodiscard]] Track* findTrackMutable(TrackId id) noexcept;
    [[nodiscard]] Clip* findClipMutable(ClipId id) noexcept;

    double tempo_{120.0};
    std::vector<Track> tracks_;
    TransportState transport_;
};

} // namespace daw::domain
