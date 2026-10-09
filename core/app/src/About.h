#pragma once

#include "daw/ui/model/AboutHost.h"

#include <juce_core/juce_core.h>

#include <string>

namespace daw::app
{

// The « À propos » page's source (S26): the version, the commit read at the
// build (cmake/DawBuildInfo.cmake), the mentions the licences require, and
// the third-party licences.
//
// The licences are the file the installer puts beside the executable,
// licences.txt; run from the repository, docs/licences-tierces.md.
class About final : public ui::AboutHost
{
public:
    About() = default;

    [[nodiscard]] std::string version() const override;
    [[nodiscard]] std::string commit() const override;
    [[nodiscard]] std::string mentions() const override;
    [[nodiscard]] std::string licences() const override;

    // "DAW IA 0.1.0-alpha (99b1407)": the first line of daw.log.
    [[nodiscard]] static juce::String banner();

    // Where the licences are read from; empty when neither file is there.
    [[nodiscard]] static juce::File licencesFile();
};

} // namespace daw::app
