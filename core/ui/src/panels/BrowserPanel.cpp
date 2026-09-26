#include "daw/ui/panels/BrowserPanel.h"

#include <algorithm>

namespace daw::ui
{
namespace
{

// More than a person reads, few enough to sort and draw at every key.
constexpr std::size_t mostResults = 200;

} // namespace

// The results of a search: the sample's name, and under it the folder it is
// in, since "01.wav" says nothing until "Club Pack / Kicks" does.
class BrowserPanel::ResultList final : public juce::ListBoxModel
{
public:
    explicit ResultList(BrowserPanel& owner)
        : owner_(owner)
    {
    }

    int getNumRows() override { return static_cast<int>(owner_.results_.size()); }

    void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected) override
    {
        if (row < 0 || row >= getNumRows())
            return;

        const auto& tokens = owner_.tokens_;
        if (selected)
        {
            g.setColour(tokens.colour("color.state.selected"));
            g.fillRect(0, 0, width, height);
        }

        auto area = juce::Rectangle<int>{0, 0, width, height}.reduced(tokens.integer("space.sm"), 0);
        auto top = area.removeFromTop(height / 2);

        const auto& file = owner_.results_[static_cast<std::size_t>(row)];
        g.setColour(tokens.colour("color.text.primary"));
        g.setFont(owner_.lookAndFeel_.typography().sans("font.size.caption", "font.weight.regular"));
        g.drawText(file.getFileName(), top, juce::Justification::bottomLeft, true);

        g.setColour(tokens.colour("color.text.tertiary"));
        g.setFont(owner_.lookAndFeel_.typography().sans("font.size.micro", "font.weight.regular"));
        g.drawText(
            owner_.resultFolders_[static_cast<std::size_t>(row)], area, juce::Justification::topLeft, true);
    }

    void listBoxItemClicked(int row, const juce::MouseEvent& event) override
    {
        if (row >= 0 && row < getNumRows() && !event.mods.isRightButtonDown())
            owner_.samples_.audition(owner_.results_[static_cast<std::size_t>(row)]);
    }

    // Dragged exactly as from the tree: the page it lands on imports it.
    juce::var getDragSourceDescription(const juce::SparseSet<int>& rows) override
    {
        if (rows.isEmpty() || rows[0] < 0 || rows[0] >= getNumRows())
            return {};
        return juce::var{"sample:" + owner_.results_[static_cast<std::size_t>(rows[0])].getFullPathName()};
    }

private:
    BrowserPanel& owner_;
};

// A folder or a sample of the tree. Folders list their children the first time
// they are opened, not before: a sample pack of ten thousand files costs
// nothing until someone looks inside it.
class BrowserPanel::FolderItem final : public juce::TreeViewItem
{
public:
    FolderItem(BrowserPanel& owner, juce::File file, bool isRoot)
        : owner_(owner)
        , file_(std::move(file))
        , isRoot_(isRoot)
    {
    }

    bool mightContainSubItems() override { return file_.isDirectory(); }

    juce::String getUniqueName() const override { return file_.getFullPathName(); }

    void itemOpennessChanged(bool isNowOpen) override
    {
        if (!isNowOpen || getNumSubItems() > 0)
            return;

        auto children =
            file_.findChildFiles(juce::File::findFilesAndDirectories | juce::File::ignoreHiddenFiles, false);

        // Folders first, then samples, each by name: the order a sample pack is
        // read in.
        std::sort(children.begin(),
                  children.end(),
                  [](const juce::File& lhs, const juce::File& rhs)
                  {
                      if (lhs.isDirectory() != rhs.isDirectory())
                          return lhs.isDirectory();
                      return lhs.getFileName().compareNatural(rhs.getFileName()) < 0;
                  });

        for (const auto& child : children)
        {
            if (child.isDirectory() || SampleHost::isSampleFile(child))
                addSubItem(new FolderItem(owner_, child, false));
        }
    }

    void paintItem(juce::Graphics& g, int width, int height) override
    {
        const auto& tokens = owner_.tokens_;

        if (isSelected())
        {
            g.setColour(tokens.colour("color.state.selected"));
            g.fillRect(0, 0, width, height);
        }

        const auto folder = file_.isDirectory();
        g.setColour(tokens.colour(folder ? "color.text.primary" : "color.text.secondary"));
        g.setFont(folder ? owner_.lookAndFeel_.typography().sans("font.size.caption", "font.weight.medium")
                         : owner_.lookAndFeel_.typography().sans("font.size.caption", "font.weight.regular"));

        const auto name =
            isRoot_ ? file_.getFileName() + "  (" + file_.getParentDirectory().getFullPathName() + ")"
                    : file_.getFileName();
        g.drawText(name, 0, 0, width, height, juce::Justification::centredLeft, true);
    }

