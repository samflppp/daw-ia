#pragma once

#include <string_view>

namespace daw::domain
{

// Version of the core module, set by CMake from project(VERSION).
[[nodiscard]] std::string_view versionString() noexcept;

} // namespace daw::domain
