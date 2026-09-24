#pragma once

#include "daw/ui/PanelRegistry.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace daw::ui
{

// The browser, the way FL keeps one on the left: the folders of this machine
// where the user's drumkits and sample packs live, as a tree, and every sample
// in them ready to be dragged onto the channel rack or the playlist.
//
// The folders are given once, with "+ Dossier", and remembered by the
// application — not by the project, which never holds a path to a drumkit.
// Dragging a sample hands over its path; the page it is dropped on imports
// its bytes into the project and says what to do with them.
class BrowserPanel final : public juce::Component
{
public:
    explicit BrowserPanel(const PanelContext& context);
    ~BrowserPanel() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // Builds the tree again from the folders the application holds.
    void refresh();

private:
    class FolderItem;
    class RootItem;

    void chooseFolder();

    const Tokens& tokens_;
    DawLookAndFeel& lookAndFeel_;
    SampleHost& samples_;

    juce::TreeView tree_;
    std::unique_ptr<RootItem> root_;
    juce::TextButton addFolder_{"+ Dossier"};
    std::unique_ptr<juce::FileChooser> chooser_;

    bool titled_{false};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BrowserPanel)
};

} // namespace daw::ui
