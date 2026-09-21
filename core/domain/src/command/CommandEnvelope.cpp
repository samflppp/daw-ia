#include "daw/domain/command/CommandEnvelope.h"

namespace daw::domain
{

Value CommandEnvelope::toValue() const
{
    Value gestureValue{};
    if (gesture.has_value())
        gestureValue = Value{gesture->toString()};

    Value groupValue{};
    if (group.has_value())
        groupValue = group->toValue();

    return Value::object({{"v", Value{currentVersion}},
                          {"id", Value{id.toString()}},
                          {"type", Value{type}},
                          {"at", Value{at.microsSinceEpoch}},
                          {"gesture", std::move(gestureValue)},
                          {"group", std::move(groupValue)},
                          {"origin", origin.toValue()},
                          {"payload", payload}});
}

Result<CommandEnvelope> CommandEnvelope::fromValue(const Value& value)
{
    auto version = value.intAt("v");
    if (!version)
        return version.error();

    if (version.value() < oldestReadableVersion || version.value() > currentVersion)
        return fail(ErrorCode::invalidPayload,
                    "unsupported envelope version: " + std::to_string(version.value()));

    auto idText = value.stringAt("id");
    if (!idText)
        return idText.error();

    auto id = CommandId::parse(idText.value());
    if (!id)
        return id.error();

    auto type = value.stringAt("type");
    if (!type)
        return type.error();

    if (type.value().empty())
        return fail(ErrorCode::invalidPayload, "empty command type");

    auto at = value.intAt("at");
    if (!at)
        return at.error();

    const auto* payloadValue = value.find("payload");
    if (payloadValue == nullptr)
        return fail(ErrorCode::invalidPayload, "missing key payload");

    CommandEnvelope envelope{};
    envelope.id = id.value();
    envelope.type = type.value();
    envelope.at = Timestamp{at.value()};
    envelope.payload = *payloadValue;

    // A v1 envelope predates provenance: those commands came from the user,
    // and saying so is the whole point of a default. A v2 envelope without an
    // origin is malformed, not old, and is refused.
    if (version.value() >= 2)
    {
        const auto* originValue = value.find("origin");
        if (originValue == nullptr)
            return fail(ErrorCode::invalidPayload, "missing key origin");

        auto origin = Provenance::fromValue(*originValue);
        if (!origin)
            return fail(origin.error().code, "origin: " + origin.error().message);

        envelope.origin = origin.value();
    }

    const auto* gestureValue = value.find("gesture");
    if (gestureValue != nullptr && !gestureValue->isNull())
    {
        auto gestureText = gestureValue->asString();
        if (!gestureText)
            return fail(gestureText.error().code, "gesture: " + gestureText.error().message);

        auto gestureId = GestureId::parse(gestureText.value());
        if (!gestureId)
            return fail(gestureId.error().code, "gesture: " + gestureId.error().message);

        envelope.gesture = gestureId.value();
    }

    // A v1 or v2 envelope carries no group, and that is not a hole to fill:
    // those commands each were their own history entry, which is exactly what
    // an absent group means.
    const auto* groupValue = value.find("group");
    if (groupValue != nullptr && !groupValue->isNull())
    {
        auto group = GroupRef::fromValue(*groupValue);
        if (!group)
            return fail(group.error().code, "group: " + group.error().message);

        envelope.group = std::move(group).value();
    }

    return envelope;
}

} // namespace daw::domain
