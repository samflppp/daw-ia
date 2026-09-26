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

// A row of a pattern, by its shape rather than by its content: which track it
// plays, how many notes it holds, and the range they cover. Enough to say "the
// row of the Bass track in pattern 2, the one with sixteen notes"; not enough
// to move one, which is what clipNotes is for.
//
// No start and no length any more: a row has neither. Where it is played is a
// property of the placements, and how long it is a property of the pattern.
Value clipValue(const Clip& clip)
{
    Value::Object members{{"clipId", Value{clip.id.toString()}},
                          {"trackId", Value{clip.trackId.toString()}},
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

// A pattern, with its rows. This is where the notes of a project live now, so
// this is where the model looks for them: the tracks say who plays, the
// patterns say what, and the arrangement says when.
Value patternValue(const Pattern& pattern, const ProjectState& state)
{
    Value::Array clips;
    clips.reserve(pattern.clips.size());
    for (const auto& clip : pattern.clips)
        clips.push_back(clipValue(clip));

    Value::Array placements;
    for (const auto* placement : state.placementsOf(pattern.id))
    {
        placements.push_back(Value::object({{"placementId", Value{placement->id.toString()}},
                                            {"startBeats", Value{placement->startBeats}}}));
    }

    // The rank is what "le pattern 2" means: the screens show an unnamed
    // pattern by it, so the model has to read the same number the user reads.
    const auto index = state.patternIndex(pattern.id);
    const auto rank = static_cast<std::int64_t>(index ? index.value() + 1 : 0);

    return Value::object(
        {{"patternId", Value{pattern.id.toString()}},
         {"rank", Value{rank}},
         {"label", Value{pattern.name.empty() ? "Pattern " + std::to_string(rank) : pattern.name}},
         {"name", Value{pattern.name}},
         {"lengthBeats", Value{pattern.lengthBeats}},
         {"clips", Value::array(std::move(clips))},
         {"placements", Value::array(std::move(placements))}});
}

Value trackValue(const Track& track, std::size_t index)
{
    Value::Array plugins;
    plugins.reserve(track.plugins.size());
    for (const auto& plugin : track.plugins)
        plugins.push_back(pluginValue(plugin));

    Value::Object members{{"trackId", Value{track.id.toString()}},
                          {"index", Value{static_cast<std::int64_t>(index)}},
                          {"name", Value{track.name}},
                          {"volumeDb", Value{track.volumeDb}},
                          {"pan", Value{track.pan}},
                          {"muted", Value{track.muted}},
                          {"channelPitch", Value{static_cast<std::int64_t>(track.channelPitch)}},
                          {"plugins", Value::array(std::move(plugins))}};

    // A sampler channel says which sample it plays, by name: the digest means
    // nothing to a model.
    if (track.sample.has_value())
        members.emplace_back("sample", Value{track.sample->name});

    // The mix, only where it says something: an output other than the
    // master, sends, a solo. A project without buses reads as before.
    if (!track.output.isNil())
        members.emplace_back("output", Value{track.output.toString()});
    if (!track.sends.empty())
    {
        Value::Array sends;
        for (const auto& send : track.sends)
            sends.push_back(
                Value::object({{"busId", Value{send.bus.toString()}}, {"levelDb", Value{send.levelDb}}}));
        members.emplace_back("sends", Value::array(std::move(sends)));
    }
    if (track.soloed)
        members.emplace_back("soloed", Value{true});

    return Value::object(std::move(members));
}

// A bus or the master, told the way a track is, without what only a channel
// has: no index in the rack, no channel pitch, no sample.
Value stripValue(const Track& strip)
{
    auto value = trackValue(strip, 0);
    Value::Object members;
    for (const auto& [key, member] : *value.asObject())
    {
        if (key != "index" && key != "channelPitch" && key != "trackId")
            members.emplace_back(key, member);
    }
    members.emplace_back(strip.id == ProjectState::masterTrackId() ? "trackId" : "busId",
                         Value{strip.id.toString()});
    return Value::object(std::move(members));
}

// The audio clips on the timeline: which sample, on which track, from which
// beat, and for how long in seconds — a clip does not follow the tempo.
Value audioValue(const ProjectState& state)
{
    Value::Array clips;
    clips.reserve(state.audioClips().size());
    for (const auto& clip : state.audioClips())
    {
        clips.push_back(Value::object({{"clipId", Value{clip.id.toString()}},
                                       {"trackId", Value{clip.trackId.toString()}},
                                       {"sample", Value{clip.sample.name}},
                                       {"startBeats", Value{clip.startBeats}},
                                       {"seconds", Value{clip.sample.seconds}}}));
    }
    return Value::array(std::move(clips));
}

Value busesValue(const ProjectState& state)
{
    Value::Array buses;
    buses.reserve(state.buses().size());
    for (const auto& bus : state.buses())
        buses.push_back(stripValue(bus));
    return Value::array(std::move(buses));
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
                          {"points", Value::array(std::move(points))},
                          {"timeSignature", state.timeSignature().toValue()}});
}

