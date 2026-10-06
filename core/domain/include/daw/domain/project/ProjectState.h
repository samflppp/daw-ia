#pragma once

#include "daw/domain/BlobRef.h"
#include "daw/domain/Ids.h"
#include "daw/domain/Result.h"
#include "daw/domain/Value.h"
#include "daw/domain/direction/Direction.h"
#include "daw/domain/project/Automation.h"

#include <cstddef>
#include <cstdint>
#include <optional>
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

// One tempo change on the timeline.
//
// Anchored in beats, never in seconds, and that is the whole reason the
// sequence is cheap today: clips and notes are in beats too, so changing a
// tempo moves no musical content. A sequence anchored in time would have to
// remap every clip on every edit.
//
// No curve. Tracktion's TempoSetting carries one (log, linear, exponential),
// and it is an automation shape: automation is out of scope, and a field
// nothing can change is a field that lies. Adding it later is additive.
struct TempoPoint
{
    TempoPointId id{};
    double startBeats{0.0};
    double beatsPerMinute{120.0};

    [[nodiscard]] Result<void> validate() const;
    [[nodiscard]] Value toValue() const;
    [[nodiscard]] static Result<TempoPoint> fromValue(const Value& value);

    friend bool operator==(const TempoPoint& lhs, const TempoPoint& rhs);
};

// How the beats of the project group into bars: 3/4, 4/4, 6/8.
//
// One for the whole project. A song that changes meter halfway is rare in the
// music this DAW is for, and a sequence of signatures would ask every grid to
// find its bar lines by search. Adding one later is additive: this becomes the
// signature at the origin, as the tempo did.
//
// A beat of the domain is a quarter note, whatever the signature: notes and
// clips do not move when the meter changes, only the bar lines drawn over
// them do. A bar of 6/8 is therefore three beats long.
struct TimeSignature
{
    static constexpr int lowestNumerator = 1;
    static constexpr int highestNumerator = 16;

    int numerator{4};
    int denominator{4};

    // The denominators a score writes: a whole note down to a sixteenth.
    [[nodiscard]] Result<void> validate() const;
    [[nodiscard]] double beatsPerBar() const noexcept { return numerator * 4.0 / denominator; }

    [[nodiscard]] Value toValue() const;
    [[nodiscard]] static Result<TimeSignature> fromValue(const Value& value);

    friend bool operator==(const TimeSignature& lhs, const TimeSignature& rhs) = default;
};

// What one track plays inside one pattern. Content, and only content.
//
// It used to carry its own start and length and to belong to a track. Both
// moved out, and the move is the whole point of the pattern model: the start
// belongs to the Placement, because the same content can be laid down eight
// times at eight different beats, and the length belongs to the Pattern,
// because every track of a pattern is as long as the pattern.
//
// What is left is a track and its notes, which is what an edit changes. That
// is why a pattern laid eight times is modified by one command: the eight
// placements hold no note to modify.
struct Clip
{
    ClipId id{};
    TrackId trackId{};
    std::vector<Note> notes;

    [[nodiscard]] Result<void> validate() const;
    [[nodiscard]] Value toValue() const;
    [[nodiscard]] static Result<Clip> fromValue(const Value& value);

    friend bool operator==(const Clip& lhs, const Clip& rhs);
};

// A piece of content, several tracks wide. What a beatmaker calls a pattern and
// what the channel rack shows: one row per track, all of them the same length.
//
// A pattern has no position. It is played where placements say, and nowhere
// else. A pattern nobody placed is written but silent, which is honest: it is
// material, not music yet.
struct Pattern
{
    PatternId id{};

    // May be empty. An unnamed pattern is displayed by its rank, which is a
    // decision of the screen; the domain does not invent names, because a name
    // invented at apply() time would differ between a run and its replay.
    std::string name;

    double lengthBeats{4.0};

    // At most one clip per track: two clips of the same track in one pattern
    // would be two answers to "what does this track play here".
    std::vector<Clip> clips;

    [[nodiscard]] const Clip* findClipForTrack(TrackId trackId) const noexcept;

    [[nodiscard]] Result<void> validate() const;
    [[nodiscard]] Value toValue() const;
    [[nodiscard]] static Result<Pattern> fromValue(const Value& value);

    friend bool operator==(const Pattern& lhs, const Pattern& rhs);
};

