#include "daw/domain/generation/Phrase.h"

#include <array>
#include <cctype>
#include <cmath>
#include <string_view>

namespace daw::domain::generation
{
namespace
{

constexpr double epsilon = 1e-6;

constexpr std::array<std::string_view, 17> numberWords{"zéro",
                                                       "une",
                                                       "deux",
                                                       "trois",
                                                       "quatre",
                                                       "cinq",
                                                       "six",
                                                       "sept",
                                                       "huit",
                                                       "neuf",
                                                       "dix",
                                                       "onze",
                                                       "douze",
                                                       "treize",
                                                       "quatorze",
                                                       "quinze",
                                                       "seize"};

[[nodiscard]] std::string count(long long value, std::string_view one, std::string_view many)
{
    if (value == 1)
        return std::string{one};
    if (value >= 0 && value < static_cast<long long>(numberWords.size()))
        return std::string{numberWords[static_cast<std::size_t>(value)]} + " " + std::string{many};
    return std::to_string(value) + " " + std::string{many};
}

[[nodiscard]] std::string_view roleWords(Role role) noexcept
{
    switch (role)
    {
    case Role::melody:
        return "de mélodie";
    case Role::bass:
        return "de basse";
    case Role::chords:
        return "d'accords";
    case Role::rhythm:
        return "de rythme";
    }
    return "de mélodie";
}

[[nodiscard]] std::string_view resolutionWords(Resolution resolution) noexcept
{
    switch (resolution)
    {
    case Resolution::quarter:
        return "en noires";
    case Resolution::eighth:
        return "en croches";
    case Resolution::sixteenth:
        return "en doubles-croches";
    }
    return "en croches";
}

[[nodiscard]] std::string_view registerWords(Register reg) noexcept
{
    switch (reg)
    {
    case Register::low:
        return "dans le grave";
    case Register::mid:
        return "dans le médium";
    case Register::high:
        return "dans l'aigu";
    }
    return "dans le médium";
}

[[nodiscard]] std::string_view densityWords(Density density) noexcept
{
    switch (density)
    {
    case Density::sparse:
        return "aérée";
    case Density::medium:
        return "ni vide ni chargée";
    case Density::dense:
        return "chargée";
    }
    return "ni vide ni chargée";
}

// What a form sounds like, not its letters.
[[nodiscard]] std::string_view formWords(Form form) noexcept
{
    switch (form)
    {
    case Form::free:
        return "sans motif qui revient";
    case Form::loop:
        return "en boucle";
    case Form::varied:
        return "qui varie à chaque retour";
    case Form::aaPrime:
        return "une idée puis sa réponse";
    case Form::aab:
        return "une idée deux fois, puis une relance";
    case Form::aaba:
        return "une idée, sa reprise, un détour, puis le retour";
    case Form::aaab:
        return "une idée trois fois, puis une relance";
    }
    return "en boucle";
}

// "fa dièse mineur", "si bémol majeur": the key as it is said, not written.
[[nodiscard]] std::string spokenKey(Key key)
{
    const auto written = describe(key); // "Fa# mineur", "Sib majeur"
    const auto space = written.find(' ');
    auto name = written.substr(0, space);
    const auto mode = space == std::string::npos ? std::string{} : written.substr(space);

    std::string accidental;
    if (name.size() > 2 && name.back() == '#')
    {
        name.pop_back();
        accidental = " dièse";
    }
    else if (name.size() > 2 && name.back() == 'b')
    {
        name.pop_back();
        accidental = " bémol";
    }
    if (!name.empty())
        name.front() = static_cast<char>(std::tolower(static_cast<unsigned char>(name.front())));
    return name + accidental + mode;
}

} // namespace

std::string lengthWords(double lengthBeats, double beatsPerBar)
{
    const auto bars = beatsPerBar > 0.0 ? lengthBeats / beatsPerBar : 0.0;
    if (bars >= 1.0 - epsilon && std::abs(bars - std::round(bars)) < epsilon)
        return count(std::llround(bars), "une mesure", "mesures");

    const auto beats = std::llround(lengthBeats);
    if (beats >= 1 && std::abs(lengthBeats - static_cast<double>(beats)) < epsilon)
        return count(beats, "un temps", "temps");
    return "un fragment";
}

std::string phrase(const PhraseInput& input)
{
    const auto& constraints = input.constraints;
    const auto role = constraints.role.value;

    auto out = lengthWords(input.lengthBeats, input.beatsPerBar) + " " + std::string{roleWords(role)};

    // A rhythm channel plays its own pitch: a key would promise nothing.
    if (role != Role::rhythm)
    {
        out += " en " + spokenKey(constraints.key.value);
    }

    if (constraints.resolution.source == Source::imposed)
        out += ", " + std::string{resolutionWords(constraints.resolution.value)};
    if (constraints.reg.source == Source::imposed && role != Role::rhythm)
        out += ", " + std::string{registerWords(constraints.reg.value)};
    if (constraints.density.source == Source::imposed)
        out += ", " + std::string{densityWords(constraints.density.value)};
    if (constraints.form.source == Source::imposed)
        out += ", " + std::string{formWords(constraints.form.value)};
    if (input.learnedShare >= styleShareSpoken)
        out += ", dans ton style";
    return out;
}

} // namespace daw::domain::generation
