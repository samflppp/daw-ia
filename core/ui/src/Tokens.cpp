#include "daw/ui/Tokens.h"

#include <cstring>
#include <string_view>

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
    auto table = TokenTable::fromJson(json.toStdString());
    jassert(table.valid()); // tokens.json is malformed
    return Tokens{std::move(table)};
}

Tokens::Tokens(TokenTable table)
    : table_(std::move(table))
{
}

const TokenTable::Entry* Tokens::find(juce::StringRef path) const
{
    const auto* text = path.text.getAddress();
    return table_.find(std::string_view{text, std::strlen(text)});
}

bool Tokens::contains(juce::StringRef path) const
{
    return find(path) != nullptr;
}

juce::Colour Tokens::colour(juce::StringRef path) const
{
    const auto* entry = find(path);
    jassert(entry != nullptr && entry->kind != TokenTable::Entry::Kind::number &&
            entry->kind != TokenTable::Entry::Kind::group); // unknown token or wrong type
    jassert(entry == nullptr ||
            entry->kind == TokenTable::Entry::Kind::colour); // expected #RRGGBB or #RRGGBBAA

    if (entry == nullptr || entry->kind != TokenTable::Entry::Kind::colour)
        return {};
    return juce::Colour{static_cast<juce::uint32>(entry->argb)};
}

float Tokens::number(juce::StringRef path) const
{
    const auto* entry = find(path);
    jassert(entry != nullptr &&
            entry->kind == TokenTable::Entry::Kind::number); // unknown token or wrong type

    if (entry == nullptr || entry->kind != TokenTable::Entry::Kind::number)
        return 0.0f;
    return static_cast<float>(entry->number);
}

int Tokens::integer(juce::StringRef path) const
{
    return juce::roundToInt(number(path));
}

} // namespace daw::ui