// One line of the playlist, the way FL Studio has them: free, named by the
// user, holding any block. A line has no sound of its own — no volume, no
// mute, no instrument. Moving a block from one line to another changes where
// it is filed and nothing it plays; what sounds is the tracks of its pattern.
//
// It lives in the project, not in the screen, because the user writes it: a
// block filed on the "Basse" line is an arrangement the user made, and losing
// it on reopening or on Ctrl+Z would be a bug. What the screen owns stays
// there: height, colour, scroll.
struct Lane
{
    LaneId id{};

    // May be empty: an unnamed line is displayed by its rank, like a pattern.
    std::string name;

    [[nodiscard]] Result<void> validate() const;
    [[nodiscard]] Value toValue() const;
    [[nodiscard]] static Result<Lane> fromValue(const Value& value);

    friend bool operator==(const Lane& lhs, const Lane& rhs);
};

// One laying of a pattern on the timeline: which pattern, at which beat, and
// on which line of the playlist.
//
// It carries no track, because the pattern already says which tracks it sounds
// on, and no length, because the pattern already says how long it is. A field
// that repeated either would be a second truth waiting to disagree. The line
// repeats nothing: it is where the block is filed, and it does not sound.
struct Placement
{
    PlacementId id{};
    PatternId patternId{};
    double startBeats{0.0};

    // Written before S17, a placement named no line. It then sits on the line
    // of its pattern, whose identifier is the pattern's own (see
    // ProjectState::laneOfPattern): the playlist those projects showed, one
    // line per pattern, comes back without a byte being rewritten.
    LaneId laneId{};

    [[nodiscard]] Result<void> validate() const;
    [[nodiscard]] Value toValue() const;
    [[nodiscard]] static Result<Placement> fromValue(const Value& value);

    friend bool operator==(const Placement& lhs, const Placement& rhs);
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

    // An effect the DAW ships (S20): see InternalEffects.h. The identifier is
    // one of its constants, "daw.eq" or "daw.compressor".
    static constexpr std::string_view internalFormat = "internal";

    std::string format;     // "VST3", "CLAP" or "internal"
    std::string identifier; // VST3 unique id, CLAP plugin id, or internal effect
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
    double value{0.0};   // normalised 0..1 for a hosted plugin; the unit of the
                         // parameter (Hz, dB, ms, ratio) for an internal effect

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

    // Whether this instance accepts that value for that parameter: 0..1 for a
    // hosted plugin, the effect's own bounds for an internal one.
    [[nodiscard]] Result<void> validParameter(std::string_view paramId, double value) const;

    [[nodiscard]] Result<void> validate() const;
    [[nodiscard]] Value toValue() const;
    [[nodiscard]] static Result<PluginInstance> fromValue(const Value& value);

    friend bool operator==(const PluginInstance& lhs, const PluginInstance& rhs);
};

// A sample the project holds: an audio file, by the digest of its bytes.
//
// The bytes are copied into the project's content store when the sample comes
// in, so the project keeps sounding when the drumkit it came from is moved,
// renamed or deleted. What the domain keeps is the reference, the name it had,
// the format it was in — needed to read it back — and its length, measured
// once at import: the playlist draws an audio clip that long without opening
// the file.
struct SampleRef
{
    BlobRef blob;
    std::string name;   // "Kick 01.wav", for the eye
    std::string format; // "wav", "aif", "flac", "mp3", "ogg": lowercase, no dot
    double seconds{0.0};

    [[nodiscard]] Result<void> validate() const;
    [[nodiscard]] Value toValue() const;
    [[nodiscard]] static Result<SampleRef> fromValue(const Value& value);

    friend bool operator==(const SampleRef& lhs, const SampleRef& rhs);
};

// A send: part of a strip's signal, taken after its fader, going to a bus.
//
// Keyed by the bus it goes to, so it needs no identifier of its own: a strip
// sends to a bus once or not at all, and "the send of the kick to the reverb"
// names it completely. Post-fader only: a pre-fader send is an additional
// field, absent means post, the day a monitor mix needs one.
struct Send
{
    TrackId bus{};
    double levelDb{0.0};

    [[nodiscard]] Result<void> validate() const;
    [[nodiscard]] Value toValue() const;
    [[nodiscard]] static Result<Send> fromValue(const Value& value);

    friend bool operator==(const Send& lhs, const Send& rhs);
};