Value transportValue(const ProjectState& state)
{
    const auto& transport = state.transport();
    return Value::object(
        {{"playing", Value{transport.playing}},
         {"positionBeats", Value{transport.positionBeats}},
         {"looping", Value{transport.looping}},
         {"loopStartBeats", Value{transport.loopStartBeats}},
         {"loopEndBeats", Value{transport.loopEndBeats}},
         {"mode", Value{std::string{transport.mode == PlayMode::pattern ? "pattern" : "song"}}},
         {"auditionedPatternId",
          Value{transport.auditionedPattern.isNil() ? std::string{}
                                                    : transport.auditionedPattern.toString()}},
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

    Value::Array patterns;
    patterns.reserve(state.patterns().size());
    for (const auto& pattern : state.patterns())
        patterns.push_back(patternValue(pattern, state));

    // Where the song ends: "ajoute le pattern 2 à la suite" starts there, and
    // without it the model would have to add up every placement itself.
    double arrangementEnd = 0.0;
    for (const auto& placement : state.arrangement())
    {
        if (const auto* pattern = state.findPattern(placement.patternId); pattern != nullptr)
            arrangementEnd = std::max(arrangementEnd, placement.startBeats + pattern->lengthBeats);
    }

    auto summary =
        Value::object({{"tempo", tempoValue(state)},
                       {"tracks", Value::array(std::move(tracks))},
                       {"buses", busesValue(state)},
                       {"master", stripValue(state.master())},
                       {"patterns", Value::array(std::move(patterns))},
                       {"arrangementEndBeats", Value{arrangementEnd}},
                       {"audioClips", audioValue(state)},
                       {"transport", transportValue(state)},
                       {"machinePlugins",
                        Value::object({{"total", Value{static_cast<std::int64_t>(plugins.available.size())}},
                                       {"listed", Value::array(std::move(installed))}})}});

    // The automation lines, whole: "fade the master out" has to know whether
    // the master already has a line, and a line is a handful of points. Only
    // when there is one, so a project without automation reads as before.
    if (!state.automation().empty())
    {
        Value::Array lines;
        lines.reserve(state.automation().size());
        for (const auto& line : state.automation())
            lines.push_back(line.toValue());
        static_cast<void>(summary.set("automation", Value::array(std::move(lines))));
    }
    return summary;
}

Result<Value> clipNotes(const ProjectState& state, ClipId clipId)
{
    const auto* clip = state.findClip(clipId);
    if (clip == nullptr)
        return fail(ErrorCode::notFound, "no clip " + clipId.toString());

    auto patternId = state.patternOfClip(clipId);
    if (!patternId)
        return patternId.error();

    const auto* pattern = state.findPattern(patternId.value());
    if (pattern == nullptr)
        return fail(ErrorCode::notFound, "no such pattern: " + patternId.value().toString());

    Value::Array notes;
    notes.reserve(clip->notes.size());
    for (const auto& note : clip->notes)
        notes.push_back(note.toValue());

    // The pattern's length travels with the notes: "quantize the notes of this
    // row" is answered against a grid, and the row itself carries none.
    return Value::object({{"clipId", Value{clip->id.toString()}},
                          {"trackId", Value{clip->trackId.toString()}},
                          {"patternId", Value{pattern->id.toString()}},
                          {"lengthBeats", Value{pattern->lengthBeats}},
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
