#include "daw/ui/panels/PluginChainPanel.h"

#include "daw/domain/commands/PluginCommands.h"

#include <algorithm>

namespace daw::ui
{

// One plugin. It knows its identifier and its rank, and nothing about the
// slots around it.
class PluginChainPanel::Slot final : public juce::Component
{
public:
    Slot(const Tokens& tokens,
         DawLookAndFeel& lookAndFeel,
         domain::CommandBus& bus,
         const domain::ProjectState& state,
         PluginHost& plugins,
         domain::TrackId trackId,
         domain::PluginId pluginId,
         int position)
        : tokens_(tokens)
        , lookAndFeel_(lookAndFeel)
        , bus_(bus)
        , state_(state)
        , plugins_(plugins)
        , trackId_(trackId)
        , pluginId_(pluginId)
        , position_(position)
    {
        setLookAndFeel(&lookAndFeel_);

        addAndMakeVisible(bypass_);
        bypass_.setButtonText("B");
        bypass_.setMouseClickGrabsKeyboardFocus(false);
        bypass_.onClick = [this]
        {
            const auto* plugin = instance();
            if (plugin == nullptr)
                return;
            static_cast<void>(
                bus_.execute(std::make_unique<domain::SetPluginBypassed>(pluginId_, !plugin->bypassed)));
        };

        addAndMakeVisible(remove_);
        remove_.setButtonText("x");
        remove_.setMouseClickGrabsKeyboardFocus(false);
        remove_.onClick = [this]
        {
            // The editor goes before the plugin does: a window drawing into a
            // plugin the projection is about to destroy is the one crash this
            // panel can cause.
            plugins_.closeEditor(pluginId_);
            static_cast<void>(bus_.execute(std::make_unique<domain::RemovePlugin>(pluginId_)));
        };

        refresh();
    }

    ~Slot() override { setLookAndFeel(nullptr); }

    [[nodiscard]] domain::PluginId pluginId() const noexcept { return pluginId_; }

    void refresh()
    {
        const auto* plugin = instance();
        if (plugin == nullptr)
            return;

        name_ = juce::String(plugin->ref.name.empty() ? plugin->ref.identifier : plugin->ref.name);
        format_ = juce::String(plugin->ref.format);
        installed_ = plugins_.isInstalled(plugin->ref);
        open_ = plugins_.editorIsOpen(pluginId_);

        bypass_.setToggleState(plugin->bypassed, juce::dontSendNotification);
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        const auto* plugin = instance();
        if (plugin == nullptr)
            return;

        if (open_)
            g.fillAll(tokens_.colour("color.state.selected"));
        else if (isMouseOver(true))
            g.fillAll(tokens_.colour("color.state.hover"));

        auto area = getLocalBounds().reduced(tokens_.integer("space.md"), 0);

        g.setColour(tokens_.colour("color.border.hairline"));
        g.fillRect(getLocalBounds().removeFromBottom(tokens_.integer("stroke.hairline")));

        auto index = area.removeFromLeft(tokens_.integer("space.lg"));
        g.setColour(tokens_.colour("color.text.disabled"));
        g.setFont(lookAndFeel_.typography().mono("font.size.micro", "font.weight.regular"));
        g.drawText(juce::String(position_ + 1), index, juce::Justification::centredLeft, false);

        area.removeFromRight(tokens_.integer("metric.track.chipWidth") * 2 + tokens_.integer("space.sm") * 2);

        auto format = area.removeFromRight(tokens_.integer("space.xl") + tokens_.integer("space.md"));
        g.setColour(tokens_.colour("color.text.disabled"));
        g.setFont(lookAndFeel_.typography().caps("font.size.micro"));
        g.drawText(format_, format, juce::Justification::centredRight, false);

        // A plugin missing from this machine is said, not hidden: the slot is
        // still in the project and comes back the day the plugin does.
        const auto* text = installed_ ? (plugin->bypassed ? "color.text.tertiary" : "color.text.primary")
                                      : "color.accent.danger";
        g.setColour(tokens_.colour(text));
        g.setFont(lookAndFeel_.typography().sans("font.size.body", "font.weight.medium"));
        g.drawText(installed_ ? name_ : name_ + " (absent)", area, juce::Justification::centredLeft, true);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(tokens_.integer("space.md"), 0);
        auto chips = area.removeFromRight(tokens_.integer("metric.track.chipWidth") * 2 +
                                          tokens_.integer("space.sm") * 2);
        chips = chips.withSizeKeepingCentre(chips.getWidth(), tokens_.integer("metric.track.chipHeight"));
        chips.removeFromLeft(tokens_.integer("space.sm"));
        bypass_.setBounds(chips.removeFromLeft(tokens_.integer("metric.track.chipWidth")));
        chips.removeFromLeft(tokens_.integer("space.sm"));
        remove_.setBounds(chips.removeFromLeft(tokens_.integer("metric.track.chipWidth")));
    }

