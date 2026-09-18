#include "daw/domain/command/CommandEnvelope.h"

namespace daw::domain
{

Value CommandEnvelope::toValue() const
{
    Value gestureValue{};
    if (gesture.has_value())
        gestureValue = Value{gesture->toString()};

    return Value::object({{"v", Value{currentVersion}},
                          {"id", Value{id.toString()}},
                          {"type", Value{type}},
                          {"at", Value{at.microsSinceEpoch}},
                          {"gesture", std::move(gestureValue)},
                          {"payload", payload}});
}

Result<CommandEnvelope> CommandEnvelope::fromValue(const Value& value)
{
    auto version = value.intAt("v");
    if (!version)
        return version.error();

    if (version.value() != currentVersion)
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

    return envelope;
}

} // namespace daw::domain
