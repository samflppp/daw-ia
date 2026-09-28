#include "daw/ui/panels/ChannelRackPanel.h"

#include "daw/domain/commands/PatternCommands.h"
#include "daw/domain/commands/PluginCommands.h"
#include "daw/domain/commands/SampleCommands.h"
#include "daw/domain/commands/TrackCommands.h"
#include "daw/domain/tidy/Roles.h"
#include "daw/ui/model/PatternEditing.h"

#include <algorithm>
#include <memory>
#include <utility>

namespace daw::ui
{

ChannelRackPanel::ChannelRackPanel(const PanelContext& context)
    : tokens_(context.tokens)
    , lookAndFeel_(context.lookAndFeel)
    , bus_(context.bus)
    , state_(context.state)
    , project_(context.project)
    , selection_(context.selection)
    , samples_(context.samples)
    , plugins_(context.plugins)
{
    titled_ = context.titled;
    setLookAndFeel(&lookAndFeel_);

    // A button that kept the focus after a click would take the next press
    // of Space instead of the transport.
    for (auto* control : std::initializer_list<juce::Component*>{&addSample_, &addInstrument_})
    {
        control->setWantsKeyboardFocus(false);
        addAndMakeVisible(*control);
    }

    addSample_.onClick = [this] { chooseSample(); };
    addInstrument_.onClick = [this] { showInstrumentMenu(); };
    addSample_.setTooltip(u8"Un nouveau canal qui joue un sample de la machine");
    addInstrument_.setTooltip(u8"Un nouveau canal sur un instrument VST ou CLAP, ou sur le synthé intégré");

    project_.addChangeListener(this);
    selection_.addChangeListener(this);
}

ChannelRackPanel::~ChannelRackPanel()
{
    selection_.removeChangeListener(this);
    project_.removeChangeListener(this);
    setLookAndFeel(nullptr);
}

void ChannelRackPanel::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    juce::ignoreUnused(source);
    repaint();
}

// --- geometry ---------------------------------------------------------------

juce::Rectangle<int> ChannelRackPanel::channelArea() const
{
    auto area = getLocalBounds();
    area.removeFromTop(tokens_.integer("metric.panel.headerHeight"));
    area.removeFromBottom(tokens_.integer("metric.channelRack.rowHeight") + tokens_.integer("space.sm"));
    return area;
}

juce::Rectangle<int> ChannelRackPanel::channelBounds(int row) const
{
    const auto area = channelArea();
    const auto rowHeight = tokens_.integer("metric.channelRack.rowHeight");
    return {area.getX(), area.getY() + row * rowHeight, area.getWidth(), rowHeight};
}

int ChannelRackPanel::rowAtY(int y) const
{
    const auto area = channelArea();
    const auto rowHeight = tokens_.integer("metric.channelRack.rowHeight");
    if (y < area.getY() || y >= area.getBottom() || rowHeight <= 0)
        return -1;

    const auto row = (y - area.getY()) / rowHeight;
    return row < static_cast<int>(state_.tracks().size()) ? row : -1;
}

juce::String ChannelRackPanel::instrumentName(const domain::Track& track) const
{
    if (track.sample.has_value())
        return juce::String::fromUTF8(track.sample->name.c_str());

    // The first instrument of the chain is the one that plays; without one,
    // the projection puts Tracktion's own synth there.
    for (const auto& plugin : track.plugins)
    {
        if (plugins_.isInstrument(plugin.ref))
            return juce::String::fromUTF8(plugin.ref.name.c_str());
    }
    return juce::String{u8"synthé intégré"};
}

std::string ChannelRackPanel::suggestedName(const domain::Track& track) const
{
    // Only over a name nobody chose: « Piste 3 », or the instrument's own
    // name, which says what plays and not what is played.
    const auto instrument = instrumentName(track).toStdString();
    if (!domain::tidy::isDefaultName(track.name) && track.name != instrument)
        return {};

    const domain::tidy::PresetNames presets = [this](domain::TrackId id) -> std::string
    {
        const auto* found = state_.findTrack(id);
        if (found == nullptr)
            return {};
        for (const auto& plugin : found->plugins)
        {
            if (plugins_.isInstrument(plugin.ref))
                return plugins_.presetName(plugin.id);
        }
        return {};
    };

    const auto guess = domain::tidy::classifyTrack(state_, track.id, presets);
    if (guess.family == domain::tidy::Family::unknown || guess.evidence == domain::tidy::Evidence::trackName)
        return {};

    auto name = domain::tidy::suggestedName(guess.family);
    return name == track.name ? std::string{} : name;
}

