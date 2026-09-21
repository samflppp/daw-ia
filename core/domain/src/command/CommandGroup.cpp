#include "daw/domain/command/CommandGroup.h"

namespace daw::domain
{

Result<void> GroupRef::validate() const
{
    if (id.isNil())
        return fail(ErrorCode::invalidArgument, "a group needs an identifier");

    if (label.empty())
        return fail(ErrorCode::invalidArgument, "a group needs a label");

    // A label is read in a panel, not stored for its own sake: one that ran
    // for a whole paragraph would push every other entry off the screen.
    if (label.size() > maxLabelLength)
        return fail(ErrorCode::invalidArgument, "the group label is too long");

    return {};
}

Value GroupRef::toValue() const
{
    return Value::object({{"id", Value{id.toString()}}, {"label", Value{label}}});
}

Result<GroupRef> GroupRef::fromValue(const Value& value)
{
    auto idText = value.stringAt("id");
    if (!idText)
        return idText.error();

    auto id = GroupId::parse(idText.value());
    if (!id)
        return id.error();

    auto label = value.stringAt("label");
    if (!label)
        return label.error();

    GroupRef group{};
    group.id = id.value();
    group.label = std::move(label).value();

    if (auto valid = group.validate(); !valid)
        return valid.error();

    return group;
}

bool operator==(const GroupRef& lhs, const GroupRef& rhs)
{
    return lhs.id == rhs.id && lhs.label == rhs.label;
}

} // namespace daw::domain
