#include "daw/domain/command/Provenance.h"

#include <string>

namespace daw::domain
{

std::string_view describe(Actor actor) noexcept
{
    switch (actor)
    {
    case Actor::user:
        return "user";
    case Actor::copilot:
        return "copilot";
    case Actor::generator:
        return "generator";
    }
    return "user";
}

Result<Actor> parseActor(std::string_view text)
{
    if (text == "user")
        return Actor::user;
    if (text == "copilot")
        return Actor::copilot;
    if (text == "generator")
        return Actor::generator;

    return fail(ErrorCode::invalidPayload, "unknown actor: " + std::string{text});
}

Result<void> Provenance::validate() const
{
    if (!context.has_value())
        return {};

    if (context->isEmpty())
        return fail(ErrorCode::invalidArgument, "provenance context is present but empty");

    return context->validate();
}

Value Provenance::toValue() const
{
    Value contextValue{};
    if (context.has_value())
        contextValue = context->toValue();

    return Value::object({{"actor", Value{describe(actor)}}, {"context", std::move(contextValue)}});
}

Result<Provenance> Provenance::fromValue(const Value& value)
{
    auto actorText = value.stringAt("actor");
    if (!actorText)
        return actorText.error();

    auto actor = parseActor(actorText.value());
    if (!actor)
        return actor.error();

    Provenance origin{};
    origin.actor = actor.value();

    const auto* contextValue = value.find("context");
    if (contextValue != nullptr && !contextValue->isNull())
    {
        auto context = BlobRef::fromValue(*contextValue);
        if (!context)
            return fail(context.error().code, "context: " + context.error().message);

        origin.context = context.value();
    }

    auto valid = origin.validate();
    if (!valid)
        return valid.error();

    return origin;
}

bool operator==(const Provenance& lhs, const Provenance& rhs)
{
    return lhs.actor == rhs.actor && lhs.context == rhs.context;
}

} // namespace daw::domain
