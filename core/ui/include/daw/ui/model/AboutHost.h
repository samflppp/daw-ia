#pragma once

#include <string>

namespace daw::ui
{

// What the « À propos » page shows (S26): which build this is, and what the
// licences of what it carries ask to be said. Where the build and the
// licences file come from stops at the application.
class AboutHost
{
public:
    AboutHost() = default;
    virtual ~AboutHost() = default;
    AboutHost(const AboutHost&) = delete;
    AboutHost& operator=(const AboutHost&) = delete;
    AboutHost(AboutHost&&) = delete;
    AboutHost& operator=(AboutHost&&) = delete;

    // "0.1.0-alpha".
    [[nodiscard]] virtual std::string version() const = 0;
    // The short commit, with "+ modifications" when the build was not that
    // commit as it stands.
    [[nodiscard]] virtual std::string commit() const = 0;
    // The mentions the licences require on screen, one per line.
    [[nodiscard]] virtual std::string mentions() const = 0;
    // The whole text of the third-party licences, or why it is missing.
    [[nodiscard]] virtual std::string licences() const = 0;
};

} // namespace daw::ui
