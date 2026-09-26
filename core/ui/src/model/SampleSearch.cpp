#include "daw/ui/model/SampleSearch.h"

#include <algorithm>

namespace daw::ui
{
namespace
{

// How much each kind of meeting is worth. A family is worth less than a typo:
// "kcik" is certainly a kick, "club" is only probably what a person who typed
// "house" wants.
constexpr double sameWord = 1.0;
constexpr double prefixWord = 0.85;
constexpr double typoWord = 0.7;
constexpr double familyWord = 0.6;

// A word found in a folder rather than in the file name counts a little less:
// the folder speaks for every file in it, the name for one.
constexpr double folderWeight = 0.8;

// Under this length a prefix or a typo would match nearly anything: "h" is the
// start of "house" and of "hat".
constexpr std::size_t shortestFuzzy = 3;

// The families: words a sample pack uses for the same thing. By hand, and
// meant to grow: a word belongs to at most one family.
const std::vector<std::vector<std::string_view>>& families()
{
    static const std::vector<std::vector<std::string_view>> table{
        // Sounds.
        {"kick", "kik", "kck", "bd", "bassdrum"},
        {"snare", "snr", "sd", "rim", "rimshot"},
        {"hat", "hihat", "hh", "hats", "openhat", "closedhat", "oh", "ch"},
        {"clap", "clp", "cp", "snap"},
        {"perc", "percussion", "shaker", "tamb", "tambourine", "conga", "bongo"},
        {"808", "sub", "subbass"},
        {"bass", "bassline", "reese"},
        {"crash", "cymbal", "ride", "cym"},
        {"tom", "toms"},
        {"vox", "vocal", "vocals", "voice", "chant"},
        {"fx", "sfx", "riser", "impact", "sweep", "downlifter", "uplifter"},
        {"loop", "loops", "groove"},
        {"oneshot", "shot", "hit"},
        {"pad", "pads", "atmo", "ambient"},
        {"lead", "leads", "synth", "pluck"},
        {"chord", "chords", "keys", "piano"},
        // Styles.
        {"house", "club", "deep", "techhouse", "garage", "disco", "dance", "edm", "techno", "minimal"},
        {"trap", "drill", "rage", "phonk"},
        {"hiphop", "rap", "boombap", "lofi", "oldschool"},
        {"dnb", "jungle", "breakbeat", "breaks", "break", "neurofunk"},
        {"rnb", "soul", "neosoul"},
        {"afro", "afrobeat", "afrobeats", "amapiano", "afrohouse"},
    };
    return table;
}

[[nodiscard]] bool sameFamily(std::string_view lhs, std::string_view rhs)
{
    for (const auto& family : families())
    {
        const auto has = [&family](std::string_view word)
        { return std::find(family.begin(), family.end(), word) != family.end(); };
        if (has(lhs))
            return has(rhs);
    }
    return false;
}

// Damerau-Levenshtein on short words, with an early exit: only "is it one
// edit away" is ever asked.
[[nodiscard]] bool oneEditApart(std::string_view lhs, std::string_view rhs)
{
    if (lhs.size() > rhs.size())
        std::swap(lhs, rhs);
    if (rhs.size() - lhs.size() > 1)
        return false;

    if (lhs.size() == rhs.size())
    {
        std::size_t first = 0;
        while (first < lhs.size() && lhs[first] == rhs[first])
            ++first;
        if (first == lhs.size())
            return true;

        // One letter changed, or two neighbours swapped.
        if (lhs.substr(first + 1) == rhs.substr(first + 1))
            return true;
        return first + 1 < lhs.size() && lhs[first] == rhs[first + 1] && lhs[first + 1] == rhs[first] &&
               lhs.substr(first + 2) == rhs.substr(first + 2);
    }

    // One letter more in rhs.
    std::size_t first = 0;
    while (first < lhs.size() && lhs[first] == rhs[first])
        ++first;
    return lhs.substr(first) == rhs.substr(first + 1);
}

[[nodiscard]] bool isAsciiLetter(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

[[nodiscard]] bool isDigit(char c)
{
    return c >= '0' && c <= '9';
}

[[nodiscard]] double bestIn(std::string_view query, const std::vector<std::string>& words)
{
    double best = 0.0;
    for (const auto& word : words)
        best = std::max(best, wordMatch(query, word));
    return best;
}

} // namespace

std::vector<std::string> searchWords(std::string_view text)
{
    std::vector<std::string> words;
    std::string current;

    // 0 nothing yet, 1 letters, 2 digits, 3 anything else kept (UTF-8 bytes).
    int kind = 0;
    const auto flush = [&words, &current]
    {
        if (!current.empty())
            words.push_back(std::move(current));
        current.clear();
    };

    for (const auto c : text)
    {
        const auto byte = static_cast<unsigned char>(c);
        const auto next = isAsciiLetter(c) ? 1 : (isDigit(c) ? 2 : (byte >= 0x80 ? 3 : 0));
        if (next == 0)
        {
            flush();
            kind = 0;
            continue;
        }

        // A letter after a letter, or a non-ASCII byte next to a letter, stays
        // in the word: "Café" is one word. Letters and digits part.
        const auto joins = next == kind || (next == 3 && kind == 1) || (next == 1 && kind == 3);
        if (!joins)
            flush();
        kind = next == 3 ? 1 : next;
        current.push_back(isAsciiLetter(c) ? static_cast<char>(c | 0x20) : c);
    }
    flush();
    return words;
}

double wordMatch(std::string_view query, std::string_view word)
{
    if (query.empty() || word.empty())
        return 0.0;
    if (query == word)
        return sameWord;

    const auto shorter = std::min(query.size(), word.size());
    if (shorter >= shortestFuzzy && (word.starts_with(query) || query.starts_with(word)))
        return prefixWord;
    if (shorter >= shortestFuzzy + 1 && oneEditApart(query, word))
        return typoWord;
    if (sameFamily(query, word))
        return familyWord;
    return 0.0;
}

std::vector<SearchHit>
searchSamples(const std::vector<SearchEntry>& entries, std::string_view query, std::size_t limit)
{
    const auto wanted = searchWords(query);
    std::vector<SearchHit> hits;
    if (wanted.empty() || limit == 0)
        return hits;

    for (std::size_t index = 0; index < entries.size(); ++index)
    {
        const auto& entry = entries[index];
        double total = 0.0;
        for (const auto& word : wanted)
            total += std::max(bestIn(word, entry.nameWords), folderWeight * bestIn(word, entry.folderWords));

        if (total > 0.0)
            hits.push_back({index, total / static_cast<double>(wanted.size())});
    }

    std::sort(hits.begin(),
              hits.end(),
              [&entries](const SearchHit& lhs, const SearchHit& rhs)
              {
                  if (lhs.score != rhs.score)
                      return lhs.score > rhs.score;
                  const auto& left = entries[lhs.entry];
                  const auto& right = entries[rhs.entry];
                  if (left.nameWords.size() != right.nameWords.size())
                      return left.nameWords.size() < right.nameWords.size();
                  return left.path < right.path;
              });

    if (hits.size() > limit)
        hits.resize(limit);
    return hits;
}

} // namespace daw::ui
