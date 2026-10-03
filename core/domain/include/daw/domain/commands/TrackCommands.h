#pragma once

#include "daw/domain/Ids.h"
#include "daw/domain/command/Command.h"
#include "daw/domain/project/ProjectState.h"

#include <cstddef>
#include <memory>
#include <string>

namespace daw::domain
{

// Creating a track was the last mutation that did not go through the bus: the
// application built a Track and pushed it into ProjectState directly. That was
// invisible until the journal became the truth on disk — a project whose
// tracks come from nowhere reopens empty, whatever the journal says.
//
// Like every other command, neither of these engenders an identifier: the
// caller generates the TrackId, or a replay would create a different project
// every time it ran.

// track.add — appends an empty track.
class AddTrack final : public Command
{
public:
    static constexpr std::string_view commandType = "track.add";

    AddTrack(TrackId trackId, std::string name, double volumeDb = 0.0);

    [[nodiscard]] static Result<std::unique_ptr<Command>> fromPayload(const Value& payload);

    [[nodiscard]] std::string_view type() const noexcept override { return commandType; }
    [[nodiscard]] Value payload() const override;
    [[nodiscard]] Result<Value> apply(ProjectState& state) const override;
    [[nodiscard]] Result<void> revert(ProjectState& state, const Value& undoRecord) const override;

    [[nodiscard]] TrackId trackId() const noexcept { return trackId_; }

private:
    TrackId trackId_;
    std::string name_;
    double volumeDb_;
};

// track.remove — takes the track out, rows, plugins and all. A bus too: its
// record then carries the strips that went into it and the sends it received,
// all back on an undo. The master cannot be removed.
//
// The undo record carries the whole track, not its identifier: a track holds
// plugin instances and captured state digests, and the command itself knows
// none of them. It carries the pattern rows that track played too, each with
// the pattern that held it — a row is content of a pattern, so removing a
// track empties as many patterns as it had rows, and only the record can fill
// them back in.
class RemoveTrack final : public Command
{
public:
    static constexpr std::string_view commandType = "track.remove";

    explicit RemoveTrack(TrackId trackId);

    [[nodiscard]] static Result<std::unique_ptr<Command>> fromPayload(const Value& payload);

    [[nodiscard]] std::string_view type() const noexcept override { return commandType; }
    [[nodiscard]] Value payload() const override;
    [[nodiscard]] Result<Value> apply(ProjectState& state) const override;
    [[nodiscard]] Result<void> revert(ProjectState& state, const Value& undoRecord) const override;

private:
    [[nodiscard]] Result<Value> applyToBus(ProjectState& state) const;
    [[nodiscard]] static Result<void> revertBus(ProjectState& state, const Value& undoRecord);

    TrackId trackId_;
};

// track.rename — gives a track another name.
//
// A name is not decoration: it is what a copilot is told to act upon ("mets la
// basse plus bas"), so being able to set one is being able to be understood.
class RenameTrack final : public Command
{
public:
    static constexpr std::string_view commandType = "track.rename";

    RenameTrack(TrackId trackId, std::string name);

    [[nodiscard]] static Result<std::unique_ptr<Command>> fromPayload(const Value& payload);

    [[nodiscard]] std::string_view type() const noexcept override { return commandType; }
    [[nodiscard]] Value payload() const override;
    [[nodiscard]] Result<Value> apply(ProjectState& state) const override;
    [[nodiscard]] Result<void> revert(ProjectState& state, const Value& undoRecord) const override;

    [[nodiscard]] TrackId trackId() const noexcept { return trackId_; }

private:
    TrackId trackId_;
    std::string name_;
};

// track.reorder — moves a track to another place in the list.
//
// An index and not "up" or "down": a relative verb depends on where the track
// currently is, and a payload whose meaning depends on the state it is replayed
// against is a payload that replays differently.
//
// An index past the end puts the track last rather than failing, exactly like
// insertTrack.
class ReorderTrack final : public Command
{
public:
    static constexpr std::string_view commandType = "track.reorder";

    ReorderTrack(TrackId trackId, std::size_t index);

    [[nodiscard]] static Result<std::unique_ptr<Command>> fromPayload(const Value& payload);

    [[nodiscard]] std::string_view type() const noexcept override { return commandType; }
    [[nodiscard]] Value payload() const override;
    [[nodiscard]] Result<Value> apply(ProjectState& state) const override;
    [[nodiscard]] Result<void> revert(ProjectState& state, const Value& undoRecord) const override;

    // Absorbs another move of the same track. Dragging a row across a list
    // crosses several places, and the history keeps one entry for the whole
    // drag -- the undo record of the first one, so undoing goes back to where
    // the row started and not one row up.
    [[nodiscard]] bool canCoalesceWith(const Command& newer) const noexcept override;

