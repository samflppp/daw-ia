#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace daw::ui
{

// tokens.json, read once and flattened: every path, "color.surface.base" or
// "space.md", to its value already decoded.
//
// Tokens used to walk the parsed JSON on every call: the path cut at its dots,
// each piece made into an identifier (a global pool behind a lock), the tree
// walked, the hexadecimal decoded. Some seven hundred calls do that, many of
// them inside the loops of a paint. Here it is done once, when the tokens are
// loaded, and a call is one hash of the path.
//
// A path to an object ("color.surface") is kept too, as a group: contains()
// answered true for it before, and still does.
//
// Pure C++, no JUCE: what a test pins down is that each token gives the value
// it gave before.
class TokenTable
{
public:
    struct Entry
    {
        enum class Kind
        {
            colour,
            number,
            text,   // a string that is not a colour: a font family
            group,  // an object: a path that goes on
            invalid // "#" followed by neither six nor eight digits
        };

        Kind kind{Kind::group};

        // Colours as JUCE packs them: alpha in the top byte, then red, green,
        // blue. "#RRGGBB" is opaque; "#RRGGBBAA" carries its alpha last.
        std::uint32_t argb{0};
        double number{0.0};
    };

    // Empty, and valid() false, when the text is not a JSON object.
    [[nodiscard]] static TokenTable fromJson(std::string_view json);

    [[nodiscard]] bool valid() const noexcept { return valid_; }

    // Null for a path tokens.json does not have.
    [[nodiscard]] const Entry* find(std::string_view path) const;

    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }

    // Decodes "#RRGGBB" or "#RRGGBBAA", surrounding blanks ignored. Invalid
    // otherwise.
    [[nodiscard]] static Entry colourOf(std::string_view text);

private:
    // Looked up by the text of the path as it is, without building a string.
    struct Hash
    {
        using is_transparent = void;
        std::size_t operator()(std::string_view text) const noexcept
        {
            return std::hash<std::string_view>{}(text);
        }
    };

    std::unordered_map<std::string, Entry, Hash, std::equal_to<>> entries_;
    bool valid_{false};
};

} // namespace daw::ui