// What a track is for in a mix (S20): the vocabulary a mixing engineer sorts a
// session by, and what the mixing rules and the copilot reason on. A kick is
// not mixed like a pad.
//
// Kept in the project only when the person chose it: a role guessed from the
// names, the generator or the sound is a reading of the project, computed
// again each time and never stored; a role the person corrected is a decision,
// and a decision is journaled, undone, and survives reopening.
enum class MixRole : std::uint8_t
{
    kick,
    snare,
    hats,
    percussion,
    bass,
    chords,
    melody,
    vocal,
    fx
};

// "kick", "snare", "hats", "percussion", "bass", "chords", "melody", "vocal",
// "fx": the words of the JSON and of the copilot.
[[nodiscard]] std::string_view mixRoleName(MixRole role) noexcept;
[[nodiscard]] std::optional<MixRole> mixRoleFromName(std::string_view name) noexcept;

// A strip of the mixer. What the channel rack calls a channel, what the mixer
// calls a bus, and the master are all this: a fader, a pan, a mute, a chain of
// inserts. What tells them apart is where the project keeps them — tracks(),
// buses(), master() — never a field, so a channel cannot become a bus by
// accident and every verb of the mix works on the three alike.
struct Track
{
    TrackId id{};
    std::string name;
    double volumeDb{0.0};

    // Where the track sits in the stereo field: -1 hard left, 0 centre,
    // +1 hard right. A number, not a law: how that number becomes two gains is
    // a decision of the projection, and it is written down in
    // engine/ProjectProjector.h. A track without pan is not mixable, and the
    // mix is the first ground a copilot works on.
    double pan{0.0};

    // Silences the whole track: its clips and the instrument that plays them.
    // Not a volume of -100 dB, and not the bypass of a plugin — those are the
    // two things it is constantly mistaken for. A muted track keeps its fader
    // where the user left it, and a bypassed chain still lets the instrument
    // through. Arranging needs the first, mixing needs the second.
    bool muted{false};

    // The pitch this track plays when a step of the channel rack is lit. A
    // kick track always plays the same note, and a rack lights cells rather
    // than choosing pitches.
    //
    // It lives in the domain and not in the rack panel on purpose: a hauteur
    // known only by one screen is a hauteur a copilot cannot use, and
    // "mets un charleston en doubles-croches sur la piste 3" needs it.
    // It changes nothing about what already sounds: it is read when a cell is
    // lit, never applied to notes that exist.
    int channelPitch{60};

    // Order is the chain order: index 0 is first in the signal path.
    std::vector<PluginInstance> plugins;

    // A sampler channel, the way FL makes one when a sample is dropped on the
    // channel rack: the instrument of this track is that sample, and a lit
    // cell triggers it. The channel pitch is the note that plays it at its own
    // pitch. Absent, the track plays whatever instrument its chain holds.
    std::optional<SampleRef> sample;

    // Where the strip goes: a bus, or, nil, the master. The master itself goes
    // nowhere, to the outside world.
    TrackId output{};

    // The buses this strip also feeds, after its fader. One per bus.
    std::vector<Send> sends;

    // In solo. Not a state of a screen: a command sets it, a copilot reads it,
    // an undo gives it back. What it silences is decided by
    // ProjectState::isAudible, once, for the engine and for anyone asking.
    bool soloed{false};

    // The role the person gave it in the mix. Absent: guessed, every time.
    std::optional<MixRole> role;

    [[nodiscard]] const Send* findSend(TrackId bus) const noexcept;

    [[nodiscard]] Result<void> validate() const;
    [[nodiscard]] Value toValue() const;
    [[nodiscard]] static Result<Track> fromValue(const Value& value);

    friend bool operator==(const Track& lhs, const Track& rhs);
};

// One audio clip on the timeline: a sample, the track it sounds on, and the
// beat it starts at.
//
// It is not in a pattern. A pattern is notes a rack edits; an audio clip is a
// recording laid where it plays, the way FL lays one in its playlist. It has no
// length of its own either: it lasts as long as its sample, in seconds, so a
// tempo change moves where it starts and never stretches it.
struct AudioClip
{
    AudioClipId id{};
    TrackId trackId{};
    SampleRef sample;
    double startBeats{0.0};

    // The playlist line it is filed on. Written before S17, an audio clip named
    // none: it sits on the line of its track (ProjectState::laneOfTrack).
    LaneId laneId{};

    [[nodiscard]] Result<void> validate() const;
    [[nodiscard]] Value toValue() const;
    [[nodiscard]] static Result<AudioClip> fromValue(const Value& value);

    friend bool operator==(const AudioClip& lhs, const AudioClip& rhs);
};

