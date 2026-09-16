#include "daw/domain/BuildInfo.h"

namespace daw::domain
{

std::string_view versionString() noexcept
{
    return DAW_VERSION_STRING;
}

} // namespace daw::domain
