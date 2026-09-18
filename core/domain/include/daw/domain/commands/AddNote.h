#pragma once

#include "daw/domain/Ids.h"
#include "daw/domain/command/Command.h"
#include "daw/domain/project/ProjectState.h"

#include <memory>

namespace daw::domain
{

// note.add — adds one note inside an existing clip.
//
// Validates the nested case: the target is not a top-level entity, so the
// command must fail cleanly, and change nothing, when the clip is gone.
class AddNote final : public Command
{
public:
    static constexpr std::string_view commandType = "note.add";

    AddNote(ClipId clipId, Note note);

    [[nodiscard]] static Result<std::unique_ptr<Command>> fromPayload(const Value& payload);

    [[nodiscard]] std::string_view type() const noexcept override { return commandType; }
    [[nodiscard]] Value payload() const override;
    [[nodiscard]] Result<Value> apply(ProjectState& state) const override;
    [[nodiscard]] Result<void> revert(ProjectState& state, const Value& undoRecord) const override;

private:
    ClipId clipId_;
    Note note_;
};

} // namespace daw::domain
