#pragma once

#include "daw/domain/BlobRef.h"
#include "daw/domain/Result.h"
#include "daw/domain/Value.h"

#include <cstdint>
#include <optional>
#include <string_view>

namespace daw::domain
{

// Who asked for a command.
//
// The product will grow two AI layers — a copilot that drives mixing, routing
// and plugin parameters, and a generative engine that injects MIDI and audio.
// Both go through the bus like any user, with no privilege whatsoever: the
// only trace they leave is this field. It is recorded from the first day
// because a journal that starts telling the truth later has already lied.
enum class Actor : std::uint8_t
{
    user = 0, // a human, through the interface
    copilot,  // an agent acting on an existing project
    generator // an agent producing new material
};

[[nodiscard]] std::string_view describe(Actor actor) noexcept;
[[nodiscard]] Result<Actor> parseActor(std::string_view text);

// The origin of a command: an actor, and optionally the context that actor
// acted upon — a prompt, a state extract, a model answer — named by digest in
// the content-addressed store, never carried by value.
//
// The store stays blind to all of this: an audio render produced by the
// generative engine is stored exactly like a take recorded by the user. The
// digest says what the bytes are, never who made them.
struct Provenance
{
    Actor actor{Actor::user};
    std::optional<BlobRef> context;

    [[nodiscard]] bool isUser() const noexcept { return actor == Actor::user && !context.has_value(); }

    [[nodiscard]] Result<void> validate() const;
    [[nodiscard]] Value toValue() const;
    [[nodiscard]] static Result<Provenance> fromValue(const Value& value);

    friend bool operator==(const Provenance& lhs, const Provenance& rhs);
    friend bool operator!=(const Provenance& lhs, const Provenance& rhs) { return !(lhs == rhs); }
};

} // namespace daw::domain
