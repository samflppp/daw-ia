#pragma once

#include "daw/ui/DawLookAndFeel.h"
#include "daw/ui/Tokens.h"

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
struct PanelContext
{
    const Tokens& tokens;
    DawLookAndFeel& lookAndFeel;

    // The identifier the manifest used. A panel needs it to name itself, and
    // the placeholder needs it to say what is missing.
    std::string id;
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