// The clips a track carried before patterns existed.
//
// Nothing writes this shape any more. It is still read, in exactly two places:
// the undo record of a track.remove written before the S9 model, and a whole
// state serialized before it. Each such clip means a pattern of one track, a
// placement at its start, and a row holding its notes — which is what
// ProjectState::addSingleTrackPattern builds.
struct LegacyClip
{
    ClipId id{};
    double startBeats{0.0};
    double lengthBeats{4.0};
    std::vector<Note> notes;
};

[[nodiscard]] Result<std::vector<LegacyClip>> legacyClipsOf(const Value& trackValue);

// What the transport plays: the pattern being worked on, alone and looping,
// or the arrangement.
//
//   pattern  the auditioned pattern is laid once at beat 0, every placement
//            is silent, and playback loops over the pattern's length. Nothing
//            else in the arrangement can be heard, which is what makes a
//            pattern editable while a song exists around it.
//   song     the placements are laid where they are, and the loop is the one
//            transport.set_loop asked for, if any.
//
// It is a property of the session and not of the project, like the playhead.
enum class PlayMode
{
    song,
    pattern
};

// Playback state. It is *not* project state: it is never serialized, never
// compared, never journalled, and the commands that change it are transient.
// A project file does not remember that it was playing.
struct TransportState
{
    bool playing{false};
    double positionBeats{0.0};

    // The loop, in beats like everything else on the timeline. Disabled means
    // playback runs past the material and stops where the engine stops it.
    //
    // It belongs here and not in the project for the same reason the playhead
    // does: it is not journalled, not undone, and two windows on the same
    // project would each have their own. A copilot still reads it and still
    // sets it, because it goes through a command like everything else.
    bool looping{false};
    double loopStartBeats{0.0};
    double loopEndBeats{0.0};

    // Song by default, so that a project nobody set a mode on plays what is
    // laid on its timeline. The beatmaker asks for pattern mode when it opens.
    PlayMode mode{PlayMode::song};

    // The pattern pattern mode plays. Nil in song mode, and allowed to be nil
    // in pattern mode too: a project with no pattern yet plays silence rather
    // than refusing to enter the mode. A pattern removed while auditioned
    // leaves a name that finds nothing, which the projection reads as silence.
    PatternId auditionedPattern{};
};

class ProjectState
{
public:
    // Both ranges come from Tracktion, not from taste.
    //   volume: volumeFaderPositionToDB() maps the fader onto [-100, +6] dB;
    //           above +6 the value is clamped, below -100 it is silence.
    //   tempo:  TempoSetting::minBPM and maxBPM.
    static constexpr double minPan = -1.0;
    static constexpr double maxPan = 1.0;

    static constexpr double minVolumeDb = -100.0;
    static constexpr double maxVolumeDb = 6.0;
    static constexpr double minTempo = 20.0;
    static constexpr double maxTempo = 300.0;

    // The tempo point at the origin exists in every project by construction:
    // nobody creates it, so nobody can be asked for its identifier. It is
    // therefore a constant and not a generated ULID — two projects built by
    // the same commands have to be equal, and a generated identifier would
    // already make two empty projects differ.
    [[nodiscard]] static TempoPointId originTempoPointId() noexcept;

    // Sorted by startBeats, never empty, and the first point is always the
    // origin one at beat 0. Those three properties are what let tempoAt()
    // answer without a special case.
    [[nodiscard]] const std::vector<TempoPoint>& tempoPoints() const noexcept { return tempo_; }
    [[nodiscard]] const TempoPoint* findTempoPoint(TempoPointId id) const noexcept;
    [[nodiscard]] Result<TempoPoint> tempoPoint(TempoPointId id) const;

    // The tempo in force at that beat: the last point at or before it. A
    // tempo is held until the next point, so there is no interpolation to do.
    [[nodiscard]] double tempoAt(double beats) const noexcept;

    // The origin point refuses to be removed or moved: the sequence has to
    // stay non-empty and has to start at the timeline origin.
    Result<void> insertTempoPoint(TempoPoint point);
    Result<void> removeTempoPoint(TempoPointId id);
    Result<void> setTempoPointBpm(TempoPointId id, double beatsPerMinute);
    Result<void> moveTempoPoint(TempoPointId id, double startBeats);

    [[nodiscard]] const TimeSignature& timeSignature() const noexcept { return timeSignature_; }
    [[nodiscard]] double beatsPerBar() const noexcept { return timeSignature_.beatsPerBar(); }
    Result<void> setTimeSignature(TimeSignature signature);

