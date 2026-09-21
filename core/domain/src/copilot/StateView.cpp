#include "daw/domain/copilot/StateView.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace daw::domain::copilot
{
namespace
{

std::string lowered(std::string_view text)
{
    std::string result;
    result.reserve(text.size());
    for (const char character : text)
        result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));

    return result;
}

Value pluginRefValue(const PluginRef& ref)
{
    return Value::object(
        {{"format", Value{ref.format}}, {"identifier", Value{ref.identifier}}, {"name", Value{ref.name}}});
}

// A plugin instance, without its parameters and without its state digest. A
// sampler exposes thousands of parameters; sending them would cost more than
// the whole project and say less than the plugin's name.
Value pluginValue(const PluginInstance& plugin)
{
    return Value::object({{"pluginId", Value{plugin.id.toString()}},
                          {"ref", pluginRefValue(plugin.ref)},
                          {"bypassed", Value{plugin.bypassed}},
                          {"touchedParams", Value{static_cast<std::int64_t>(plugin.params.size())}}});
}

// A clip, by its shape rather than by its content: how many notes, and the
// range they cover. Enough to say "the clip on the Bass track, the one with
// sixteen notes"; not enough to move one, which is what clipNotes is for.
Value clipValue(const Clip& clip)
{
    Value::Object members{{"clipId", Value{clip.id.toString()}},
                          {"startBeats", Value{clip.startBeats}},
                          {"lengthBeats", Value{clip.lengthBeats}},
                          {"noteCount", Value{static_cast<std::int64_t>(clip.notes.size())}}};

    if (!clip.notes.empty())
    {
        const auto range =
            std::minmax_element(clip.notes.begin(),
                                clip.notes.end(),
                                [](const Note& left, const Note& right) { return left.pitch < right.pitch; });

        members.emplace_back("lowestPitch", Value{static_cast<std::int64_t>(range.first->pitch)});
        members.emplace_back("highestPitch", Value{static_cast<std::int64_t>(range.second->pitch)});
    }

    return Value::object(std::move(members));
}

Value trackValue(const Track& track, std::size_t index)
{
    Value::Array clips;
    clips.reserve(track.clips.size());
    for (const auto& clip : track.clips)
        clips.push_back(clipValue(clip));

    Value::Array plugins;
    plugins.reserve(track.plugins.size());
    for (const auto& plugin : track.plugins)
        plugins.push_back(pluginValue(plugin));

    return Value::object({{"trackId", Value{track.id.toString()}},
                          {"index", Value{static_cast<std::int64_t>(index)}},
                          {"name", Value{track.name}},
                          {"volumeDb", Value{track.volumeDb}},
                          {"pan", Value{track.pan}},
                          {"muted", Value{track.muted}},
                          {"clips", Value::array(std::move(clips))},
                          {"plugins", Value::array(std::move(plugins))}});
}

Value tempoValue(const ProjectState& state)
{
    Value::Array points;
    points.reserve(state.tempoPoints().size());
    for (const auto& point : state.tempoPoints())
    {
        points.push_back(Value::object({{"pointId", Value{point.id.toString()}},
                                        {"startBeats", Value{point.startBeats}},
                                        {"beatsPerMinute", Value{point.beatsPerMinute}}}));
    }

    // The sequence is short by nature — one point per tempo change — so it is
    // sent whole. The point at the origin is named, because "set the tempo to
    // 140" aims at it and nothing else tells the model which one it is.
    return Value::object({{"originPointId", Value{ProjectState::originTempoPointId().toString()}},
                          {"points", Value::array(std::move(points))}});
}

Value transportValue(const ProjectState& state)
{
    const auto& transport = state.transport();
    return Value::object({{"playing", Value{transport.playing}},
                          {"positionBeats", Value{transport.positionBeats}},
                          {"looping", Value{transport.looping}},
                          {"loopStartBeats", Value{transport.loopStartBeats}},
                          {"loopEndBeats", Value{transport.loopEndBeats}},
                          {"tempoAtPosition", Value{state.tempoAt(transport.positionBeats)}}});
}

} // namespace

Value summarise(const ProjectState& state, const MachinePlugins& plugins)
{
    Value::Array tracks;
    tracks.reserve(state.tracks().size());
    for (std::size_t index = 0; index < state.tracks().size(); ++index)
        tracks.push_back(trackValue(state.tracks()[index], index));

    Value::Array installed;
    const auto listed = std::min(plugins.available.size(), MachinePlugins::maxListed);
    installed.reserve(listed);
    for (std::size_t index = 0; index < listed; ++index)
        installed.push_back(pluginRefValue(plugins.available[index]));

    return Value::object(
        {{"tempo", tempoValue(state)},
         {"tracks", Value::array(std::move(tracks))},
         {"transport", transportValue(state)},
         {"machinePlugins",
          Value::object({{"total", Value{static_cast<std::int64_t>(plugins.available.size())}},
                         {"listed", Value::array(std::move(installed))}})}});
}

Result<Value> clipNotes(const ProjectState& state, ClipId clipId)
{
    const auto* clip = state.findClip(clipId);
    if (clip == nullptr)
        return fail(ErrorCode::notFound, "no clip " + clipId.toString());

    Value::Array notes;
    notes.reserve(clip->notes.size());
    for (const auto& note : clip->notes)
        notes.push_back(note.toValue());

    return Value::object({{"clipId", Value{clip->id.toString()}},
                          {"startBeats", Value{clip->startBeats}},
                          {"lengthBeats", Value{clip->lengthBeats}},
                          {"notes", Value::array(std::move(notes))}});
}

Value findPlugins(const MachinePlugins& plugins, std::string_view query)
{
    const auto needle = lowered(query);

    Value::Array found;
    for (const auto& ref : plugins.available)
    {
        if (needle.empty() || lowered(ref.name).find(needle) != std::string::npos)
            found.push_back(pluginRefValue(ref));
    }

    return Value::object({{"query", Value{std::string{query}}}, {"found", Value::array(std::move(found))}});
}

} // namespace daw::domain::copilot