    // The click already opened or closed the folder: a double click would
    // undo it with its second half.
    void itemDoubleClicked(const juce::MouseEvent&) override {}

    // A sample is dragged by its path. The page it lands on imports it.
    juce::var getDragSourceDescription() override
    {
        return file_.isDirectory() ? juce::var{} : juce::var{"sample:" + file_.getFullPathName()};
    }

    void itemClicked(const juce::MouseEvent& event) override
    {
        // A click on a sample plays it, the way FL's browser does: the kick is
        // chosen by ear before it is dropped. Nothing enters the project.
        if (!file_.isDirectory() && !event.mods.isRightButtonDown())
        {
            owner_.samples_.audition(file_);
            return;
        }

        // A click on a folder opens it, or closes it: one click, as in FL. The
        // double click JUCE asks for by default is one gesture too many for a
        // tree a beatmaker goes down ten times a minute.
        if (file_.isDirectory() && !event.mods.isRightButtonDown())
        {
            setOpen(!isOpen());
            return;
        }

        if (!isRoot_ || !event.mods.isRightButtonDown())
            return;

        juce::PopupMenu menu;
        menu.addItem(1, u8"Retirer ce dossier du navigateur");

        auto* owner = &owner_;
        const auto folder = file_;
        menu.showMenuAsync(juce::PopupMenu::Options{},
                           [owner, folder](int chosen)
                           {
                               if (chosen != 1)
                                   return;
                               owner->samples_.removeFolder(folder);
                               owner->refresh();
                           });
    }

private:
    BrowserPanel& owner_;
    juce::File file_;
    bool isRoot_{false};
};

// The invisible root: one child per folder the user gave.
class BrowserPanel::RootItem final : public juce::TreeViewItem
{
public:
    bool mightContainSubItems() override { return true; }
    void paintItem(juce::Graphics&, int, int) override {}
};

BrowserPanel::BrowserPanel(const PanelContext& context)
    : tokens_(context.tokens)
    , lookAndFeel_(context.lookAndFeel)
    , samples_(context.samples)
{
    titled_ = context.titled;
    setLookAndFeel(&lookAndFeel_);

    tree_.setRootItemVisible(false);
    tree_.setDefaultOpenness(false);
    tree_.setIndentSize(tokens_.integer("space.lg"));
    addAndMakeVisible(tree_);

    addFolder_.setWantsKeyboardFocus(false);
    addFolder_.onClick = [this] { chooseFolder(); };
    addAndMakeVisible(addFolder_);

    search_.setMultiLine(false);
    search_.setReturnKeyStartsNewLine(false);
    search_.setTextToShowWhenEmpty(u8"Rechercher : kick house, snare trap…",
                                   tokens_.colour("color.text.disabled"));
    search_.onTextChange = [this] { runSearch(); };
    search_.onEscapeKey = [this]
    {
        search_.clear();
        runSearch();
    };
    addAndMakeVisible(search_);

    resultModel_ = std::make_unique<ResultList>(*this);
    resultList_.setModel(resultModel_.get());
    resultList_.setRowHeight(tokens_.integer("metric.browser.resultHeight"));
    resultList_.setColour(juce::ListBox::backgroundColourId, tokens_.colour("color.surface.panel"));
    addChildComponent(resultList_);

    samples_.addChangeListener(this);
    refresh();
}

BrowserPanel::~BrowserPanel()
{
    samples_.removeChangeListener(this);
    resultList_.setModel(nullptr);
    tree_.setRootItem(nullptr);
    setLookAndFeel(nullptr);
}

void BrowserPanel::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    juce::ignoreUnused(source);

    // A listing came back: the search reads the new one. A waveform finished
    // measuring broadcasts too, and changes nothing here.
    if (samples_.searchIndex() != searched_)
        runSearch();
}

