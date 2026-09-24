#pragma once

#include "daw/domain/command/CommandBus.h"
#include "daw/domain/project/ProjectState.h"
#include "daw/ui/DawLookAndFeel.h"
#include "daw/ui/Tokens.h"
#include "daw/ui/model/CopilotHost.h"
#include "daw/ui/model/History.h"
#include "daw/ui/model/PluginHost.h"
#include "daw/ui/model/ProjectObserver.h"
#include "daw/ui/model/SampleHost.h"
#include "daw/ui/model/Selection.h"
#include "daw/ui/model/TransportClock.h"
#include "daw/ui/model/WorkspaceHost.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace daw::ui
{

// What a panel is given when it is built.
//
// It is given no position, no size, and no neighbour: hygiene rule 2 is not a
// convention here, it is the shape of this structure. A panel that wanted to
// know where it sits would have to be handed something that is not in it.
struct PanelServices
{
    const Tokens& tokens;
    DawLookAndFeel& lookAndFeel;

    // The bus is the only way a panel changes anything. The state is const:
    // a panel reads the project, it never writes into it.
    domain::CommandBus& bus;
    const domain::ProjectState& state;

    // What changed, what is selected, where the playhead is, what has been
    // done, which plugins this machine holds, and which workspaces exist. None
    // of these is project state.
    ProjectObserver& project;
    Selection& selection;
    const TransportClock& clock;
    History& history;
    PluginHost& plugins;
    WorkspaceHost& workspaces;

    // The copilot, as a panel is allowed to see it: a state, a conversation,
    // and a way to ask. The process and the socket behind it stop at the
    // application.
    CopilotHost& copilot;

    // The samples: their bytes into the project, and the folders of this
    // machine the browser shows.
    SampleHost& samples;
};

// The services, plus the one thing that differs from one panel to the next.
struct PanelContext : PanelServices
{
    // The identifier the manifest used. A panel needs it to name itself, and
    // the placeholder needs it to say what is missing.
    std::string id;

    // True when the panel sits in a page window whose title bar already names
    // it. The panel then leaves its own caption out: one name per window, not
    // the same word twice, one above the other.
    bool titled{false};
};

// Maps a manifest identifier onto the component that answers for it.
//
// A workspace can name a panel this build does not implement, and that is not
// an error: the four manifests have existed since S1 and describe an interface
// that will be finished over several weeks. An unknown identifier yields a
// placeholder that says so, because a workspace that refuses to open teaches
// nothing, and a hole with no explanation teaches worse.
class PanelRegistry
{
public:
    using Factory = std::function<std::unique_ptr<juce::Component>(const PanelContext&)>;

    // The panels this build implements. The others fall back to the
    // placeholder, which is built here too.
    [[nodiscard]] static PanelRegistry withBuiltinPanels();

    void add(std::string id, Factory factory);

    [[nodiscard]] bool contains(std::string_view id) const noexcept;

    // Never null: an unknown identifier yields the placeholder.
    [[nodiscard]] std::unique_ptr<juce::Component> create(const PanelContext& context) const;

private:
    struct Entry
    {
        std::string id;
        Factory factory;
    };

    std::vector<Entry> entries_;
};

} // namespace daw::ui
