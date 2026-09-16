#include "daw/ui/Tokens.h"

#include <DawTokensData.h>

namespace daw::ui
{

const Tokens& Tokens::builtIn()
{
    static const Tokens tokens =
        fromJson(juce::String::fromUTF8(DawTokensData::tokens_json, DawTokensData::tokens_jsonSize));
    return tokens;
}

Tokens Tokens::fromJson(const juce::String& json)
{
    auto root = juce::JSON::parse(json);
    jassert(root.isObject()); // tokens.json is malformed
    return Tokens{std::move(root)};
}

Tokens::Tokens(juce::var root)
    : root_(std::move(root))
{
}

juce::var Tokens::lookup(juce::StringRef path) const
{
    auto node = root_;
    for (const auto& key : juce::StringArray::fromTokens(juce::String(path), ".", ""))
    {
        if (!node.isObject())
            return {};
        node = node.getProperty(juce::Identifier(key), {});
    }
    return node;
}

bool Tokens::contains(juce::StringRef path) const
{
    return !lookup(path).isVoid();
}

juce::Colour Tokens::colour(juce::StringRef path) const
{
    const auto value = lookup(path).toString().trim();
    jassert(value.startsWithChar('#')); // unknown token or wrong type

    const auto hex = value.substring(1);
    if (hex.length() == 6)
        return juce::Colour::fromString("ff" + hex);
    if (hex.length() == 8)
        return juce::Colour::fromString(hex.substring(6) + hex.substring(0, 6));

    jassertfalse; // expected #RRGGBB or #RRGGBBAA
    return {};
}

float Tokens::number(juce::StringRef path) const
{
    const auto value = lookup(path);
    jassert(value.isDouble() || value.isInt() || value.isInt64()); // unknown token or wrong type
    return static_cast<float>(static_cast<double>(value));
}

int Tokens::integer(juce::StringRef path) const
{
    return juce::roundToInt(number(path));
}

} // namespace daw::ui