void BrowserPanel::runSearch()
{
    const auto query = search_.getText().trim();
    const auto searching = query.isNotEmpty();

    // A search that starts lists the folders again: files copied into a pack
    // since the last listing are found once it comes back.
    if (searching && !wasSearching_)
        samples_.indexSamples();
    wasSearching_ = searching;

    results_.clear();
    resultFolders_.clear();
    searched_ = samples_.searchIndex();

    if (searching && searched_ != nullptr)
    {
        for (const auto& hit : searchSamples(*searched_, query.toStdString(), mostResults))
        {
            const juce::File file{juce::String::fromUTF8((*searched_)[hit.entry].path.c_str())};
            results_.push_back(file);

            // The folder under the root the user gave, the way a person
            // reads it: "drumkit / Kicks".
            auto folder = file.getParentDirectory().getFullPathName();
            for (const auto& root : samples_.folders())
            {
                if (file.isAChildOf(root))
                    folder = file.getParentDirectory().getRelativePathFrom(root.getParentDirectory());
            }
            resultFolders_.push_back(folder.replace(juce::File::getSeparatorString(), " / "));
        }
    }

    // Shown before it is filled: a hidden list builds no rows, and the first
    // results would wait for the next change to be clickable.
    resultList_.setVisible(searching);
    tree_.setVisible(!searching && !samples_.folders().empty());
    resultList_.updateContent();
    resultList_.deselectAllRows();
    resultList_.repaint();
    repaint();
}

juce::Component* BrowserPanel::resultRow(int row)
{
    // The row as the mouse finds it: getComponentForRowNumber() only answers
    // for a row drawn by a custom component, and these are painted.
    if (row < 0 || row >= static_cast<int>(results_.size()))
        return nullptr;
    return resultList_.getComponentAt(resultList_.getRowPosition(row, true).getCentre());
}

void BrowserPanel::refresh()
{
    tree_.setRootItem(nullptr);
    root_ = std::make_unique<RootItem>();

    for (const auto& folder : samples_.folders())
        root_->addSubItem(new FolderItem(*this, folder, true));

    tree_.setRootItem(root_.get());

    // Without a folder, the page says what to do instead of showing an empty
    // tree over the sentence. The folders changed: the listing starts again,
    // and the search follows it when it comes back.
    runSearch();
}

void BrowserPanel::chooseFolder()
{
    chooser_ = std::make_unique<juce::FileChooser>(
        u8"Choisir un dossier de samples", juce::File::getSpecialLocation(juce::File::userMusicDirectory));

    chooser_->launchAsync(juce::FileBrowserComponent::openMode |
                              juce::FileBrowserComponent::canSelectDirectories,
                          [this](const juce::FileChooser& chooser)
                          {
                              const auto folder = chooser.getResult();
                              if (folder.isDirectory())
                              {
                                  samples_.addFolder(folder);
                                  refresh();
                              }
                          });
}

void BrowserPanel::paint(juce::Graphics& g)
{
    g.fillAll(tokens_.colour("color.surface.panel"));

    auto header = getLocalBounds().removeFromTop(tokens_.integer("metric.panel.headerHeight"));
    g.setColour(tokens_.colour("color.border.hairline"));
    g.fillRect(header.removeFromBottom(tokens_.integer("stroke.hairline")));

    if (!titled_)
    {
        header.removeFromLeft(tokens_.integer("space.md"));
        g.setColour(tokens_.colour("color.text.tertiary"));
        g.setFont(lookAndFeel_.typography().caps("font.size.micro"));
        g.drawText("NAVIGATEUR", header, juce::Justification::centredLeft, false);
    }

    if (samples_.folders().empty() && search_.isEmpty())
    {
        auto area = getLocalBounds();
        area.removeFromTop(tokens_.integer("metric.panel.headerHeight") +
                           tokens_.integer("metric.browser.searchHeight"));
        g.setColour(tokens_.colour("color.text.disabled"));
        g.setFont(lookAndFeel_.typography().sans("font.size.caption", "font.weight.regular"));
        g.drawFittedText(u8"« + Dossier » pour donner accès à un drumkit ou un sample pack",
                         area.reduced(tokens_.integer("space.md")),
                         juce::Justification::centred,
                         3);
    }
}

void BrowserPanel::resized()
{
    auto area = getLocalBounds();
    auto header = area.removeFromTop(tokens_.integer("metric.panel.headerHeight"));
    header = header.reduced(tokens_.integer("space.sm"), tokens_.integer("space.xs"));
    addFolder_.setBounds(header.removeFromRight(tokens_.integer("metric.page.tabWidth") * 3 / 4));

    search_.setBounds(area.removeFromTop(tokens_.integer("metric.browser.searchHeight"))
                          .reduced(tokens_.integer("space.sm"), tokens_.integer("space.xs")));
    tree_.setBounds(area);
    resultList_.setBounds(area);
}

} // namespace daw::ui
