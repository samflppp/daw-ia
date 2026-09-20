#pragma once

#include "daw/domain/Result.h"
#include "daw/ui/workspace/WorkspaceManifest.h"

#include <juce_core/juce_core.h>

#include <string_view>
#include <vector>

namespace daw::ui
{

// The manifests this build carries, read once from the binary.
//
// All four are parsed at startup, not only the one that is shown: a manifest
// that is broken has to be known before a user asks for it, and the workspace
// switch has to be able to promise that the next screen will open.
class Workspaces
{
public:
    [[nodiscard]] static const Workspaces& builtIn();

    [[nodiscard]] const std::vector<WorkspaceManifest>& all() const noexcept { return manifests_; }

    // The workspace a launch without arguments opens.
    [[nodiscard]] static std::string_view defaultId() noexcept { return "beatmaker"; }

    [[nodiscard]] const WorkspaceManifest* find(std::string_view id) const noexcept;

    // Manifests the binary carries and this build could not read, with the
    // reason. Reported, never swallowed.
    [[nodiscard]] const std::vector<juce::String>& rejected() const noexcept { return rejected_; }

private:
    Workspaces();

    std::vector<WorkspaceManifest> manifests_;
    std::vector<juce::String> rejected_;
};

} // namespace daw::ui
