#include "daw/domain/tidy/Roles.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <map>

namespace daw::domain::tidy
{
namespace
{

// Lower case, and split on anything that is not a letter or a digit: « Sub
// Bass 02 » is sub, bass, 02. Bytes above ASCII are kept as letters, so
// « Mélodie » stays one word.
[[nodiscard]] std::vector<std::string> wordsOf(std::string_view text)
{
    std::vector<std::string> words;
    std::string current;
    for (const auto c : text)
    {
        const auto byte = static_cast<unsigned char>(c);
        if (std::isalnum(byte) != 0 || byte >= 0x80)
        {
            current.push_back(static_cast<char>(std::tolower(byte)));
        }
        else if (!current.empty())
        {
            words.push_back(current);
            current.clear();
        }
    }
    if (!current.empty())
        words.push_back(current);
    return words;
}

struct Keyword
{
    std::string_view word;
    Family family;
};

// Drum words first: « bass drum » is a kick before it is a bass. Short words
// must match a whole word; four letters and more may start one or sit inside
// one (« hihat », « basses », « kick01 »).
constexpr std::array<Keyword, 67> keywords{{
    {"kick", Family::kick},
    {"kik", Family::kick},
    {"bd", Family::kick},
    {"bassdrum", Family::kick},
    {"snare", Family::snare},
    {"snr", Family::snare},
    {"sd", Family::snare},
    {"rim", Family::snare},
    {"clap", Family::clap},
    {"clp", Family::clap},
    {"hihat", Family::hat},
    {"hat", Family::hat},
    {"hh", Family::hat},
    {"cymbal", Family::hat},
    {"ride", Family::hat},
    {"crash", Family::hat},
    {"perc", Family::percussion},
    {"tom", Family::percussion},
    {"shaker", Family::percussion},
    {"conga", Family::percussion},
    {"bongo", Family::percussion},
    {"cowbell", Family::percussion},
    {"tamb", Family::percussion},
    {"drum", Family::percussion},
    {"batterie", Family::percussion},
    {"kit", Family::percussion},
    {"808", Family::bass},
    {"bass", Family::bass},
    {"basse", Family::bass},
    {"sub", Family::bass},
    {"reese", Family::bass},
    {"chord", Family::chords},
    {"accord", Family::chords},
    {"keys", Family::chords},
    {"piano", Family::chords},
    {"rhodes", Family::chords},
    {"organ", Family::chords},
    {"orgue", Family::chords},
    {"stab", Family::chords},
    {"pad", Family::pad},
    {"nappe", Family::pad},
    {"string", Family::pad},
    {"cordes", Family::pad},
    {"atmo", Family::pad},
    {"ambient", Family::pad},
    {"lead", Family::melody},
    {"melod", Family::melody},
    {"mélod", Family::melody},
    {"pluck", Family::melody},
    {"bell", Family::melody},
    {"flute", Family::melody},
    {"arp", Family::melody},
    {"guitar", Family::melody},
    {"guitare", Family::melody},
    {"sax", Family::melody},
    {"topline", Family::melody},
    {"fx", Family::fx},
    {"sfx", Family::fx},
    {"riser", Family::fx},
    {"impact", Family::fx},
    {"sweep", Family::fx},
    {"noise", Family::fx},
    {"whoosh", Family::fx},
    {"vox", Family::vocal},
    {"vocal", Family::vocal},
    {"voix", Family::vocal},
    {"voice", Family::vocal},
}};

[[nodiscard]] bool matches(const std::string& token, std::string_view keyword)
{
    if (token == keyword)
        return true;
    if (keyword.size() < 3)
        return false;
    if (token.rfind(keyword, 0) == 0)
        return true;
    return keyword.size() >= 4 && token.find(keyword) != std::string::npos;
}

[[nodiscard]] Guess fromName(std::string_view name, Evidence evidence, double confidence)
{
    // Keywords outside, words inside: the order of the list decides, so a
    // drum word wins over the bass it contains. The words run together are
    // tried too, for « Bass Drum ».
    auto words = wordsOf(name);
    if (words.size() > 1)
    {
        std::string joined;
        for (const auto& word : words)
            joined += word;
        words.push_back(joined);
    }

    for (const auto& keyword : keywords)
    {
        for (const auto& token : words)
        {
            if (matches(token, keyword.word))
                return Guess{keyword.family, evidence, confidence, std::string{name}};
        }
    }
    return {};
}

[[nodiscard]] int polyphony(const std::vector<Note>& notes)
{
    int most = 0;
    for (const auto& note : notes)
    {
        const auto at = note.startBeats;
        const auto sounding =
            std::count_if(notes.begin(),
                          notes.end(),
                          [at](const Note& other)
                          { return other.startBeats <= at && at < other.startBeats + other.lengthBeats; });
        most = std::max(most, static_cast<int>(sounding));
    }
    return most;
}

[[nodiscard]] Guess fromNotes(const std::vector<Note>& notes)
{
    if (notes.empty())
        return {};

    std::vector<int> pitches;
    double length = 0.0;
    for (const auto& note : notes)
    {
        pitches.push_back(note.pitch);
        length += note.lengthBeats;
    }
    std::sort(pitches.begin(), pitches.end());
    const auto median = pitches[pitches.size() / 2];
    const auto meanLength = length / static_cast<double>(notes.size());

    // Chords sound together; held ones are a pad. Weaker than any name.
    if (polyphony(notes) >= 3)
        return Guess{meanLength >= 2.0 ? Family::pad : Family::chords, Evidence::notes, 0.45, {}};
    if (median < 48)
        return Guess{Family::bass, Evidence::notes, 0.4, {}};
    return Guess{Family::melody, Evidence::notes, 0.35, {}};
}

} // namespace

bool isDefaultName(std::string_view name)
{
    const auto words = wordsOf(name);
    if (words.empty())
        return true;
    if (words.size() > 2)
        return false;

    static constexpr std::array<std::string_view, 7> generic{
        "piste", "track", "canal", "channel", "insert", "ligne", "pattern"};
    const auto isGeneric =
        std::find(generic.begin(), generic.end(), std::string_view{words.front()}) != generic.end();
    const auto numbered =
        words.size() == 1 ||
        std::all_of(words.back().begin(),
                    words.back().end(),
                    [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; });
    return isGeneric && numbered;
}

Family familyOfName(std::string_view name)
{
    return fromName(name, Evidence::none, 0.0).family;
}

Guess classify(const Clues& clues)
{
    // The strongest witness speaks first: a name somebody chose, then what the
    // instrument or the sample calls itself, then the notes.
    if (!isDefaultName(clues.trackName))
    {
        if (auto guess = fromName(clues.trackName, Evidence::trackName, 0.9); guess.family != Family::unknown)
            return guess;
    }
    if (auto guess = fromName(clues.presetName, Evidence::presetName, 0.8); guess.family != Family::unknown)
        return guess;
    if (auto guess = fromName(clues.sampleName, Evidence::sampleName, 0.75); guess.family != Family::unknown)
        return guess;
    if (auto guess = fromName(clues.pluginName, Evidence::pluginName, 0.5); guess.family != Family::unknown)
        return guess;
    if (clues.sampleChannel)
        return Guess{Family::percussion, Evidence::sampleName, 0.3, clues.sampleName};
    return fromNotes(clues.notes);
}

Clues cluesOf(const ProjectState& state, TrackId track, const PresetNames& presets)
{
    Clues clues{};
    const auto* found = state.findTrack(track);
    if (found == nullptr)
        return clues;

    clues.trackName = found->name;
    if (presets)
        clues.presetName = presets(track);
    if (!found->plugins.empty())
        clues.pluginName = found->plugins.front().ref.name;
    if (found->sample.has_value())
    {
        clues.sampleChannel = true;
        clues.sampleName = found->sample->name;
    }
    for (const auto& pattern : state.patterns())
    {
        if (const auto* row = pattern.findClipForTrack(track); row != nullptr)
            clues.notes.insert(clues.notes.end(), row->notes.begin(), row->notes.end());
    }
    return clues;
}

Guess classifyTrack(const ProjectState& state, TrackId track, const PresetNames& presets)
{
    return classify(cluesOf(state, track, presets));
}

std::vector<TrackId> tracksOfLane(const ProjectState& state, LaneId lane)
{
    std::map<TrackId, std::size_t> notes;
    std::vector<TrackId> order;
    const auto count = [&](TrackId track, std::size_t amount)
    {
        if (notes.find(track) == notes.end())
            order.push_back(track);
        notes[track] += amount;
    };

    for (const auto& placement : state.arrangement())
    {
        if (placement.laneId != lane)
            continue;
        if (const auto* pattern = state.findPattern(placement.patternId); pattern != nullptr)
        {
            for (const auto& row : pattern->clips)
                count(row.trackId, row.notes.size());
        }
    }
    for (const auto& clip : state.audioClips())
    {
        if (clip.laneId == lane)
            count(clip.trackId, 1);
    }

    std::stable_sort(
        order.begin(), order.end(), [&notes](TrackId lhs, TrackId rhs) { return notes[lhs] > notes[rhs]; });
    return order;
}

Guess classifyLane(const ProjectState& state, LaneId lane, const PresetNames& presets)
{
    const auto* found = state.findLane(lane);
    if (found == nullptr)
        return {};

    if (!isDefaultName(found->name))
    {
        if (auto guess = fromName(found->name, Evidence::lineName, 0.95); guess.family != Family::unknown)
            return guess;
    }

    // Else the most confident reading of what it holds.
    Guess best{};
    for (const auto track : tracksOfLane(state, lane))
    {
        const auto guess = classifyTrack(state, track, presets);
        if (guess.confidence > best.confidence)
            best = guess;
    }
    for (const auto& clip : state.audioClips())
    {
        if (clip.laneId != lane)
            continue;
        if (auto guess = fromName(clip.sample.name, Evidence::sampleName, 0.75);
            guess.confidence > best.confidence)
            best = guess;
    }
    return best;
}

std::string suggestedName(Family family)
{
    switch (family)
    {
    case Family::kick:
        return "Kick";
    case Family::snare:
        return "Snare";
    case Family::clap:
        return "Clap";
    case Family::hat:
        return "Hi-hat";
    case Family::percussion:
        return "Percs";
    case Family::bass:
        return "Basse";
    case Family::chords:
        return "Accords";
    case Family::pad:
        return "Nappe";
    case Family::melody:
        return "Mélodie";
    case Family::fx:
        return "FX";
    case Family::vocal:
        return "Voix";
    case Family::unknown:
        break;
    }
    return {};
}

std::optional<generation::Role> generationRole(Family family)
{
    switch (family)
    {
    case Family::kick:
    case Family::snare:
    case Family::clap:
    case Family::hat:
    case Family::percussion:
        return generation::Role::rhythm;
    case Family::bass:
        return generation::Role::bass;
    case Family::chords:
    case Family::pad:
        return generation::Role::chords;
    case Family::melody:
        return generation::Role::melody;
    case Family::fx:
    case Family::vocal:
    case Family::unknown:
        break;
    }
    return std::nullopt;
}

std::string because(const Guess& guess)
{
    switch (guess.evidence)
    {
    case Evidence::lineName:
        return "d'après le nom de la ligne « " + guess.clue + " »";
    case Evidence::trackName:
        return "d'après le nom de la piste « " + guess.clue + " »";
    case Evidence::presetName:
        return "d'après le preset « " + guess.clue + " »";
    case Evidence::sampleName:
        return "d'après le sample « " + guess.clue + " »";
    case Evidence::pluginName:
        return "d'après le plugin « " + guess.clue + " »";
    case Evidence::notes:
        return "d'après ses notes";
    case Evidence::none:
        break;
    }
    return {};
}

} // namespace daw::domain::tidy
