#pragma once

#include "daw/domain/Ids.h"
#include "daw/domain/command/Command.h"

#include <memory>

namespace daw::domain
{

// clip.create_midi — creates a pattern of one track, and places it.
//
// The payload is the one it has always had: a track, a clip, a start and a
// length. What it means changed when content and position were split apart —
// a clip no longer carries a start or a length, so this command now builds the
// three things that payload describes:
//
//   the pattern, as long as lengthBeats says;
//   the row that track plays in it, identified by clipId;
//   the placement that lays the pattern at startBeats.
//
// The two new identifiers are derived from the clip's own bytes rather than
// drawn: eight weeks of journals name only the clip, and a replay that invented
// a pattern identifier would build a different project every time it ran. The
// rule is untouched — nothing here engenders an identifier.
//
// The clip identifier is an argument, not something apply() invents: a payload
// replayed twice must produce the same project, and an identifier drawn at
// execution time would break that.
class CreateMidiClip final : public Command
{
public:
    static constexpr std::string_view commandType = "clip.create_midi";

    CreateMidiClip(TrackId trackId, ClipId clipId, double startBeats, double lengthBeats);

    [[nodiscard]] static Result<std::unique_ptr<Command>> fromPayload(const Value& payload);

    [[nodiscard]] std::string_view type() const noexcept override { return commandType; }
    [[nodiscard]] Value payload() const override;
    [[nodiscard]] Result<Value> apply(ProjectState& state) const override;
    [[nodiscard]] Result<void> revert(ProjectState& state, const Value& undoRecord) const override;

private:
    TrackId trackId_;
    ClipId clipId_;
    double startBeats_;
    double lengthBeats_;
};

} // namespace daw::domain
