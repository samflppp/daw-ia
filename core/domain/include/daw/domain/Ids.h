#pragma once

#include "daw/domain/Result.h"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace daw::domain
{

// 128-bit identifier in ULID form: 48 bits of millisecond timestamp followed by
// 80 random bits, written as 26 Crockford base32 characters.
//
// Chosen over juce::Uuid (forbidden here: the domain links no framework) and
// over a per-process counter (meaningless once a command is replayed in another
// process). Two useful properties: identifiers sort by creation time, and the
// textual form is a plain ASCII key, ready for SQLite and for JSON-RPC.
class Ulid
{
public:
    static constexpr std::size_t textLength = 26;

    Ulid() noexcept = default;
    explicit Ulid(std::array<std::uint8_t, 16> bytes) noexcept
        : bytes_{bytes}
    {
    }

    [[nodiscard]] static Ulid generate();
    [[nodiscard]] static Result<Ulid> parse(std::string_view text);

    [[nodiscard]] std::string toString() const;
    [[nodiscard]] bool isNil() const noexcept;
    [[nodiscard]] const std::array<std::uint8_t, 16>& bytes() const noexcept { return bytes_; }

    friend bool operator==(const Ulid& lhs, const Ulid& rhs) noexcept { return lhs.bytes_ == rhs.bytes_; }
    friend bool operator!=(const Ulid& lhs, const Ulid& rhs) noexcept { return !(lhs == rhs); }
    friend bool operator<(const Ulid& lhs, const Ulid& rhs) noexcept { return lhs.bytes_ < rhs.bytes_; }

private:
    std::array<std::uint8_t, 16> bytes_{};
};

// Distinct identifier types so a ClipId never silently passes as a TrackId.
template <typename Tag>
class EntityId
{
public:
    EntityId() noexcept = default;
    explicit EntityId(Ulid value) noexcept
        : value_{value}
    {
    }

    [[nodiscard]] static EntityId generate() { return EntityId{Ulid::generate()}; }

    [[nodiscard]] static Result<EntityId> parse(std::string_view text)
    {
        auto parsed = Ulid::parse(text);
        if (!parsed)
            return parsed.error();
        return EntityId{parsed.value()};
    }

    [[nodiscard]] std::string toString() const { return value_.toString(); }
    [[nodiscard]] bool isNil() const noexcept { return value_.isNil(); }
    [[nodiscard]] const Ulid& value() const noexcept { return value_; }

    friend bool operator==(const EntityId& lhs, const EntityId& rhs) noexcept
    {
        return lhs.value_ == rhs.value_;
    }

    friend bool operator!=(const EntityId& lhs, const EntityId& rhs) noexcept { return !(lhs == rhs); }

    friend bool operator<(const EntityId& lhs, const EntityId& rhs) noexcept
    {
        return lhs.value_ < rhs.value_;
    }

private:
    Ulid value_{};
};

struct CommandIdTag;
struct GestureIdTag;
struct GroupIdTag;
struct TrackIdTag;
struct ClipIdTag;
struct NoteIdTag;
struct PluginIdTag;
struct TempoPointIdTag;
struct PatternIdTag;
struct PlacementIdTag;
struct AudioClipIdTag;
struct AutomationLineIdTag;
struct AutomationPointIdTag;

using CommandId = EntityId<CommandIdTag>;
using GestureId = EntityId<GestureIdTag>;

// Identifies one history entry built from several commands: "add a Bass track
// and put Vital on it" is two commands and one thing the user did. A gesture
// is not the same idea and does not replace it — a gesture merges commands
// that are compatible, a group holds commands that are not.
using GroupId = EntityId<GroupIdTag>;
using TrackId = EntityId<TrackIdTag>;
using ClipId = EntityId<ClipIdTag>;
using NoteId = EntityId<NoteIdTag>;

// Identifies one plugin *instance* on a track, not the plugin binary: two
// copies of the same synth on the same track are two PluginIds. The binary is
// named by PluginRef, which is not an identity the project engenders.
using PluginId = EntityId<PluginIdTag>;

// Identifies one pattern: a piece of content, several tracks wide, that knows
// nothing about where it is played. Its identity is separate from a placement's
// so that the same pattern can be laid on the timeline as often as wanted, and
// so that editing it once is felt everywhere it was laid.
using PatternId = EntityId<PatternIdTag>;

// Identifies one laying of a pattern on the timeline. It carries a position and
// nothing else: a placement holds no note, which is exactly what makes "modify
// a pattern placed eight times" one command instead of eight.
using PlacementId = EntityId<PlacementIdTag>;

// Identifies one audio clip laid on the timeline: a sample, a track it sounds
// on, and a beat. Its own kind of identifier, because it is neither a pattern
// nor a placement of one: it holds no note, and nothing edits it from a rack.
using AudioClipId = EntityId<AudioClipIdTag>;

// Identifies one tempo change on the timeline. A point is named rather than
// located because it moves: a payload that designated a point by its position
// in beats would aim at a different point as soon as another command moved it.
using TempoPointId = EntityId<TempoPointIdTag>;

// Identify one automation line and one point of it. Named rather than located
// for the reason a tempo point is: a point moves, and a payload that aimed at
// "the point at beat 12" would aim at another one after a drag.
using AutomationLineId = EntityId<AutomationLineIdTag>;
using AutomationPointId = EntityId<AutomationPointIdTag>;

} // namespace daw::domain
