#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace daw::ui
{

// The browser's search: "kick house" finds the kicks of a house pack first,
// then the kicks that are close — "Kick Club 01", "Kick Techno" — then what
// matches only half the query.
//
// A first approximation, local and deterministic: no model is called, nothing
// leaves the machine, the same query over the same folders always answers the
// same list. It reads names only, the file's and the folders' above it,
// because that is where a sample pack says what a sample is — "Drums/Kicks/
// Club 01.wav" is a kick even though its file name does not say so.
//
// A query word meets a name word in one of five ways, from best to worst:
//   the same word                        "kick"  / "kick"
//   one is the start of the other        "kick"  / "kicks", "hous" / "house"
//   one typo apart                       "kcik"  / "kick"
//   the same family, in a table          "house" / "club", "kick" / "bd"
//   nothing
// The family table is written by hand, below the header, and is the part that
// knows music: "house" and "club" share no letter, so no string distance can
// relate them. What a model would add later — "dark", "punchy", a sound
// rather than a name — is in IDEES.md, not here.

// Lowercase words of a name: split on anything that is not a letter or a
// digit, and between letters and digits ("Kick808" is "kick" and "808").
// Letters outside ASCII are kept as they are.
[[nodiscard]] std::vector<std::string> searchWords(std::string_view text);

// How well one query word matches one name word, from 0 (not at all) to 1.
[[nodiscard]] double wordMatch(std::string_view query, std::string_view word);

// One sample the search can find.
struct SearchEntry
{
    std::string path;                     // what is auditioned and dragged
    std::vector<std::string> nameWords;   // the file name, without extension
    std::vector<std::string> folderWords; // the folders between the root and the file
};

struct SearchHit
{
    std::size_t entry{0}; // index into the entries searched
    double score{0.0};    // 1 when every query word is found as is in the name
};

// The entries that match at least one query word, best first, at most
// `limit` of them. Ties go to the shorter name, then to the path, so the
// order never depends on the order the disk listed the files in.
[[nodiscard]] std::vector<SearchHit>
searchSamples(const std::vector<SearchEntry>& entries, std::string_view query, std::size_t limit);

} // namespace daw::ui
