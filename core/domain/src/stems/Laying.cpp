#include "daw/domain/stems/Laying.h"

#include "daw/domain/commands/SampleCommands.h"
#include "daw/domain/commands/TrackCommands.h"

#include <algorithm>

namespace daw::domain::stems
{

std::string_view label(std::string_view stem) noexcept
{
    if (stem == "vocals")
        return "Voix";
    if (stem == "drums")
        return "Batterie";
    if (stem == "bass")
        return "Basse";
    if (stem == "other")
        return "Le reste";
    return {};
}

std::optional<MixRole> roleOf(std::string_view stem) noexcept
{
    if (stem == "vocals")
        return MixRole::vocal;
    if (stem == "drums")
        return MixRole::percussion;
    if (stem == "bass")
        return MixRole::bass;
    return std::nullopt;
}

Result<std::vector<std::unique_ptr<Command>>> commandsFor(const ProjectState& state, const Laying& laying)
{
    if (laying.stems.empty())
        return fail(ErrorCode::invalidArgument, "no stem to lay");
    if (laying.replaced.has_value() && state.findAudioClip(*laying.replaced) == nullptr)
        return fail(ErrorCode::notFound, "no such audio clip: " + laying.replaced->toString());

    for (auto it = laying.stems.begin(); it != laying.stems.end(); ++it)
    {
        if (label(it->name).empty())
            return fail(ErrorCode::invalidArgument, "unknown stem: " + it->name);
        const auto twice = [&it](const Stem& other)
        { return other.name == it->name || other.track == it->track || other.clip == it->clip; };
        if (std::any_of(std::next(it), laying.stems.end(), twice))
            return fail(ErrorCode::conflict, "stem given twice: " + it->name);
        if (state.findTrack(it->track) != nullptr)
            return fail(ErrorCode::conflict, "track already in the project: " + it->track.toString());
        if (state.findAudioClip(it->clip) != nullptr)
            return fail(ErrorCode::conflict, "audio clip already in the project: " + it->clip.toString());
    }

    // In the order of `names`, whatever order they came in: the tracks are
    // laid voice, drums, bass, the rest, every time.
    std::vector<const Stem*> ordered;
    for (const auto name : names)
    {
        for (const auto& stem : laying.stems)
        {
            if (stem.name == name)
                ordered.push_back(&stem);
        }
    }

    std::vector<std::unique_ptr<Command>> commands;
    for (const auto* stem : ordered)
    {
        commands.push_back(std::make_unique<AddTrack>(
            stem->track, std::string{label(stem->name)} + " — " + laying.sourceName, 0.0));
        if (const auto role = roleOf(stem->name); role.has_value())
            commands.push_back(std::make_unique<SetTrackRole>(stem->track, role));
        commands.push_back(
            std::make_unique<PlaceAudio>(stem->clip, stem->track, stem->sample, laying.startBeats));
    }
    if (laying.replaced.has_value())
        commands.push_back(std::make_unique<RemoveAudio>(*laying.replaced));
    return commands;
}

std::string groupLabel(const Laying& laying)
{
    return "Séparer en stems : " + laying.sourceName;
}

} // namespace daw::domain::stems
