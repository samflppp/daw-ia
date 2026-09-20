#pragma once

#include "daw/domain/BlobRef.h"
#include "daw/domain/Ids.h"
#include "daw/domain/Result.h"
#include "daw/domain/Value.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
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

// --- plugins ---------------------------------------------------------------
//
// Hosting a third-party plugin puts two very different kinds of state in the
// project, and conflating them would cost either the undo or the journal:
//
//   PluginParam   light, continuous. One float per parameter the user has
//                 touched. It arrives through the bus, one command per value,
//                 merged per gesture, so a fader sweep is one undo step.
//
//   StateBlobRef  heavy, punctual. The plugin's own opaque chunk, which can
//                 reach tens of megabytes for a sampler. It never enters the
//                 project by value: the bytes go to a content-addressed store
//                 and the project keeps the digest.
//
// Neither replaces the other. Parameters cannot express a loaded sample set;
// a blob cannot be captured on every knob movement.

// Names a plugin binary, not an instance. Nothing here is engendered by the
// project: the scan finds these values, and they have to survive a move to
// another machine, so the identifier is the format's own stable id and never
// a file path or an index in a list.
struct PluginRef
{
    static constexpr std::string_view vst3Format = "VST3";
    static constexpr std::string_view clapFormat = "CLAP";

    std::string format;     // "VST3" or "CLAP"
    std::string identifier; // VST3 unique id, or CLAP plugin id
    std::string name;       // human label, informative only

    [[nodiscard]] Result<void> validate() const;
    [[nodiscard]] Value toValue() const;
    [[nodiscard]] static Result<PluginRef> fromValue(const Value& value);

    friend bool operator==(const PluginRef& lhs, const PluginRef& rhs);
};

// Reference to an opaque plugin state, held in the content-addressed store.
// An empty digest means "no captured state": the plugin keeps its own default.
//
// It is a BlobRef and nothing more: naming bytes by digest is not a plugin
// idea, and the provenance of a command names the context an agent acted upon
// exactly the same way.
using StateBlobRef = BlobRef;

// One parameter the user has touched at least once.
//
// The list is sparse on purpose. A plugin like Omnisphere exposes thousands of
// parameters; mirroring them all would put megabytes of JSON into every
// toValue(), therefore into every comparison and every hash. A parameter
// absent from the list means "whatever the blob says, or the plugin's own
// default" — the fallback is read at instantiation, in that order.
struct PluginParam
{
    std::string paramId; // the format's own parameter id, never an index
    double value{0.0};   // normalised 0..1, as the host sees it

    [[nodiscard]] Result<void> validate() const;
    [[nodiscard]] Value toValue() const;
    [[nodiscard]] static Result<PluginParam> fromValue(const Value& value);

    friend bool operator==(const PluginParam& lhs, const PluginParam& rhs);
};

struct PluginInstance
{
    PluginId id{};
    PluginRef ref{};
    bool bypassed{false};
    std::vector<PluginParam> params; // sorted by paramId, one entry per id
    StateBlobRef state{};

    [[nodiscard]] const PluginParam* findParam(std::string_view paramId) const noexcept;

    [[nodiscard]] Result<void> validate() const;
    [[nodiscard]] Value toValue() const;
    [[nodiscard]] static Result<PluginInstance> fromValue(const Value& value);

    friend bool operator==(const PluginInstance& lhs, const PluginInstance& rhs);
};

struct Track
{
    TrackId id{};
    std::string name;
    double volumeDb{0.0};
    std::vector<Clip> clips;

    // Order is the chain order: index 0 is first in the signal path.
    std::vector<PluginInstance> plugins;

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

    Result<void> addTrack(Track track); // appends
    Result<void> removeTrack(TrackId id);

    // Undoing a removal has to put the track back where it was, so the index
    // is readable and writable. Beyond the current count it appends, exactly
    // like insertPlugin: a replayed payload never fails on a project that grew
    // differently.
    [[nodiscard]] Result<std::size_t> trackIndex(TrackId id) const;
    Result<void> insertTrack(Track track, std::size_t index);

    [[nodiscard]] Result<double> trackVolume(TrackId id) const;
    Result<void> setTrackVolume(TrackId id, double volumeDb);

    Result<void> addClip(TrackId trackId, Clip clip);
    Result<void> removeClip(ClipId id);

    Result<void> addNote(ClipId clipId, Note note);
    Result<void> removeNote(ClipId clipId, NoteId noteId);

    // --- plugins
    [[nodiscard]] const PluginInstance* findPlugin(PluginId id) const noexcept;

    // Where the instance sits: which track, and at which place in its chain.
    // remove() needs both to be undoable, so the pair is readable on its own.
    struct PluginLocation
    {
        TrackId trackId{};
        std::size_t index{0};
    };

    [[nodiscard]] Result<PluginLocation> pluginLocation(PluginId id) const;

    // index beyond the current chain length appends; it is never an error, so
    // a replayed payload cannot fail on a chain that grew differently.
    Result<void> insertPlugin(TrackId trackId, PluginInstance plugin, std::size_t index);
    Result<void> removePlugin(PluginId id);
    Result<void> setPluginBypassed(PluginId id, bool bypassed);

    // Adds the parameter if this is the first time it is touched, updates it
    // otherwise. clearPluginParameter() undoes that first touch: without it,
    // an undo would leave a parameter pinned to its default instead of giving
    // it back to the blob.
    Result<void> setPluginParameter(PluginId id, std::string paramId, double value);
    Result<void> clearPluginParameter(PluginId id, std::string_view paramId);

    Result<void> setPluginState(PluginId id, StateBlobRef state);

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
    [[nodiscard]] PluginInstance* findPluginMutable(PluginId id) noexcept;

    double tempo_{120.0};
    std::vector<Track> tracks_;
    TransportState transport_;
};

} // namespace daw::domain
