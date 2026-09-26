#include "daw/ui/panels/BrowserPanel.h"

#include <algorithm>

namespace daw::ui
{

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

    refresh();
}

BrowserPanel::~BrowserPanel()
{
    tree_.setRootItem(nullptr);
    setLookAndFeel(nullptr);
}

void BrowserPanel::refresh()
{
    tree_.setRootItem(nullptr);
    root_ = std::make_unique<RootItem>();

    for (const auto& folder : samples_.folders())
        root_->addSubItem(new FolderItem(*this, folder, true));

    tree_.setRootItem(root_.get());

    // Without a folder, the page says what to do instead of showing an empty
    // tree over the sentence.
    tree_.setVisible(!samples_.folders().empty());
    repaint();
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

    if (samples_.folders().empty())
    {
        auto area = getLocalBounds();
        area.removeFromTop(tokens_.integer("metric.panel.headerHeight"));
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

    tree_.setBounds(area);
}

} // namespace daw::ui
