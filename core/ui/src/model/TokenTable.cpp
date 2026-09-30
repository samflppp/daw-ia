#include "daw/ui/model/TokenTable.h"

#include "daw/domain/Value.h"
#include "daw/domain/serialization/Json.h"

#include <optional>
#include <utility>

namespace daw::ui
{
namespace
{

[[nodiscard]] std::string_view trimmed(std::string_view text)
{
    constexpr std::string_view blanks = " \t\r\n";
    const auto first = text.find_first_not_of(blanks);
    if (first == std::string_view::npos)
        return {};
    const auto last = text.find_last_not_of(blanks);
    return text.substr(first, last - first + 1);
}

[[nodiscard]] std::optional<std::uint32_t> hexOf(std::string_view digits)
{
    std::uint32_t value = 0;
    for (const auto c : digits)
    {
        std::uint32_t digit = 0;
        if (c >= '0' && c <= '9')
            digit = static_cast<std::uint32_t>(c - '0');
        else if (c >= 'a' && c <= 'f')
            digit = static_cast<std::uint32_t>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F')
            digit = static_cast<std::uint32_t>(c - 'A' + 10);
        else
            return std::nullopt;
        value = (value << 4U) | digit;
    }
    return value;
}

} // namespace

TokenTable::Entry TokenTable::colourOf(std::string_view text)
{
    Entry entry{};
    entry.kind = Entry::Kind::invalid;

    const auto value = trimmed(text);
    if (value.empty() || value.front() != '#')
        return entry;

    const auto digits = value.substr(1);
    const auto hex = hexOf(digits);
    if (!hex.has_value())
        return entry;

    if (digits.size() == 6)
    {
        entry.kind = Entry::Kind::colour;
        entry.argb = 0xff000000U | *hex;
    }
    else if (digits.size() == 8)
    {
        // RRGGBBAA to AARRGGBB.
        entry.kind = Entry::Kind::colour;
        entry.argb = (*hex << 24U) | (*hex >> 8U);
    }
    return entry;
}

TokenTable TokenTable::fromJson(std::string_view json)
{
    TokenTable table;
    const auto root = domain::json::read(json);
    if (!root.ok() || !root.value().isObject())
        return table;

    // Depth first, each member under its parent's path.
    const auto flatten =
        [&table](const auto& self, const domain::Value& node, const std::string& path) -> void
    {
        Entry entry{};
        if (const auto* members = node.asObject(); members != nullptr)
        {
            entry.kind = Entry::Kind::group;
            for (const auto& [key, child] : *members)
                self(self, child, path.empty() ? key : path + "." + key);
        }
        else if (node.isInt() || node.isNumber())
        {
            entry.kind = Entry::Kind::number;
            entry.number = node.asDouble().value();
        }
        else if (node.isString())
        {
            const auto text = node.asString().value();
            entry = trimmed(text).starts_with('#') ? colourOf(text) : Entry{Entry::Kind::text, 0, 0.0};
        }
        else
        {
            entry.kind = Entry::Kind::text;
        }

        if (!path.empty())
            table.entries_.insert_or_assign(path, entry);
    };

    flatten(flatten, root.value(), {});
    table.valid_ = true;
    return table;
}

const TokenTable::Entry* TokenTable::find(std::string_view path) const
{
    const auto found = entries_.find(path);
    return found != entries_.end() ? &found->second : nullptr;
}

} // namespace daw::ui
