#include "daw/domain/Timestamp.h"

#include <chrono>

namespace daw::domain
{

Timestamp Timestamp::now() noexcept
{
    const auto since = std::chrono::system_clock::now().time_since_epoch();
    return Timestamp{std::chrono::duration_cast<std::chrono::microseconds>(since).count()};
}

} // namespace daw::domain
