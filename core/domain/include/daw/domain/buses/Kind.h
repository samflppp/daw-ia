#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace daw::domain::buses
{

// What a plugin of the person's is, for the smart buses — decided with the
// founder on 8 October 2026: by its name, with a suggestion to accept when it
// is hard to tell.
//
//   1. the person's answer, when there is one: it wins over everything, a
//      correction of the catalogue included;
//   2. the catalogue: « Reverb », « Delay » (or « Echo »), an instrument, or
//      any other kind it names (« Dynamics », « Modulation »…) — believed;
//   3. a word of the name that cannot mean anything else: « reverb »,
//      « verb », « delay », « echo » — recognised, and said;
//   4. anything else is asked: a word that often means a reverb or a delay
//      but not always (« room », « plate », « tap »…) gives a suggestion; a
//      name with no such word (« Supermassive ») asks without one.
//
// The name is read word by word, a capital starting a word: « ValhallaPlate »
// is « valhalla » and « plate », and « Valhalla » never reads as « hall ».

enum class Kind
{
    reverb,
    delay,
    instrument,
    other
};

enum class KnownBy
{
    answer,
    catalogue,
    name,
    unknown // to ask
};

struct Recognition
{
    std::optional<Kind> kind; // none: to ask
    KnownBy by{KnownBy::unknown};
    std::optional<Kind> suggested; // when asked: what the name suggests
};

[[nodiscard]] Recognition
recognise(std::string_view name, std::string_view category, bool instrument, std::optional<Kind> answer);

// « reverb », « delay », « instrument », « other », as an answer is kept.
[[nodiscard]] std::string toString(Kind kind);
[[nodiscard]] std::optional<Kind> kindFromString(std::string_view text);

} // namespace daw::domain::buses