    // The direction by references (S22). Empty in a project that never had
    // one, and then not serialised: such a project is the one of S21, byte
    // for byte.
    [[nodiscard]] const direction::Direction& direction() const noexcept { return direction_; }
    void setDirection(direction::Direction direction) { direction_ = std::move(direction); }

    static constexpr int lowestChannelPitch = Note::lowestPitch;
    static constexpr int highestChannelPitch = Note::highestPitch;

    // The channels: what the channel rack and the playlist show, what patterns
    // and audio clips play on. Never a bus, never the master.
    [[nodiscard]] const std::vector<Track>& tracks() const noexcept { return tracks_; }
    [[nodiscard]] const Track* findTrack(TrackId id) const noexcept;

    // --- the mixer
    //
    // Buses and the master are strips like the channels, kept apart from
    // them: no pattern row, no sample, no audio clip can land on one, because
    // every verb of that kind asks findTrack(), which only knows channels.
    // The verbs of the mix — volume, pan, mute, name, inserts, output, sends,
    // solo — ask findStrip(), which knows all three.
    //
    // Tracktion gives a project 32 aux buses; a bus is one of them.
    static constexpr std::size_t maxBuses = 32;

    // Built into every project, like the tempo point at the origin: nobody
    // creates it, so it is a constant and not a generated ULID.
    [[nodiscard]] static TrackId masterTrackId() noexcept;

    [[nodiscard]] const std::vector<Track>& buses() const noexcept { return buses_; }
    [[nodiscard]] const Track& master() const noexcept { return master_; }
    [[nodiscard]] const Track* findStrip(TrackId id) const noexcept;
    [[nodiscard]] bool isBus(TrackId id) const noexcept;

    [[nodiscard]] Result<std::size_t> busIndex(TrackId id) const;
    Result<void> insertBus(Track bus, std::size_t index);

    // Takes the bus out, and what pointed at it: the strips whose output it
    // was go to the master, and the sends to it are dropped. bus.remove's undo
    // record carries all three back.
    Result<void> removeBus(TrackId id);

    // Nil sends the strip to the master. A target that is not a bus, the strip
    // itself, or a route that would come back to the strip is refused.
    Result<void> setTrackOutput(TrackId id, TrackId output);

    // Adds the send, or changes its level.
    Result<void> setTrackSend(TrackId id, TrackId bus, double levelDb);
    Result<void> insertTrackSend(TrackId id, Send send, std::size_t index);
    Result<void> removeTrackSend(TrackId id, TrackId bus);
    [[nodiscard]] Result<std::size_t> sendIndex(TrackId id, TrackId bus) const;

    Result<void> setTrackSoloed(TrackId id, bool soloed);
    Result<void> setTrackRole(TrackId id, std::optional<MixRole> role);

    // Whether signal leaving `from` can reach `to`, through outputs and sends.
    [[nodiscard]] bool reaches(TrackId from, TrackId to) const;

    // Whether a strip is heard, mute and solo taken together. The rule, once:
    //   no solo anywhere    a strip is heard unless muted;
    //   a solo somewhere    a strip is heard unless muted, and only if it is
    //                       in solo, or feeds a strip in solo, or is fed by
    //                       one — soloing a bus keeps what goes into it, and
    //                       soloing a channel keeps the buses it goes through;
    //   the master          heard unless muted.
    // A limit, said: soloing a bus that only receives sends keeps its sources
    // heard on their own path too, dry. Silencing one path of a strip and not
    // the other would need a gain per route.
    [[nodiscard]] bool isAudible(TrackId id) const;
    [[nodiscard]] const Clip* findClip(ClipId id) const noexcept;

    // Which pattern holds that clip. The clip alone does not say it, and every
    // caller that has a ClipId and needs the length it is drawn against does.
    [[nodiscard]] Result<PatternId> patternOfClip(ClipId id) const;

    Result<void> addTrack(Track track);   // appends
    Result<void> removeTrack(TrackId id); // a channel; a bus goes through removeBus

    // Undoing a removal has to put the track back where it was, so the index
    // is readable and writable. Beyond the current count it appends, exactly
    // like insertPlugin: a replayed payload never fails on a project that grew
    // differently.
    [[nodiscard]] Result<std::size_t> trackIndex(TrackId id) const;
    Result<void> insertTrack(Track track, std::size_t index);

    Result<void> setTrackName(TrackId id, std::string name);