juce::Rectangle<int> ChannelRackPanel::suggestionBounds(int row) const
{
    auto content = channelBounds(row).reduced(tokens_.integer("space.sm"), 0);
    content.removeFromLeft(tokens_.integer("metric.channelRack.channelWidth"));
    return content.removeFromRight(tokens_.integer("metric.channelRack.channelWidth"));
}

// --- painting ---------------------------------------------------------------

void ChannelRackPanel::resized()
{
    // The two ways in, under the channels, where FL keeps its "+".
    auto footer =
        getLocalBounds()
            .removeFromBottom(tokens_.integer("metric.channelRack.rowHeight") + tokens_.integer("space.sm"))
            .reduced(tokens_.integer("space.md"), tokens_.integer("space.xs"));
    addSample_.setBounds(footer.removeFromLeft(tokens_.integer("metric.channelRack.channelWidth") * 3 / 4));
    footer.removeFromLeft(tokens_.integer("space.sm"));
    addInstrument_.setBounds(
        footer.removeFromLeft(tokens_.integer("metric.channelRack.channelWidth") * 3 / 4));
}

void ChannelRackPanel::paint(juce::Graphics& g)
{
    g.fillAll(tokens_.colour("color.surface.sunken"));

    auto header = getLocalBounds().removeFromTop(tokens_.integer("metric.panel.headerHeight"));
    g.setColour(tokens_.colour("color.surface.panel"));
    g.fillRect(header);
    g.setColour(tokens_.colour("color.border.hairline"));
    g.fillRect(header.removeFromBottom(tokens_.integer("stroke.hairline")));

    header.removeFromLeft(tokens_.integer("space.md"));
    g.setColour(tokens_.colour("color.text.tertiary"));
    g.setFont(lookAndFeel_.typography().caps("font.size.micro"));
    if (!titled_)
    {
        g.drawText("CHANNEL RACK",
                   header.removeFromLeft(tokens_.integer("metric.channelRack.channelWidth")),
                   juce::Justification::centredLeft,
                   false);
    }

    if (state_.tracks().empty())
    {
        paintEmpty(g);
        return;
    }

    paintChannels(g, channelArea());
}

void ChannelRackPanel::paintEmpty(juce::Graphics& g) const
{
    g.setColour(tokens_.colour("color.text.disabled"));
    g.setFont(lookAndFeel_.typography().sans("font.size.caption", "font.weight.regular"));
    g.drawText(u8"« + Sample », « + Instrument », ou glissez un sample ici",
               channelArea(),
               juce::Justification::centred,
               false);
}

void ChannelRackPanel::paintChannels(juce::Graphics& g, juce::Rectangle<int> area) const
{
    const auto hairline = tokens_.integer("stroke.hairline");

    g.saveState();
    g.reduceClipRegion(area);

    for (std::size_t index = 0; index < state_.tracks().size(); ++index)
    {
        const auto& track = state_.tracks()[index];
        const auto row = channelBounds(static_cast<int>(index));
        if (row.getY() >= area.getBottom())
            break;

        g.setColour(tokens_.colour("color.surface.panel"));
        g.fillRect(row);
        if (track.id == selection_.track())
        {
            g.setColour(tokens_.colour("color.state.selected"));
            g.fillRect(row);
        }

        g.setColour(tokens_.colour("color.border.hairline"));
        g.fillRect(row.getX(), row.getBottom() - hairline, row.getWidth(), hairline);

        // A sampler channel is marked, so a dropped kick is told apart from a
        // synth channel at a glance.
        if (track.sample.has_value())
        {
            g.setColour(tokens_.colour("color.actor.copilot"));
            g.fillRect(row.getX(), row.getY(), hairline * 2, row.getHeight());
        }

        auto content = row.reduced(tokens_.integer("space.sm"), 0);
        auto name = content.removeFromLeft(tokens_.integer("metric.channelRack.channelWidth"));

        g.setColour(tokens_.colour(track.muted ? "color.text.disabled" : "color.text.primary"));
        g.setFont(lookAndFeel_.typography().sans("font.size.caption", "font.weight.medium"));
        g.drawText(juce::String::fromUTF8(track.name.c_str()), name, juce::Justification::centredLeft, true);

        // What plays the notes of this channel, where the steps used to be.
        g.setColour(tokens_.colour("color.text.tertiary"));
        g.setFont(lookAndFeel_.typography().sans("font.size.micro", "font.weight.regular"));
        g.drawText(instrumentName(track), content, juce::Justification::centredLeft, true);

        // A name to accept, in grey, like a proposal: nothing is written
        // until it is clicked.
        if (const auto suggestion = suggestedName(track); !suggestion.empty())
        {
            g.setColour(tokens_.colour("color.note.ghost"));
            g.setFont(lookAndFeel_.typography().sans("font.size.caption", "font.weight.medium"));
            g.drawText(juce::String::fromUTF8(u8"→ ") + juce::String::fromUTF8(suggestion.c_str()),
                       suggestionBounds(static_cast<int>(index)),
                       juce::Justification::centredRight,
                       true);
        }
    }

    g.restoreState();
}

