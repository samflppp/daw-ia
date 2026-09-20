#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace daw::ui
{

// The workspaces a panel can offer to switch to.
//
// The switch lives in the transport, which is a panel, and a panel cannot know
// the layout host that would rebuild the screen. It asks instead, and the
// application decides — the same shape as a command going to the bus.
class WorkspaceHost
{
public:
    struct Entry
    {
        std::string id;
        std::string label;
    };

    WorkspaceHost() = default;
    virtual ~WorkspaceHost() = default;

    WorkspaceHost(const WorkspaceHost&) = delete;
    WorkspaceHost& operator=(const WorkspaceHost&) = delete;
    WorkspaceHost(WorkspaceHost&&) = delete;
    WorkspaceHost& operator=(WorkspaceHost&&) = delete;

    [[nodiscard]] virtual std::vector<Entry> available() const = 0;
    [[nodiscard]] virtual std::string current() const = 0;

    virtual void request(std::string_view id) = 0;
};

} // namespace daw::ui