    // Moves a track to another place in the list. An index beyond the last
    // track puts it at the end rather than failing, exactly like insertTrack:
    // a replayed payload must not fail on a project that grew differently.
    Result<void> moveTrack(TrackId id, std::size_t index);

    [[nodiscard]] Result<double> trackVolume(TrackId id) const;
    Result<void> setTrackVolume(TrackId id, double volumeDb);

    [[nodiscard]] Result<double> trackPan(TrackId id) const;
    Result<void> setTrackPan(TrackId id, double pan);

    [[nodiscard]] Result<bool> trackMuted(TrackId id) const;
    Result<void> setTrackMuted(TrackId id, bool muted);

    [[nodiscard]] Result<int> trackChannelPitch(TrackId id) const;
    Result<void> setTrackChannelPitch(TrackId id, int pitch);

    // Makes the track a sampler channel on that sample, or, with nothing, gives
    // it back to its chain.
    Result<void> setTrackSample(TrackId id, std::optional<SampleRef> sample);

    // --- audio clips
    //
    // In the arrangement next to the placements, and ordered the same way: by
    // the order they were laid, which an undo restores.
    [[nodiscard]] const std::vector<AudioClip>& audioClips() const noexcept { return audio_; }
    [[nodiscard]] const AudioClip* findAudioClip(AudioClipId id) const noexcept;
    [[nodiscard]] Result<std::size_t> audioClipIndex(AudioClipId id) const;

    Result<void> addAudioClip(AudioClip clip);
    Result<void> insertAudioClip(AudioClip clip, std::size_t index);
    Result<void> removeAudioClip(AudioClipId id);

    // A nil line keeps the clip on its line.
    Result<void> moveAudioClip(AudioClipId id, double startBeats, LaneId laneId = {});

    // --- lanes
    //
    // The lines of the playlist, top to bottom. Every placement and every audio
    // clip is filed on one of them; a line may be empty.
    [[nodiscard]] const std::vector<Lane>& lanes() const noexcept { return lanes_; }
    [[nodiscard]] const Lane* findLane(LaneId id) const noexcept;
    [[nodiscard]] Result<std::size_t> laneIndex(LaneId id) const;

    // Beyond the current count it appends, like insertTrack.
    Result<void> insertLane(Lane lane, std::size_t index);

    // Takes the blocks filed on it with it, like removePattern takes its
    // placements: a block on no line would be a block nothing can draw.
    Result<void> removeLane(LaneId id);
    Result<void> setLaneName(LaneId id, std::string name);
    Result<void> moveLane(LaneId id, std::size_t index);

    [[nodiscard]] bool laneIsEmpty(LaneId id) const noexcept;

    // The line a pattern or a track had before lines were free: the
    // pattern's or the track's own identifier, retyped. Derived and not
    // engendered, like patternIdForClip, so a journal written before S17
    // replays into the same lines every time.
    static LaneId laneOfPattern(PatternId id) noexcept;
    static LaneId laneOfTrack(TrackId id) noexcept;

    // Creates that line if it is missing, where the S16 playlist showed it:
    // the lines of patterns first, in pattern order, then the audio lines, in
    // track order. Returns whether it had to be created, so a command can
    // take it away again on undo.
    Result<bool> ensureLaneOfPattern(PatternId id);
    Result<bool> ensureLaneOfTrack(TrackId id);

    // --- patterns and placements
    //
    // Content and position, kept apart. Everything a pattern holds is edited
    // once; everything a placement holds is a beat. That split is what the
    // playlist of the next week is built on: laying a pattern down eight times
    // costs eight placements and copies no note.

    [[nodiscard]] const std::vector<Pattern>& patterns() const noexcept { return patterns_; }
    [[nodiscard]] const Pattern* findPattern(PatternId id) const noexcept;
    [[nodiscard]] Result<std::size_t> patternIndex(PatternId id) const;

    Result<void> addPattern(Pattern pattern); // appends

    // Beyond the current count it appends, like insertTrack: a replayed undo
    // must not fail on a project that grew differently.
    Result<void> insertPattern(Pattern pattern, std::size_t index);
    Result<void> removePattern(PatternId id); // takes its placements with it
    Result<void> setPatternName(PatternId id, std::string name);
    Result<void> setPatternLength(PatternId id, double lengthBeats);

    [[nodiscard]] const std::vector<Placement>& arrangement() const noexcept { return arrangement_; }
    [[nodiscard]] const Placement* findPlacement(PlacementId id) const noexcept;