    // Clicking the slot opens the plugin's window, and clicking it again
    // closes it: the window is the plugin's, so the slot is the switch.
    void mouseDown(const juce::MouseEvent& event) override
    {
        juce::ignoreUnused(event);

        if (!plugins_.hasEditor(pluginId_))
            return;

        if (plugins_.editorIsOpen(pluginId_))
            plugins_.closeEditor(pluginId_);
        else
            plugins_.openEditor(pluginId_);

        refresh();
    }

    void mouseEnter(const juce::MouseEvent& event) override
    {
        juce::ignoreUnused(event);
        repaint();
    }

    void mouseExit(const juce::MouseEvent& event) override
    {
        juce::ignoreUnused(event);
        repaint();
    }

private:
    [[nodiscard]] const domain::PluginInstance* instance() const
    {
        const auto* track = state_.findTrack(trackId_);
        if (track == nullptr)
            return nullptr;

        const auto found =
            std::find_if(track->plugins.begin(),
                         track->plugins.end(),
                         [this](const domain::PluginInstance& plugin) { return plugin.id == pluginId_; });

        return found != track->plugins.end() ? &(*found) : nullptr;
    }

    const Tokens& tokens_;
    DawLookAndFeel& lookAndFeel_;
    domain::CommandBus& bus_;
    const domain::ProjectState& state_;
    PluginHost& plugins_;
    domain::TrackId trackId_;
    domain::PluginId pluginId_;
    int position_;

    juce::String name_;
    juce::String format_;
    bool installed_{true};
    bool open_{false};

    juce::ToggleButton bypass_;
    juce::TextButton remove_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Slot)
};

PluginChainPanel::PluginChainPanel(const PanelContext& context)
    : tokens_(context.tokens)
    , lookAndFeel_(context.lookAndFeel)
    , bus_(context.bus)
    , state_(context.state)
    , project_(context.project)
    , selection_(context.selection)
    , plugins_(context.plugins)
{
    titled_ = context.titled;
    setLookAndFeel(&lookAndFeel_);
    setOpaque(true); // paint() fills the whole rectangle: what is behind is never painted

    slotHolder_ = std::make_unique<juce::Component>();
    viewport_.setViewedComponent(slotHolder_.get(), false);
    viewport_.setScrollBarsShown(true, false);
    addAndMakeVisible(viewport_);

    addAndMakeVisible(add_);
    add_.onClick = [this] { showInsertMenu(); };

    project_.addChangeListener(this);
    selection_.addChangeListener(this);
    rebuild();
}

PluginChainPanel::~PluginChainPanel()
{
    selection_.removeChangeListener(this);
    project_.removeChangeListener(this);
    setLookAndFeel(nullptr);
}

const domain::Track* PluginChainPanel::track() const
{
    return state_.findTrack(selection_.track());
}

void PluginChainPanel::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    juce::ignoreUnused(source);

    // Same rule as the track list: rebuild only when the chain itself changed,
    // refresh otherwise. A bypass must not destroy the slot the user just
    // clicked.
    const auto* selected = track();

    bool sameChain = selected != nullptr && selected->plugins.size() == slots_.size();
    if (selected == nullptr)
        sameChain = slots_.empty();

    for (std::size_t index = 0; sameChain && selected != nullptr && index < selected->plugins.size(); ++index)
        sameChain = slots_[index]->pluginId() == selected->plugins[index].id;

    if (!sameChain)
    {
        rebuild();
        return;
    }

    for (auto* slot : slots_)
        slot->refresh();

    // Selecting a track changes the header and whether a plugin can be added,
    // even when the chain itself is identical: without this, the button stays
    // as it was when the panel was built, which is to say disabled for ever.
    refreshChrome();
    repaint();
}

void PluginChainPanel::refreshChrome()
{
    add_.setEnabled(track() != nullptr);

    // An empty viewport would paint its own background over the sentence that
    // says why the chain is empty, so it goes away while there is nothing to
    // scroll.
    viewport_.setVisible(!slots_.empty());
}

void PluginChainPanel::rebuild()
{
    slots_.clear();
    slotHolder_->removeAllChildren();

    if (const auto* selected = track(); selected != nullptr)
    {
        int position = 0;
        for (const auto& plugin : selected->plugins)
        {
            auto slot = std::make_unique<Slot>(
                tokens_, lookAndFeel_, bus_, state_, plugins_, selected->id, plugin.id, position++);

            slots_.push_back(slot.get());
            slotHolder_->addAndMakeVisible(slot.release());
        }
    }

    refreshChrome();
    resized();
    repaint();
}

