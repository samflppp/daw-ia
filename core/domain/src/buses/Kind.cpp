#include "daw/domain/buses/Kind.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <vector>

namespace daw::domain::buses
{
namespace
{

// Words that cannot mean anything else.
constexpr std::array<std::string_view, 3> sureReverb{"reverb", "verb", "reverberation"};
constexpr std::array<std::string_view, 2> sureDelay{"delay", "echo"};

// Words that often mean one, not always: « TR5 Mic Room » models a
// microphone, « Space Modulator » modulates.
constexpr std::array<std::string_view, 10> likelyReverb{
    "room", "hall", "plate", "chamber", "spring", "shimmer", "ambience", "space", "cathedral", "church"};
constexpr std::array<std::string_view, 6> likelyDelay{
    "tap", "taps", "multitap", "dly", "pingpong", "repeats"};

bool isLower(char c)
{
    return std::islower(static_cast<unsigned char>(c)) != 0;
}

bool isUpper(char c)
{
    return std::isupper(static_cast<unsigned char>(c)) != 0;
}

bool isAlnum(char c)
{
    return std::isalnum(static_cast<unsigned char>(c)) != 0;
}

std::string lower(std::string_view text)
{
    std::string said(text);
    std::transform(said.begin(),
                   said.end(),
                   said.begin(),
                   [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
    return said;
}

// « ValhallaPlate » → valhalla, plate ; « TR5 Mic Room » → tr5, mic, room.
std::vector<std::string> wordsOf(std::string_view name)
{
    std::vector<std::string> words;
    std::string word;
    const auto flush = [&]
    {
        if (!word.empty())
            words.push_back(lower(word));
        word.clear();
    };
    for (std::size_t index = 0; index < name.size(); ++index)
    {
        const char c = name[index];
        if (!isAlnum(c))
        {
            flush();
            continue;
        }
        if (isUpper(c) && index > 0 && isLower(name[index - 1]))
            flush();
        word.push_back(c);
    }
    flush();
    return words;
}

template <std::size_t N>
bool any(const std::vector<std::string>& words, const std::array<std::string_view, N>& list)
{
    return std::any_of(words.begin(),
                       words.end(),
                       [&](const std::string& word)
                       { return std::find(list.begin(), list.end(), word) != list.end(); });
}

// The catalogue's kinds, « Fx » left out: « Fx|Reverb » says reverb, « Fx »
// alone says nothing.
std::optional<Kind> fromCatalogue(std::string_view category)
{
    const auto text = lower(category);
    bool named = false;
    std::size_t start = 0;
    while (start <= text.size())
    {
        const auto end = std::min(text.find('|', start), text.size());
        const auto part = text.substr(start, end - start);
        if (part.find("reverb") != std::string::npos)
            return Kind::reverb;
        if (part.find("delay") != std::string::npos || part.find("echo") != std::string::npos)
            return Kind::delay;
        if (!part.empty() && part != "fx")
            named = true;
        start = end + 1;
    }
    return named ? std::optional<Kind>{Kind::other} : std::nullopt;
}

} // namespace

Recognition
recognise(std::string_view name, std::string_view category, bool instrument, std::optional<Kind> answer)
{
    if (answer)
        return {answer, KnownBy::answer, std::nullopt};
    if (instrument)
        return {Kind::instrument, KnownBy::catalogue, std::nullopt};
    if (const auto known = fromCatalogue(category))
        return {known, KnownBy::catalogue, std::nullopt};

    const auto words = wordsOf(name);
    const bool reverb = any(words, sureReverb);
    const bool delay = any(words, sureDelay);
    if (reverb != delay)
        return {reverb ? Kind::reverb : Kind::delay, KnownBy::name, std::nullopt};
    if (reverb && delay)
        return {std::nullopt, KnownBy::unknown, std::nullopt};

    const bool likeReverb = any(words, likelyReverb);
    const bool likeDelay = any(words, likelyDelay);
    if (likeReverb != likeDelay)
        return {std::nullopt, KnownBy::unknown, likeReverb ? Kind::reverb : Kind::delay};
    return {std::nullopt, KnownBy::unknown, std::nullopt};
}

std::string toString(Kind kind)
{
    switch (kind)
    {
    case Kind::reverb:
        return "reverb";
    case Kind::delay:
        return "delay";
    case Kind::instrument:
        return "instrument";
    case Kind::other:
    default:
        return "other";
    }
}

std::optional<Kind> kindFromString(std::string_view text)
{
    for (const auto kind : {Kind::reverb, Kind::delay, Kind::instrument, Kind::other})
        if (toString(kind) == text)
            return kind;
    return std::nullopt;
}

} // namespace daw::domain::buses