    // The placements of one pattern, in timeline order. The beatmaker loop
    // reads the first one; the playlist of the next week reads them all.
    [[nodiscard]] std::vector<const Placement*> placementsOf(PatternId id) const;

    Result<void> addPlacement(Placement placement);

    // Undoing a removal has to put the placement back where it was in the
    // arrangement. Order carries no musical meaning — a placement sounds at its
    // beat whatever its rank — but it carries an equality, exactly as for
    // notes. Beyond the current count it appends.
    [[nodiscard]] Result<std::size_t> placementIndex(PlacementId id) const;
    Result<void> insertPlacement(Placement placement, std::size_t index);

    Result<void> removePlacement(PlacementId id);

    // A nil line keeps the placement on its line.
    Result<void> movePlacement(PlacementId id, double startBeats, LaneId laneId = {});

    // Adds an empty track row to a pattern. The clip identifier comes from the
    // caller, like every other identifier in this project.
    Result<void> addClip(PatternId patternId, Clip clip);
    Result<void> removeClip(ClipId id);

    // The pattern, the placement and the single row a clip.create_midi means.
    //
    // It exists because that command was written before patterns did, and its
    // payload must keep replaying: eight weeks of journals name a track, a
    // clip, a start and a length. The identifiers of the pattern and of the
    // placement are derived from the clip's own bytes, so nothing is engendered
    // and a replay rebuilds exactly the same project.
    static PatternId patternIdForClip(ClipId clipId) noexcept;
    static PlacementId placementIdForClip(ClipId clipId) noexcept;

    Result<void> addSingleTrackPattern(
        TrackId trackId, ClipId clipId, double startBeats, double lengthBeats, std::vector<Note> notes = {});

    Result<void> addNote(ClipId clipId, Note note);
    Result<void> removeNote(ClipId clipId, NoteId noteId);

    // Undoing a removal has to put the note back where it was. Order carries
    // no musical meaning — a clip sounds the same whatever order its notes are
    // stored in — but it carries an equality: two projects that differ only by
    // the order of a vector are two different serialized forms, and a test that
    // undoes a session back to its start would see them as unequal.
    [[nodiscard]] Result<std::size_t> noteIndex(ClipId clipId, NoteId noteId) const;
    Result<void> insertNote(ClipId clipId, Note note, std::size_t index);

    // Changes how hard a note is struck, and nothing else. It is read in the
    // piano roll as the shade of the note; until now it could only be read.
    Result<void> setNoteVelocity(ClipId clipId, NoteId noteId, int velocity);

    // Moves a note in time and pitch. Length is untouched on purpose: dragging
    // a note and stretching it are two gestures, and merging them into one
    // command would make an undo give back a note the user never had.
    Result<void> moveNote(ClipId clipId, NoteId noteId, int pitch, double startBeats);

    // Changes how long a note sounds, and nothing else. Its start does not
    // move: stretching a note from its right edge is the gesture, and a
    // command that also moved it would undo into a note the user never had.
    Result<void> resizeNote(ClipId clipId, NoteId noteId, double lengthBeats);

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

    // Moves a plugin to `index` in its own chain (S24): the same instance,
    // its identifier, state, parameters and the automation lines that drive
    // them untouched. Past the end means last, like insertPlugin. Returns the
    // index it was at.
    Result<std::size_t> movePlugin(PluginId id, std::size_t index);
    Result<void> setPluginBypassed(PluginId id, bool bypassed);

    // Adds the parameter if this is the first time it is touched, updates it
    // otherwise. clearPluginParameter() undoes that first touch: without it,
    // an undo would leave a parameter pinned to its default instead of giving
    // it back to the blob.
    Result<void> setPluginParameter(PluginId id, std::string paramId, double value);
    Result<void> clearPluginParameter(PluginId id, std::string_view paramId);

    Result<void> setPluginState(PluginId id, StateBlobRef state);

    // --- automation
    //
    // In the arrangement, absolute on the timeline (the S13 model, written out
    // in Automation.h). The lines keep the order they were created in, which
    // an undo restores; the points of a line are sorted by beat.
    //
    // A line whose target goes away goes with it: removeTrack, removeBus and
    // removePlugin drop the lines of what they remove, and the commands that
    // call them carry those lines in their undo records.
    [[nodiscard]] const std::vector<AutomationLine>& automation() const noexcept { return automation_; }
    [[nodiscard]] const AutomationLine* findAutomationLine(AutomationLineId id) const noexcept;
    [[nodiscard]] const AutomationLine* findAutomationLineFor(const AutomationTarget& target) const noexcept;
    [[nodiscard]] Result<std::size_t> automationLineIndex(AutomationLineId id) const;