// --- editing ------------------------------------------------------------------

void ChannelRackPanel::mouseDown(const juce::MouseEvent& event)
{
    const auto row = rowAtY(event.getPosition().getY());
    if (row < 0)
        return;

    if (event.mods.isRightButtonDown())
    {
        showChannelMenu(row);
        return;
    }

    // The grey name, accepted: one track.rename, one Ctrl+Z.
    const auto& track = state_.tracks()[static_cast<std::size_t>(row)];
    if (const auto suggestion = suggestedName(track);
        !suggestion.empty() && suggestionBounds(row).contains(event.getPosition()))
    {
        static_cast<void>(bus_.execute(std::make_unique<domain::RenameTrack>(track.id, suggestion)));
        return;
    }

    // Selecting is not an edit and writes no command: the piano roll follows.
    selection_.selectTrack(state_.tracks()[static_cast<std::size_t>(row)].id);
    repaint();
}

void ChannelRackPanel::mouseDoubleClick(const juce::MouseEvent& event)
{
    if (const auto row = rowAtY(event.getPosition().getY()); row >= 0)
        renameChannel(row);
}

void ChannelRackPanel::showChannelMenu(int row)
{
    juce::PopupMenu menu;
    menu.addItem(renameItem, u8"Renommer…");
    menu.addItem(removeItem, "Retirer le canal");

    const auto trackId = state_.tracks()[static_cast<std::size_t>(row)].id;
    juce::Component::SafePointer<ChannelRackPanel> safe{this};
    menu.showMenuAsync(
        juce::PopupMenu::Options{}.withTargetComponent(this),
        [safe, trackId](int chosen)
        {
            if (safe == nullptr)
                return;

            const auto& tracks = safe->state_.tracks();
            const auto found =
                std::find_if(tracks.begin(),
                             tracks.end(),
                             [trackId](const domain::Track& track) { return track.id == trackId; });
            if (found == tracks.end())
                return;

            if (chosen == renameItem)
                safe->renameChannel(static_cast<int>(found - tracks.begin()));
            else if (chosen == removeItem)
                static_cast<void>(safe->bus_.execute(std::make_unique<domain::RemoveTrack>(trackId)));
        });
}

void ChannelRackPanel::renameChannel(int row)
{
    if (row < 0 || row >= static_cast<int>(state_.tracks().size()))
        return;

    const auto& track = state_.tracks()[static_cast<std::size_t>(row)];
    auto* window = new juce::AlertWindow(u8"Renommer le canal", {}, juce::MessageBoxIconType::NoIcon, this);
    window->addTextEditor("name", juce::String::fromUTF8(track.name.c_str()));
    window->addButton("Renommer", 1, juce::KeyPress(juce::KeyPress::returnKey));
    window->addButton("Annuler", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    juce::Component::SafePointer<ChannelRackPanel> safe{this};
    const auto trackId = track.id;
    window->enterModalState(
        true,
        juce::ModalCallbackFunction::create(
            [safe, window, trackId](int result)
            {
                if (safe == nullptr || result != 1)
                    return;

                const auto name = window->getTextEditorContents("name").trim();
                if (name.isNotEmpty())
                    static_cast<void>(safe->bus_.execute(
                        std::make_unique<domain::RenameTrack>(trackId, name.toStdString())));
            }),
        true);
}

void ChannelRackPanel::chooseSample()
{
    auto folders = samples_.folders();
    const auto start =
        folders.empty() ? juce::File::getSpecialLocation(juce::File::userMusicDirectory) : folders.front();
    chooser_ = std::make_unique<juce::FileChooser>(
        u8"Choisir un sample", start, "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");

    juce::Component::SafePointer<ChannelRackPanel> safe{this};
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safe](const juce::FileChooser& chooser)
                          {
                              if (safe != nullptr && chooser.getResult().existsAsFile())
                                  safe->dropSample(chooser.getResult(), safe->getHeight());
                          });
}