    [[nodiscard]] TrackId trackId() const noexcept { return trackId_; }

private:
    TrackId trackId_;
    std::size_t index_;
};

// track.set_pan — places a track in the stereo field, from -1 to +1.
//
// A continuous gesture, so it coalesces per track, exactly like
// track.set_volume: a pan knob dragged across the field is one history entry.
//
// It is its own command and not a second field of track.set_volume. The two
// are moved by two different controls, and merging them would make undoing a
// volume drag also move the track back across the stereo field.
//
// The command carries a position, never a pair of gains. How -0.5 becomes a
// left gain and a right gain is a decision of the projection, written down in
// engine/ProjectProjector.h: a payload that carried gains would freeze that
// law into every journal ever written.
class SetTrackPan final : public Command
{
public:
    static constexpr std::string_view commandType = "track.set_pan";

    SetTrackPan(TrackId trackId, double pan);

    [[nodiscard]] static Result<std::unique_ptr<Command>> fromPayload(const Value& payload);

    [[nodiscard]] std::string_view type() const noexcept override { return commandType; }
    [[nodiscard]] Reach reach() const noexcept override { return Reach::mix; }
    [[nodiscard]] Value payload() const override;
    [[nodiscard]] Result<Value> apply(ProjectState& state) const override;
    [[nodiscard]] Result<void> revert(ProjectState& state, const Value& undoRecord) const override;

    [[nodiscard]] bool canCoalesceWith(const Command& newer) const noexcept override;

    [[nodiscard]] TrackId trackId() const noexcept { return trackId_; }
    [[nodiscard]] double pan() const noexcept { return pan_; }

private:
    TrackId trackId_;
    double pan_;
};

// track.set_muted — silences a track, or gives it back.
//
// A switch, not a movement: it never coalesces, exactly like
// plugin.set_bypassed. The two are deliberately distinct commands. Mute is a
// property of the track, used while arranging; bypass is a property of one
// plugin in a chain, used while mixing. A single command for both would force
// a choice the moment a user wants a muted track whose reverb still rings on
// a send, or a bypassed compressor on a track that plays.
class SetTrackMuted final : public Command
{
public:
    static constexpr std::string_view commandType = "track.set_muted";

    SetTrackMuted(TrackId trackId, bool muted);

    [[nodiscard]] static Result<std::unique_ptr<Command>> fromPayload(const Value& payload);

    [[nodiscard]] std::string_view type() const noexcept override { return commandType; }
    [[nodiscard]] Reach reach() const noexcept override { return Reach::mix; }
    [[nodiscard]] Value payload() const override;
    [[nodiscard]] Result<Value> apply(ProjectState& state) const override;
    [[nodiscard]] Result<void> revert(ProjectState& state, const Value& undoRecord) const override;

    [[nodiscard]] TrackId trackId() const noexcept { return trackId_; }
    [[nodiscard]] bool muted() const noexcept { return muted_; }

private:
    TrackId trackId_;
    bool muted_;
};

// track.set_channel_pitch — the pitch a track plays when a rack step is lit.
//
// A kick track always plays the same note, and a channel rack lights cells
// rather than choosing pitches. That pitch had to live somewhere, and the
// choice is the one the S7bis review named: in the domain, not in the rack
// panel. A value known only by one screen is a value a copilot cannot use, and
// "mets un charleston en doubles-croches sur la piste 3" needs it to answer.
//
// It changes nothing that already sounds. It is read when a cell is lit, and
// the notes already written keep the pitch they were written with — otherwise
// changing a channel's pitch would silently rewrite a bassline.
//
// Coalesces per track, like every continuous control: dragging the value
// through a dozen semitones is one history entry.
class SetTrackChannelPitch final : public Command
{
public:
    static constexpr std::string_view commandType = "track.set_channel_pitch";

    SetTrackChannelPitch(TrackId trackId, int pitch);

    [[nodiscard]] static Result<std::unique_ptr<Command>> fromPayload(const Value& payload);

    [[nodiscard]] std::string_view type() const noexcept override { return commandType; }
    [[nodiscard]] Value payload() const override;
    [[nodiscard]] Result<Value> apply(ProjectState& state) const override;
    [[nodiscard]] Result<void> revert(ProjectState& state, const Value& undoRecord) const override;

    [[nodiscard]] bool canCoalesceWith(const Command& newer) const noexcept override;

    [[nodiscard]] TrackId trackId() const noexcept { return trackId_; }
    [[nodiscard]] int pitch() const noexcept { return pitch_; }

private:
    TrackId trackId_;
    int pitch_;
};

// track.set_role — the role a person gives a track in the mix (S20).
//
// Null takes the decision back: the role is guessed again. Not coalescable: a
// role is chosen, not dragged.
class SetTrackRole final : public Command
{
public:
    static constexpr std::string_view commandType = "track.set_role";

    SetTrackRole(TrackId trackId, std::optional<MixRole> role);

    [[nodiscard]] static Result<std::unique_ptr<Command>> fromPayload(const Value& payload);

    [[nodiscard]] std::string_view type() const noexcept override { return commandType; }
    [[nodiscard]] Reach reach() const noexcept override { return Reach::mix; }
    [[nodiscard]] Value payload() const override;
    [[nodiscard]] Result<Value> apply(ProjectState& state) const override;
    [[nodiscard]] Result<void> revert(ProjectState& state, const Value& undoRecord) const override;

private:
    TrackId trackId_;
    std::optional<MixRole> role_;
};

} // namespace daw::domain