    // Whether the target names something the project holds: the strip for a
    // volume or a pan, the plugin for a parameter.
    [[nodiscard]] Result<void> checkAutomationTarget(const AutomationTarget& target) const;

    // The lines that removing this strip, or this plugin, would drop: a
    // strip's own volume and pan, and the parameters of every plugin it holds.
    [[nodiscard]] std::vector<AutomationLineId> automationOfStrip(TrackId strip) const;
    [[nodiscard]] std::vector<AutomationLineId> automationOfPlugin(PluginId plugin) const;

    // Beyond the current count it appends, like insertTrack.
    Result<void> insertAutomationLine(AutomationLine line, std::size_t index);
    Result<void> removeAutomationLine(AutomationLineId id);

    Result<void> insertAutomationPoint(AutomationLineId line, AutomationPoint point);
    Result<void> removeAutomationPoint(AutomationLineId line, AutomationPointId point);

    // Beat and value together: dragging a point moves it on both axes.
    Result<void>
    moveAutomationPoint(AutomationLineId line, AutomationPointId point, double beats, double value);
    Result<void> setAutomationCurve(AutomationLineId line, AutomationPointId point, double curve);

    // --- transport (session state, outside toValue/fromValue and operator==)
    [[nodiscard]] const TransportState& transport() const noexcept { return transport_; }
    Result<void> setPlaying(bool playing);
    Result<void> setPositionBeats(double positionBeats);

    // An empty or backwards range is refused rather than silently disabled: a
    // loop of length zero would be a transport that never advances.
    Result<void> setLoop(bool looping, double startBeats, double endBeats);

    // Switches between pattern and song. Pattern mode names the pattern it
    // plays, or nothing; song mode names none. A pattern that does not exist is
    // refused: a mode that auditioned a typo would play silence and say
    // nothing.
    Result<void> setPlayMode(PlayMode mode, PatternId auditioned);

    // Whole-state serialization. Tests compare two states through it, and the
    // versioning layer of a later week will hash it. Transport is excluded on
    // purpose: an undo must not rewind the playhead.
    [[nodiscard]] Value toValue() const;
    [[nodiscard]] static Result<ProjectState> fromValue(const Value& value);

    friend bool operator==(const ProjectState& lhs, const ProjectState& rhs);

private:
    [[nodiscard]] Track* findTrackMutable(TrackId id) noexcept;
    [[nodiscard]] Track* findStripMutable(TrackId id) noexcept;
    [[nodiscard]] std::vector<const Track*> strips() const;
    [[nodiscard]] std::vector<Track*> stripsMutable();
    [[nodiscard]] static Track defaultMaster();
    [[nodiscard]] Clip* findClipMutable(ClipId id) noexcept;
    [[nodiscard]] Pattern* findPatternMutable(PatternId id) noexcept;
    [[nodiscard]] PluginInstance* findPluginMutable(PluginId id) noexcept;
    [[nodiscard]] AutomationLine* findAutomationLineMutable(AutomationLineId id) noexcept;
    void dropAutomation(const std::vector<AutomationLineId>& lines);

    // The lines a project written before S17 shows: one per pattern, in
    // pattern order, then one per track holding audio, in track order.
    // toValue leaves lanes out when they are exactly these, so an old project
    // serialises byte for byte as it did; fromValue rebuilds them when absent.
    [[nodiscard]] std::vector<Lane> legacyLanes() const;
    [[nodiscard]] bool isLaneOfSomeTrack(LaneId id) const noexcept;

    [[nodiscard]] TempoPoint* findTempoPointMutable(TempoPointId id) noexcept;
    void sortTempoPoints();
    Result<void> readTempoSequence(const Value::Array& points);

    std::vector<TempoPoint> tempo_{TempoPoint{originTempoPointId(), 0.0, 120.0}};
    TimeSignature timeSignature_;
    direction::Direction direction_;
    std::vector<Track> tracks_;
    std::vector<Pattern> patterns_;
    std::vector<Lane> lanes_;
    std::vector<Placement> arrangement_;
    std::vector<AudioClip> audio_;
    std::vector<Track> buses_;
    Track master_{defaultMaster()};
    std::vector<AutomationLine> automation_;
    TransportState transport_;
};

} // namespace daw::domain