void ChannelRackPanel::showInstrumentMenu()
{
    // Instruments only: an effect as the first thing of a channel would play
    // no note at all.
    std::vector<domain::PluginRef> instruments;
    for (const auto& ref : plugins_.available())
    {
        if (plugins_.isInstrument(ref))
            instruments.push_back(ref);
    }
    std::sort(instruments.begin(),
              instruments.end(),
              [](const domain::PluginRef& lhs, const domain::PluginRef& rhs)
              { return juce::String(lhs.name).compareIgnoreCase(juce::String(rhs.name)) < 0; });

    juce::PopupMenu menu;
    menu.setLookAndFeel(&lookAndFeel_);
    menu.addItem(builtInSynthItem, u8"Synthé intégré (4OSC)");
    menu.addSeparator();
    for (std::size_t index = 0; index < instruments.size(); ++index)
    {
        const auto& ref = instruments[index];
        menu.addItem(firstPluginItem + static_cast<int>(index),
                     juce::String::fromUTF8(ref.name.c_str()) + "  (" + juce::String(ref.format) + ")");
    }
    if (instruments.empty())
        menu.addItem(-1, u8"Aucun instrument installé trouvé", false, false);
    menu.addSeparator();
    menu.addItem(rescanItem, u8"Rechercher les plugins installés");

    juce::Component::SafePointer<ChannelRackPanel> safe{this};
    menu.showMenuAsync(juce::PopupMenu::Options{}.withTargetComponent(addInstrument_),
                       [safe, instruments](int chosen)
                       {
                           if (safe == nullptr)
                               return;
                           if (chosen == rescanItem)
                               safe->plugins_.rescan();
                           else if (chosen == builtInSynthItem)
                               safe->addInstrument(std::nullopt);
                           else if (chosen >= firstPluginItem &&
                                    chosen < firstPluginItem + static_cast<int>(instruments.size()))
                               safe->addInstrument(
                                   instruments[static_cast<std::size_t>(chosen - firstPluginItem)]);
                       });
}

void ChannelRackPanel::addInstrument(const std::optional<domain::PluginRef>& ref)
{
    // A channel and what plays on it: one thing the user did, one Ctrl+Z.
    const auto trackId = domain::TrackId::generate();
    std::vector<std::unique_ptr<domain::Command>> commands;
    commands.push_back(
        std::make_unique<domain::AddTrack>(trackId, ref.has_value() ? ref->name : std::string{"Synth"}, 0.0));

    if (ref.has_value())
    {
        // The identifier is generated here and travels in the payload: a
        // replay has to rebuild this instance, not another one.
        domain::PluginInstance instance{};
        instance.id = domain::PluginId::generate();
        instance.ref = *ref;
        commands.push_back(std::make_unique<domain::InsertPlugin>(trackId, instance, 0));
    }

    domain::GroupOptions group{};
    group.label = "canal instrument : " + (ref.has_value() ? ref->name : std::string{"synthé intégré"});
    if (bus_.executeGroup(std::move(commands), group).ok())
        selection_.selectTrack(trackId);
}

// --- dropping samples ---------------------------------------------------------

void ChannelRackPanel::dropSample(const juce::File& file, int y)
{
    auto sample = samples_.import(file);
    if (!sample)
        return;

    // On a channel: that channel plays the sample from now on.
    if (const auto row = rowAtY(y); row >= 0)
    {
        const auto& track = state_.tracks()[static_cast<std::size_t>(row)];
        static_cast<void>(bus_.execute(std::make_unique<domain::SetTrackSample>(track.id, sample.value())));
        selection_.selectTrack(track.id);
        return;
    }

    // Below the channels: a new one, named after the sample. Two commands, one
    // thing the user did, one Ctrl+Z.
    const auto trackId = domain::TrackId::generate();
    std::vector<std::unique_ptr<domain::Command>> commands;
    commands.push_back(
        std::make_unique<domain::AddTrack>(trackId, file.getFileNameWithoutExtension().toStdString(), 0.0));
    commands.push_back(std::make_unique<domain::SetTrackSample>(trackId, sample.value()));

    domain::GroupOptions group{};
    group.label = "canal sampler : " + sample.value().name;

    if (bus_.executeGroup(std::move(commands), group).ok())
        selection_.selectTrack(trackId);
}

bool ChannelRackPanel::isInterestedInDragSource(const SourceDetails& details)
{
    return details.description.toString().startsWith("sample:");
}

void ChannelRackPanel::itemDropped(const SourceDetails& details)
{
    const auto description = details.description.toString();
    if (description.startsWith("sample:"))
        dropSample(juce::File{description.fromFirstOccurrenceOf("sample:", false, false)},
                   details.localPosition.y);
}

bool ChannelRackPanel::isInterestedInFileDrag(const juce::StringArray& files)
{
    return std::any_of(files.begin(),
                       files.end(),
                       [](const juce::String& path) { return SampleHost::isSampleFile(juce::File{path}); });
}

void ChannelRackPanel::filesDropped(const juce::StringArray& files, int x, int y)
{
    juce::ignoreUnused(x);
    for (const auto& path : files)
    {
        if (SampleHost::isSampleFile(juce::File{path}))
        {
            dropSample(juce::File{path}, y);
            return;
        }
    }
}

} // namespace daw::ui
