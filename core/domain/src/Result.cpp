#include "daw/domain/Result.h"

namespace daw::domain
{

std::string_view describe(ErrorCode code) noexcept
{
    switch (code)
    {
    case ErrorCode::none:
        return "none";
    case ErrorCode::invalidArgument:
        return "invalidArgument";
    case ErrorCode::invalidPayload:
        return "invalidPayload";
    case ErrorCode::unknownCommandType:
        return "unknownCommandType";
    case ErrorCode::notFound:
        return "notFound";
    case ErrorCode::conflict:
        return "conflict";
    case ErrorCode::nothingToUndo:
        return "nothingToUndo";
    case ErrorCode::nothingToRedo:
        return "nothingToRedo";
    case ErrorCode::gestureClosed:
        return "gestureClosed";
    case ErrorCode::reentrantCall:
        return "reentrantCall";
    case ErrorCode::typeMismatch:
        return "typeMismatch";
    case ErrorCode::serialisationError:
        return "serialisationError";
    }
    return "unknown";
}

} // namespace daw::domain