void PluginChainPanel::showInsertMenu()
{
    // Sorted by format then by name: a machine holds hundreds of plugins, and
    // the order a scan happened to produce is the one order that helps nobody.
    auto installed = plugins_.available();
    std::sort(installed.begin(),
              installed.end(),
              [](const domain::PluginRef& lhs, const domain::PluginRef& rhs)
              {
                  if (lhs.format != rhs.format)
                      return lhs.format < rhs.format;
                  return juce::String(lhs.name).compareIgnoreCase(juce::String(rhs.name)) < 0;
              });

    juce::PopupMenu menu;
    menu.setLookAndFeel(&lookAndFeel_);

    // One submenu per format, so the first thing the list asks is a question
    // with two answers instead of three hundred.
    juce::PopupMenu format;
    std::string current;

    for (int index = 0; index < static_cast<int>(installed.size()); ++index)
    {
        const auto& ref = installed[static_cast<std::size_t>(index)];

        if (ref.format != current)
        {
            if (!current.empty())
                menu.addSubMenu(juce::String(current), format);

            format.clear();
            format.setLookAndFeel(&lookAndFeel_);
            current = ref.format;
        }

        format.addItem(index + 1, juce::String(ref.name));
    }

    if (!current.empty())
        menu.addSubMenu(juce::String(current), format);

    if (installed.empty())
        menu.addItem(noneItemId, "Aucun plugin connu", false, false);

    menu.addSeparator();
    menu.addItem(rescanItemId, u8"Rechercher les plugins installés");

    menu.showMenuAsync(juce::PopupMenu::Options{}.withTargetComponent(add_),
                       [this, installed](int choice)
                       {
                           if (choice == rescanItemId)
                           {
                               plugins_.rescan();
                               return;
                           }

                           if (choice <= 0 || choice > static_cast<int>(installed.size()))
                               return;

                           insert(installed[static_cast<std::size_t>(choice - 1)]);
                       });
}

void PluginChainPanel::insert(const domain::PluginRef& ref)
{
    const auto* selected = track();
    if (selected == nullptr)
        return;

    // The identifier is generated here and travels in the payload, like a
    // track's: a replay has to rebuild this instance, not another one.
    domain::PluginInstance instance{};
    instance.id = domain::PluginId::generate();
    instance.ref = ref;

    static_cast<void>(bus_.execute(
        std::make_unique<domain::InsertPlugin>(selected->id, instance, selected->plugins.size())));
}

void PluginChainPanel::paint(juce::Graphics& g)
{
    g.fillAll(tokens_.colour("color.surface.panel"));

    auto header = getLocalBounds().removeFromTop(tokens_.integer("metric.panel.headerHeight"));
    g.setColour(tokens_.colour("color.border.hairline"));
    g.fillRect(header.removeFromBottom(tokens_.integer("stroke.hairline")));

    header.removeFromLeft(tokens_.integer("space.md"));
    header.removeFromRight(tokens_.integer("space.md"));

    const auto* selected = track();
    auto name = header.removeFromRight(header.getWidth() / 2);

    g.setColour(tokens_.colour("color.text.tertiary"));
    g.setFont(lookAndFeel_.typography().caps("font.size.micro"));
    if (!titled_)
    {
        g.drawText(u8"CHAÎNE", header, juce::Justification::centredLeft, false);
    }

    g.setColour(tokens_.colour("color.text.disabled"));
    g.setFont(lookAndFeel_.typography().sans("font.size.micro", "font.weight.regular"));
    g.drawText(selected != nullptr ? juce::String(selected->name) : juce::String("aucune piste"),
               name,
               juce::Justification::centredRight,
               true);

    if (selected != nullptr && !slots_.empty())
        return;

    auto area = getLocalBounds();
    area.removeFromTop(tokens_.integer("metric.panel.headerHeight"));
    area.removeFromBottom(tokens_.integer("metric.plugin.slotHeight"));

    g.setColour(tokens_.colour("color.text.disabled"));
    g.setFont(lookAndFeel_.typography().sans("font.size.caption", "font.weight.regular"));
    g.drawText(selected != nullptr ? u8"Chaîne vide" : u8"Sélectionnez une piste",
               area,
               juce::Justification::centred,
               false);
}

void PluginChainPanel::resized()
{
    auto area = getLocalBounds();
    area.removeFromTop(tokens_.integer("metric.panel.headerHeight"));

    add_.setBounds(area.removeFromBottom(tokens_.integer("metric.plugin.slotHeight")));
    viewport_.setBounds(area);

    const auto slotHeight = tokens_.integer("metric.plugin.slotHeight");
    slotHolder_->setSize(viewport_.getMaximumVisibleWidth(), slotHeight * static_cast<int>(slots_.size()));

    int y = 0;
    for (auto* slot : slots_)
    {
        slot->setBounds(0, y, slotHolder_->getWidth(), slotHeight);
        y += slotHeight;
    }
}

} // namespace daw::ui
