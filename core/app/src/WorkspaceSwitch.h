#pragma once

#include "daw/ui/WorkspaceView.h"
#include "daw/ui/Workspaces.h"
#include "daw/ui/model/WorkspaceHost.h"

#include <functional>
#include <string>

namespace daw::app
{

// Answers the transport panel when it asks to change workspace.
//
// The panel names an identifier and nothing else. Finding the manifest and
// rebuilding the screen is the application's business, exactly as executing a
// command is the bus's: in both cases the panel asks and does not act.
//
// A workspace reserved for workshops is neither offered nor shown, unless the
// application was launched for a workshop (--atelier): asking for it by its
// identifier is refused like asking for one that does not exist.
class WorkspaceSwitch final : public ui::WorkspaceHost
{
public:
    WorkspaceSwitch(const ui::Workspaces& workspaces, std::string current, bool workshop);

    // Whether this launch may show that manifest.
    [[nodiscard]] static bool offers(const ui::WorkspaceManifest& manifest, bool workshop) noexcept
    {
        return !manifest.workshop || workshop;
    }

    // Set once the view exists. Until then a request is remembered as the
    // current identifier and nothing is rebuilt.
    std::function<void(const ui::WorkspaceManifest&)> onShow;

    [[nodiscard]] std::vector<Entry> available() const override;
    [[nodiscard]] std::string current() const override;

    void request(std::string_view id) override;

private:
    const ui::Workspaces& workspaces_;
    std::string current_;
    bool workshop_;
};

} // namespace daw::app
