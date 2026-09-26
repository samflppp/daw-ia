#pragma once

#include "daw/ui/PanelRegistry.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

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
//
// One click opens a folder, one click plays a sample. Above the tree, the
// search: typing replaces the tree with the samples that match best — see
// SampleSearch.h for what "best" means — and every result plays on a click
// and drags like the tree's. Escape, or an empty field, brings the tree back.
class BrowserPanel final : public juce::Component, private juce::ChangeListener
{
public:
    explicit BrowserPanel(const PanelContext& context);
    ~BrowserPanel() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // Builds the tree again from the folders the application holds.
    void refresh();

    // What the search shows, best first, and the row that shows one of them.
    // The verification types in the field and clicks a row.
    [[nodiscard]] const std::vector<juce::File>& results() const noexcept { return results_; }
    [[nodiscard]] juce::Component* resultRow(int row);

private:
    class FolderItem;
    class RootItem;
    class ResultList;

    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    void chooseFolder();
    void runSearch();

    const Tokens& tokens_;
    DawLookAndFeel& lookAndFeel_;
    SampleHost& samples_;

    juce::TreeView tree_;
    std::unique_ptr<RootItem> root_;

    juce::TextEditor search_;
    std::unique_ptr<ResultList> resultModel_;
    juce::ListBox resultList_;
    std::vector<juce::File> results_;
    std::vector<juce::String> resultFolders_;
    std::shared_ptr<const std::vector<SearchEntry>> searched_;
    bool wasSearching_{false};
    juce::TextButton addFolder_{"+ Dossier"};
    std::unique_ptr<juce::FileChooser> chooser_;

    bool titled_{false};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BrowserPanel)
};

} // namespace daw::ui
