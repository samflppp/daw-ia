#include "daw/ui/panels/FluxPanel.h"

#include "daw/domain/commands/MixCommands.h"
#include "daw/domain/commands/PluginCommands.h"
#include "daw/domain/flux/Picture.h"
#include "daw/domain/project/InternalEffects.h"

#include <algorithm>
#include <cmath>

namespace daw::ui
{
namespace
{

using domain::flux::LinkKind;
using domain::flux::Node;
using domain::flux::NodeKind;
using domain::flux::StateKind;

juce::String text(const std::string& value)
{
    return juce::String::fromUTF8(value.c_str());
}

// A send drawn in the graph starts where the mixer's does.
constexpr double firstSendDb = -12.0;

constexpr int noneItem = 1;
constexpr int newBusItem = 2;
constexpr int firstInternalItem = 100;
constexpr int firstInstalledItem = 1000;

} // namespace

FluxPanel::FluxPanel(const PanelContext& context)
    : tokens_(context.tokens)
    , lookAndFeel_(context.lookAndFeel)
    , bus_(context.bus)
    , state_(context.state)
    , project_(context.project)
    , mix_(context.mix)
    , plugins_(context.plugins)
    , flux_(context.flux)
    , titled_(context.titled)
{
    setOpaque(true);
    setWantsKeyboardFocus(true);
    project_.addChangeListener(this);
    mix_.addChangeListener(this);
    rebuild();
}

FluxPanel::~FluxPanel()
{
    project_.removeChangeListener(this);
    mix_.removeChangeListener(this);
    listenAt({});
    disarm();
}

void FluxPanel::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    if (source == &project_ || source == &mix_)
    {
        rebuild();
        repaint();
    }
}

void FluxPanel::rebuild()
{
    const auto isInstrument = [this](const domain::PluginRef& ref) { return plugins_.isInstrument(ref); };
    const auto* proposed = mix_.stage() == MixHost::Stage::ready ? mix_.proposedState() : nullptr;
    const bool wasProposal = std::exchange(proposal_, proposed != nullptr);
    graph_ = domain::flux::graphOf(proposed != nullptr ? *proposed : state_, isInstrument);
    if (proposal_ != wasProposal)
    {
        shown_.clear();
        if (proposal_)
            listenAt({});
    }
    markProposal();
    if (!selected_.empty() && graph_.find(selected_) == nullptr)
        selected_.clear();

    // The place listened to may have been made again by the projection, or
    // be gone: told again, or the song given back.
    if (!listened_.empty())
        listenAt(graph_.find(listened_) != nullptr ? listened_ : std::string{});
    shown_.erase(std::remove_if(shown_.begin(),
                                shown_.end(),
                                [this](const Shown& shown) { return graph_.find(shown.node) == nullptr; }),
                 shown_.end());
}

// What the proposal adds and changes, against the graph of the project now,
// and the sentences of its changes, on the node each one moves: a level or a
// pan on the fader, a setting on the DAW's equaliser or compressor.
void FluxPanel::markProposal()
{
    added_.clear();
    changed_.clear();
    sentences_.clear();
    const auto* proposed = mix_.proposedState();
    const auto* proposal = mix_.proposal();
    if (!proposal_ || proposed == nullptr || proposal == nullptr)
        return;

    const auto live = domain::flux::graphOf(
        state_, [this](const domain::PluginRef& ref) { return plugins_.isInstrument(ref); });
    for (const auto& node : graph_.nodes)
    {
        const auto* before = live.find(node.id);
        if (before == nullptr)
            added_.push_back(node.id);
        else if (node.kind == NodeKind::fader && node.label != before->label)
            changed_.push_back(node.id);
        else if (node.kind == NodeKind::effect)
        {
            const auto* was = state_.findPlugin(node.plugin);
            const auto* will = proposed->findPlugin(node.plugin);
            if (was != nullptr && will != nullptr && !(*was == *will))
                changed_.push_back(node.id);
        }
    }

    for (const auto& change : proposal->changes)
    {
        std::string node = domain::flux::faderNode(change.track);
        if (change.kind == domain::mix::Change::Kind::equaliser ||
            change.kind == domain::mix::Change::Kind::compressor)
        {
            const auto identifier = change.kind == domain::mix::Change::Kind::equaliser
                                        ? domain::internal::equaliser
                                        : domain::internal::compressor;
            if (const auto* strip = proposed->findStrip(change.track); strip != nullptr)
                for (const auto& plugin : strip->plugins)
                    if (plugin.ref.format == domain::PluginRef::internalFormat &&
                        plugin.ref.identifier == identifier)
                        node = domain::flux::effectNode(plugin.id);
        }
        sentences_.emplace_back(node, change.sentence);
    }

    // The sound of each state, as the copy heard it: drawn as it is.
    shown_.clear();
    for (const auto& node : graph_.nodes)
        if (const auto place = placeOf(node); place)
        {
            auto samples = mix_.proposedSound(place->strip, place->slot);
            const auto rate = flux_.sampleRate();
            const auto tail =
                std::min(samples.size(),
                         static_cast<std::size_t>(rate * tokens_.integer("metric.flux.levelMs") / 1000.0));
            const auto level = domain::flux::peakDbOf(samples.data() + (samples.size() - tail), tail);
            shown_.push_back(Shown{node.id, std::move(samples), level});
        }
}

std::vector<std::string> FluxPanel::sentencesAt(const std::string& node) const
{
    std::vector<std::string> said;
    for (const auto& [at, sentence] : sentences_)
        if (at == node)
            said.push_back(sentence);
    return said;
}

// --- geometry -----------------------------------------------------------------

juce::Rectangle<int> FluxPanel::graphArea() const
{
    auto area = getLocalBounds();
    if (!titled_)
        area.removeFromTop(tokens_.integer("metric.panel.headerHeight"));
    if (!selected_.empty())
        area.removeFromBottom(tokens_.integer("metric.flux.detailHeight"));
    return area;
}

juce::Rectangle<int> FluxPanel::detailArea() const
{
    if (selected_.empty())
        return {};
    return getLocalBounds().removeFromBottom(tokens_.integer("metric.flux.detailHeight"));
}

juce::Point<float> FluxPanel::toScreen(juce::Point<float> graphPoint) const
{
    const auto area = graphArea().toFloat();
    return area.getTopLeft() + (graphPoint - origin_) * zoom_;
}

juce::Rectangle<float> FluxPanel::rawBoundsOf(const Node& node) const
{
    const auto column = static_cast<float>(tokens_.integer("metric.flux.columnWidth"));
    const auto row = static_cast<float>(tokens_.integer("metric.flux.rowHeight"));
    const juce::Point<float> centre{(static_cast<float>(node.column) + 0.5f) * column,
                                    (static_cast<float>(node.row) + 0.5f) * row};

    float width = 0.0f;
    float height = 0.0f;
    switch (node.kind)
    {
    case NodeKind::state:
        width = static_cast<float>(tokens_.integer("metric.flux.stateWidth"));
        height = static_cast<float>(tokens_.integer("metric.flux.stateHeight"));
        break;
    case NodeKind::effect:
        width = static_cast<float>(tokens_.integer("metric.flux.effectWidth"));
        height = static_cast<float>(tokens_.integer("metric.flux.effectHeight"));
        break;
    case NodeKind::fader:
        width = static_cast<float>(tokens_.integer("metric.flux.faderWidth"));
        height = static_cast<float>(tokens_.integer("metric.flux.faderHeight"));
        break;
    }
    return juce::Rectangle<float>{width, height}.withCentre(centre);
}

juce::Rectangle<float> FluxPanel::boundsOf(const std::string& node) const
{
    const auto* found = graph_.find(node);
    if (found == nullptr)
        return {};
    const auto raw = rawBoundsOf(*found);
    return {toScreen(raw.getTopLeft()), toScreen(raw.getBottomRight())};
}

juce::Rectangle<float> FluxPanel::contentBounds() const
{
    juce::Rectangle<float> all;
    for (const auto& node : graph_.nodes)
        all = all.isEmpty() ? rawBoundsOf(node) : all.getUnion(rawBoundsOf(node));
    return all;
}

const Node* FluxPanel::nodeAt(juce::Point<float> at) const
{
    for (const auto& node : graph_.nodes)
        if (boundsOf(node.id).contains(at))
            return &node;
    return nullptr;
}

std::optional<FluxHost::Place> FluxPanel::placeOf(const Node& node) const
{
    if (node.kind != NodeKind::state)
        return std::nullopt;
    const auto strip = node.strip.toString();
    switch (node.state)
    {
    case StateKind::source:
    case StateKind::sum:
        return FluxHost::Place{strip, FluxHost::sourceSlot};
    case StateKind::afterEffect:
        return FluxHost::Place{strip, node.plugin.toString()};
    case StateKind::afterFader:
        return FluxHost::Place{strip, FluxHost::faderSlot};
    }
    return std::nullopt;
}

// The state an effect reads from, or the one it gives.
std::string FluxPanel::neighbour(const std::string& effect, bool before) const
{
    for (const auto& link : graph_.links)
    {
        if (link.kind != LinkKind::chain)
            continue;
        if (before && link.to == effect)
            return link.from;
        if (!before && link.from == effect)
            return link.to;
    }
    return {};
}

// A link as it is drawn: out of the right of a node, into the left of the
// other. A long way stays on its row, kept free for it, up to the gap before
// the node it reaches; there it bends down or up into it.
juce::Path FluxPanel::pathOf(const domain::flux::Link& link) const
{
    juce::Path path;
    const auto from = boundsOf(link.from);
    const auto to = boundsOf(link.to);
    if (from.isEmpty() || to.isEmpty())
        return path;

    const juce::Point<float> start{from.getRight(), from.getCentreY()};
    const juce::Point<float> end{to.getX(), to.getCentreY()};
    path.startNewSubPath(start);
    const auto gap = static_cast<float>(tokens_.integer("metric.flux.columnWidth") -
                                        tokens_.integer("metric.flux.stateWidth")) *
                     zoom_;
    const auto straight = std::max(start.x, end.x - gap);
    if (straight > start.x)
        path.lineTo(straight, start.y);
    const auto bend = (end.x - straight) / 2.0f;
    path.cubicTo({straight + bend, start.y}, end.translated(-bend, 0.0f), end);
    return path;
}

const domain::flux::Link* FluxPanel::linkAt(juce::Point<float> at) const
{
    const auto reach = static_cast<float>(tokens_.integer("space.sm"));
    const domain::flux::Link* nearest = nullptr;
    auto best = reach;
    for (const auto& link : graph_.links)
    {
        const auto path = pathOf(link);
        if (path.isEmpty())
            continue;
        juce::Point<float> on;
        path.getNearestPoint(at, on);
        if (const auto distance = on.getDistanceFrom(at); distance <= best)
        {
            best = distance;
            nearest = &link;
        }
    }
    return nearest;
}

// Where an effect's dot is, in its node.
juce::Rectangle<float> FluxPanel::dotOf(const juce::Rectangle<float>& bounds) const
{
    auto inner = bounds.reduced(static_cast<float>(tokens_.integer("space.xs")) * zoom_, 0.0f);
    const auto dot = static_cast<float>(tokens_.integer("metric.flux.dotSize")) * zoom_;
    return inner.removeFromLeft(dot).withSizeKeepingCentre(dot, dot);
}

// --- navigation ---------------------------------------------------------------

void FluxPanel::frameAll()
{
    const auto content = contentBounds();
    const auto area = graphArea().toFloat().reduced(static_cast<float>(tokens_.integer("space.lg")));
    if (content.isEmpty() || area.isEmpty())
        return;

    const auto lowest = static_cast<float>(tokens_.integer("metric.flux.zoomMinPercent")) / 100.0f;
    const auto highest = static_cast<float>(tokens_.integer("metric.flux.zoomMaxPercent")) / 100.0f;
    // Never larger than life: a small graph is shown at its size.
    zoom_ = std::clamp(
        std::min({area.getWidth() / content.getWidth(), area.getHeight() / content.getHeight(), 1.0f}),
        lowest,
        highest);

    // Centred in the area, everything in view.
    const auto shownSize = juce::Point<float>{area.getWidth(), area.getHeight()} / zoom_;
    origin_ = content.getCentre() - shownSize / 2.0f -
              (area.getTopLeft() - graphArea().toFloat().getTopLeft()) / zoom_;
    framed_ = true;
    repaint();
}

void FluxPanel::zoomAround(juce::Point<float> at, float factor)
{
    const auto lowest = static_cast<float>(tokens_.integer("metric.flux.zoomMinPercent")) / 100.0f;
    const auto highest = static_cast<float>(tokens_.integer("metric.flux.zoomMaxPercent")) / 100.0f;
    const auto next = std::clamp(zoom_ * factor, lowest, highest);
    const auto topLeft = graphArea().toFloat().getTopLeft();

    // The graph point under the mouse stays under it.
    const auto under = origin_ + (at - topLeft) / zoom_;
    zoom_ = next;
    origin_ = under - (at - topLeft) / zoom_;
    repaint();
}

void FluxPanel::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    const auto step = static_cast<float>(tokens_.integer("metric.flux.zoomStepPercent")) / 100.0f;
    if (event.mods.isCtrlDown())
    {
        if (wheel.deltaY != 0.0f)
            zoomAround(event.position, wheel.deltaY > 0.0f ? 1.0f + step : 1.0f / (1.0f + step));
        return;
    }

    // The wheel scrolls down the rows, Shift across the columns.
    const auto row = static_cast<float>(tokens_.integer("metric.flux.rowHeight"));
    const auto column = static_cast<float>(tokens_.integer("metric.flux.columnWidth"));
    const auto amount =
        (wheel.deltaY != 0.0f ? wheel.deltaY : wheel.deltaX) * (wheel.isReversed ? -1.0f : 1.0f);
    if (event.mods.isShiftDown() || (wheel.deltaY == 0.0f && wheel.deltaX != 0.0f))
        origin_.x -= amount * column * 2.0f;
    else
        origin_.y -= amount * row * 2.0f;
    repaint();
}

void FluxPanel::mouseDown(const juce::MouseEvent& event)
{
    grabKeyboardFocus();
    pulling_ = Pulling::nothing;
    pullMoved_ = false;
    pressedState_.clear();
    if (event.mods.isMiddleButtonDown())
    {
        dragging_ = true;
        dragStart_ = event.position;
        originAtDrag_ = origin_;
        return;
    }

    if (!graphArea().toFloat().contains(event.position))
        return;
    const auto* node = nodeAt(event.position);

    if (event.mods.isPopupMenu())
    {
        if (node != nullptr)
            return;
        if (const auto* link = linkAt(event.position); link != nullptr)
            showLinkMenu(*link);
        else
            showEmptyMenu();
        return;
    }

    if (node != nullptr && node->kind == NodeKind::effect)
    {
        if (dotOf(boundsOf(node->id))
                .expanded(static_cast<float>(tokens_.integer("space.xxs")))
                .contains(event.position))
        {
            static_cast<void>(toggleBypass(node->id));
            return;
        }
        select(node->id);
        pulling_ = Pulling::effect;
        pulled_ = node->id;
        return;
    }
    if (node != nullptr && node->kind == NodeKind::state)
        pressedState_ = node->id;
    if (node != nullptr && node->kind == NodeKind::state && node->state == StateKind::afterFader &&
        node->strip != domain::ProjectState::masterTrackId())
    {
        pulling_ = Pulling::send;
        pulled_ = node->id;
        return;
    }
    if (node == nullptr)
    {
        // The end of an output, near where it arrives: taken to rewire it.
        if (const auto* link = linkAt(event.position); link != nullptr && link->kind == LinkKind::output)
        {
            const auto end = boundsOf(link->to);
            const auto reach = static_cast<float>(tokens_.integer("metric.flux.columnWidth")) * zoom_;
            if (event.position.x > end.getX() - reach)
            {
                pulling_ = Pulling::output;
                pulled_ = link->from;
                return;
            }
        }
        select({});
    }
}

void FluxPanel::mouseDrag(const juce::MouseEvent& event)
{
    if (dragging_)
    {
        origin_ = originAtDrag_ - (event.position - dragStart_) / zoom_;
        repaint();
        return;
    }
    if (pulling_ == Pulling::nothing)
        return;
    pullAt_ = event.position;
    pullMoved_ = pullMoved_ || event.getDistanceFromDragStart() > tokens_.integer("space.xs");
    repaint();
}

void FluxPanel::mouseUp(const juce::MouseEvent& event)
{
    dragging_ = false;
    const auto pulling = std::exchange(pulling_, Pulling::nothing);
    const auto pressed = std::exchange(pressedState_, std::string{});

    // A state clicked, not pulled: listened to alone, or no longer.
    if (!proposal_ && !pressed.empty() && !pullMoved_ && nodeAt(event.position) == graph_.find(pressed))
    {
        listenAt(listened_ == pressed ? std::string{} : pressed);
        return;
    }
    if (pulling == Pulling::nothing || !pullMoved_)
        return;
    repaint();

    const auto* node = nodeAt(event.position);
    switch (pulling)
    {
    case Pulling::effect:
        if (const auto* link = linkAt(event.position); link != nullptr && node == nullptr)
            static_cast<void>(dropEffect(pulled_, link->from, link->to));
        break;
    case Pulling::send:
        if (node != nullptr)
            static_cast<void>(sendTo(pulled_, node->id));
        break;
    case Pulling::output:
        if (node != nullptr)
            static_cast<void>(outputTo(pulled_, node->id));
        break;
    case Pulling::nothing:
        break;
    }
}

// --- the gestures -------------------------------------------------------------

const Node* FluxPanel::stripNode(const std::string& id) const
{
    return graph_.find(id);
}

bool FluxPanel::insertOn(const std::string& from, const std::string& to, const domain::PluginRef& ref)
{
    const auto* link = graph_.link(from, to);
    if (link == nullptr || link->kind != LinkKind::chain || !link->beforeFader)
        return false;
    // The identifier is made here and travels in the payload.
    domain::PluginInstance instance{};
    instance.id = domain::PluginId::generate();
    instance.ref = ref;
    return bus_.execute(std::make_unique<domain::InsertPlugin>(link->strip, instance, link->insertIndex))
        .ok();
}

bool FluxPanel::dropEffect(const std::string& effect, const std::string& from, const std::string& to)
{
    const auto* node = graph_.find(effect);
    const auto* link = graph_.link(from, to);
    if (node == nullptr || node->kind != NodeKind::effect || link == nullptr ||
        link->kind != LinkKind::chain || !link->beforeFader || from == effect || to == effect)
        return false;
    const auto* instance = state_.findPlugin(node->plugin);
    const auto location = state_.pluginLocation(node->plugin);
    if (instance == nullptr || !location.ok())
        return false;

    if (location.value().trackId == link->strip)
    {
        // The link's index counts the effect where it is: past it, one less.
        auto index = link->insertIndex;
        if (location.value().index < index)
            --index;
        if (index == location.value().index)
            return false;
        return bus_.execute(std::make_unique<domain::MovePlugin>(node->plugin, index)).ok();
    }

    // Into another chain: the same instance, its settings with it, one gesture.
    std::vector<std::unique_ptr<domain::Command>> commands;
    commands.push_back(std::make_unique<domain::RemovePlugin>(node->plugin));
    commands.push_back(std::make_unique<domain::InsertPlugin>(link->strip, *instance, link->insertIndex));
    domain::GroupOptions group{};
    group.label = "déplacer l'effet";
    return bus_.executeGroup(std::move(commands), group).ok();
}

bool FluxPanel::sendTo(const std::string& wayOut, const std::string& sum)
{
    const auto* from = graph_.find(wayOut);
    const auto* to = graph_.find(sum);
    if (from == nullptr || to == nullptr || from->kind != NodeKind::state ||
        from->state != StateKind::afterFader || to->kind != NodeKind::state || to->state != StateKind::sum ||
        to->strip == domain::ProjectState::masterTrackId() || to->strip == from->strip)
        return false;
    return bus_.execute(std::make_unique<domain::SetTrackSend>(from->strip, to->strip, firstSendDb)).ok();
}

bool FluxPanel::outputTo(const std::string& wayOut, const std::string& sum)
{
    const auto* from = graph_.find(wayOut);
    const auto* to = graph_.find(sum);
    if (from == nullptr || to == nullptr || from->kind != NodeKind::state ||
        from->state != StateKind::afterFader || to->kind != NodeKind::state || to->state != StateKind::sum ||
        to->strip == from->strip)
        return false;
    // The master is written as no bus at all.
    const auto output = to->strip == domain::ProjectState::masterTrackId() ? domain::TrackId{} : to->strip;
    return bus_.execute(std::make_unique<domain::SetTrackOutput>(from->strip, output)).ok();
}

bool FluxPanel::toggleBypass(const std::string& effect)
{
    const auto* node = graph_.find(effect);
    if (node == nullptr || node->kind != NodeKind::effect)
        return false;
    return bus_.execute(std::make_unique<domain::SetPluginBypassed>(node->plugin, !node->bypassed)).ok();
}

bool FluxPanel::removeSelected()
{
    const auto* node = graph_.find(selected_);
    if (node == nullptr || node->kind != NodeKind::effect)
        return false;
    const auto plugin = node->plugin;
    select({});
    return bus_.execute(std::make_unique<domain::RemovePlugin>(plugin)).ok();
}

bool FluxPanel::addBus()
{
    // The caller names what it creates, as the mixer's « + Bus ».
    const auto name = "Bus " + std::to_string(state_.buses().size() + 1);
    return bus_.execute(std::make_unique<domain::AddBus>(domain::TrackId::generate(), name)).ok();
}

void FluxPanel::showEmptyMenu()
{
    juce::PopupMenu menu;
    menu.addItem(newBusItem, juce::String::fromUTF8("Nouveau bus"));
    menu.showMenuAsync(juce::PopupMenu::Options{}.withTargetComponent(this),
                       [safe = juce::Component::SafePointer<FluxPanel>{this}](int choice)
                       {
                           if (safe != nullptr && choice == newBusItem)
                               static_cast<void>(safe->addBus());
                       });
}

void FluxPanel::showLinkMenu(const domain::flux::Link& link)
{
    if (link.kind != LinkKind::chain || !link.beforeFader)
        return;

    // The menu of the mixer's « ＋ effet »: the DAW's effects, then the
    // person's installed ones.
    juce::PopupMenu menu;
    std::vector<domain::PluginRef> refs;
    menu.addSectionHeader(juce::String::fromUTF8("Insérer ici"));
    for (const auto& effect : domain::internalEffects())
    {
        refs.push_back(domain::PluginRef{std::string{domain::PluginRef::internalFormat},
                                         std::string{effect.identifier},
                                         std::string{effect.name}});
        menu.addItem(firstInternalItem + static_cast<int>(refs.size()) - 1, text(std::string{effect.name}));
    }
    std::vector<domain::PluginRef> installed;
    for (const auto& ref : plugins_.available())
        if (plugins_.isInstalled(ref) && !plugins_.isInstrument(ref))
            installed.push_back(ref);
    std::sort(
        installed.begin(), installed.end(), [](const auto& a, const auto& b) { return a.name < b.name; });
    menu.addSectionHeader(juce::String::fromUTF8("Tes effets"));
    if (installed.empty())
        menu.addItem(noneItem, juce::String::fromUTF8("Aucun effet installé trouvé"), false);
    for (std::size_t index = 0; index < installed.size(); ++index)
        menu.addItem(firstInstalledItem + static_cast<int>(index), text(installed[index].name));

    menu.showMenuAsync(
        juce::PopupMenu::Options{}.withTargetComponent(this),
        [safe = juce::Component::SafePointer<FluxPanel>{this},
         from = link.from,
         to = link.to,
         refs,
         installed](int choice)
        {
            if (safe == nullptr)
                return;
            if (choice >= firstInstalledItem &&
                choice < firstInstalledItem + static_cast<int>(installed.size()))
                static_cast<void>(safe->insertOn(
                    from, to, installed[static_cast<std::size_t>(choice - firstInstalledItem)]));
            else if (choice >= firstInternalItem &&
                     choice < firstInternalItem + static_cast<int>(refs.size()))
                static_cast<void>(
                    safe->insertOn(from, to, refs[static_cast<std::size_t>(choice - firstInternalItem)]));
        });
}

void FluxPanel::mouseDoubleClick(const juce::MouseEvent& event)
{
    // An effect's editor, as from the mixer's slot.
    const auto* node = nodeAt(event.position);
    if (node != nullptr && node->kind == NodeKind::effect && plugins_.hasEditor(node->plugin))
        plugins_.openEditor(node->plugin);
}

void FluxPanel::listenAt(const std::string& node)
{
    const auto* found = graph_.find(node);
    const auto place = found != nullptr ? placeOf(*found) : std::nullopt;
    listened_ = place ? node : std::string{};
    if (place || flux_.listening())
        flux_.listen(place);
    repaint();
}

bool FluxPanel::keyPressed(const juce::KeyPress& key)
{
    if (key.getKeyCode() == juce::KeyPress::escapeKey && !listened_.empty())
    {
        listenAt({});
        return true;
    }
    if ((key.getKeyCode() == 'F' || key.getKeyCode() == 'f') && !key.getModifiers().isAnyModifierKeyDown())
    {
        frameAll();
        return true;
    }
    if (key.getKeyCode() == juce::KeyPress::escapeKey && !selected_.empty())
    {
        select({});
        return true;
    }
    if ((key.getKeyCode() == juce::KeyPress::deleteKey || key.getKeyCode() == juce::KeyPress::backspaceKey) &&
        !selected_.empty())
        return removeSelected();
    return false;
}

void FluxPanel::select(const std::string& node)
{
    selected_ = node;
    spectrumBefore_.clear();
    spectrumAfter_.clear();
    repaint();
}

// --- the sound ----------------------------------------------------------------

void FluxPanel::resized()
{
    if (!framed_ && isShowing())
        frameAll();
}

void FluxPanel::visibilityChanged()
{
    if (!isShowing())
    {
        listenAt({});
        disarm();
    }
}

void FluxPanel::parentHierarchyChanged()
{
    if (!isShowing())
    {
        listenAt({});
        disarm();
    }
    else if (!framed_)
        frameAll();
}

void FluxPanel::disarm()
{
    if (armed_.empty())
        return;
    armed_.clear();
    flux_.arm({});
}

void FluxPanel::armVisible()
{
    // The states on the screen, and the before and the after of the effect
    // looked at, wherever they are.
    std::vector<FluxHost::Place> wanted;
    const auto area = graphArea().toFloat();
    const auto before = neighbour(selected_, true);
    const auto after = neighbour(selected_, false);
    for (const auto& node : graph_.nodes)
    {
        const auto place = placeOf(node);
        if (!place)
            continue;
        if (area.intersects(boundsOf(node.id)) || node.id == before || node.id == after)
            wanted.push_back(*place);
    }
    if (wanted != armed_)
    {
        armed_ = wanted;
        flux_.arm(armed_);
    }
}

const FluxPanel::Shown* FluxPanel::shownFor(const std::string& node) const
{
    const auto found = std::find_if(
        shown_.begin(), shown_.end(), [&node](const Shown& shown) { return shown.node == node; });
    return found != shown_.end() ? &*found : nullptr;
}

std::vector<float> FluxPanel::shownAt(const std::string& node) const
{
    const auto* shown = shownFor(node);
    return shown != nullptr ? shown->samples : std::vector<float>{};
}

double FluxPanel::levelAt(const std::string& node) const
{
    const auto* shown = shownFor(node);
    return shown != nullptr ? shown->levelDb : -100.0;
}

void FluxPanel::frame()
{
    if (!isShowing())
    {
        listenAt({});
        disarm();
        return;
    }
    if (proposal_)
    {
        // The proposal's sound is the copy's, read once: nothing is armed.
        disarm();
        if (!selected_.empty() && spectrumBefore_.empty())
        {
            const auto bands = static_cast<std::size_t>(tokens_.integer("metric.flux.spectrumBands"));
            const auto rate = flux_.sampleRate();
            const auto spectrumOf = [&](const std::string& node)
            {
                const auto* shown = shownFor(node);
                return shown != nullptr ? domain::flux::spectrumOf(
                                              shown->samples.data(), shown->samples.size(), rate, bands)
                                        : std::vector<double>(bands, -100.0);
            };
            spectrumBefore_ = spectrumOf(neighbour(selected_, true));
            spectrumAfter_ = spectrumOf(neighbour(selected_, false));
            repaint();
        }
        return;
    }
    armVisible();

    const auto latest = flux_.latest();
    if (latest < 0)
        return;
    const auto rate = flux_.sampleRate();
    const auto count = static_cast<int>(rate * tokens_.integer("metric.flux.waveMs") / 1000.0);
    const auto levelCount = static_cast<std::size_t>(rate * tokens_.integer("metric.flux.levelMs") / 1000.0);
    const auto from = latest - count + 1;

    const auto before = neighbour(selected_, true);
    const auto after = neighbour(selected_, false);
    const auto area = graphArea().toFloat();
    for (const auto& node : graph_.nodes)
    {
        const auto place = placeOf(node);
        if (!place || (!area.intersects(boundsOf(node.id)) && node.id != before && node.id != after))
            continue;

        auto found = std::find_if(
            shown_.begin(), shown_.end(), [&node](const Shown& shown) { return shown.node == node.id; });
        if (found == shown_.end())
        {
            shown_.push_back(Shown{node.id, {}, -100.0});
            found = shown_.end() - 1;
        }
        found->samples.resize(static_cast<std::size_t>(std::max(0, count)));
        flux_.read(*place, from, count, found->samples.data());
        const auto tail = std::min(levelCount, found->samples.size());
        found->levelDb = domain::flux::peakDbOf(found->samples.data() + (found->samples.size() - tail), tail);
    }

    if (!selected_.empty())
    {
        const auto bands = static_cast<std::size_t>(tokens_.integer("metric.flux.spectrumBands"));
        const auto spectrumOf = [&](const std::string& node)
        {
            const auto* shown = shownFor(node);
            return shown != nullptr
                       ? domain::flux::spectrumOf(shown->samples.data(), shown->samples.size(), rate, bands)
                       : std::vector<double>(bands, -100.0);
        };
        spectrumBefore_ = spectrumOf(before);
        spectrumAfter_ = spectrumOf(after);
    }
    repaint();
}

// --- painting -----------------------------------------------------------------

void FluxPanel::paint(juce::Graphics& g)
{
    g.fillAll(tokens_.colour("color.surface.sunken"));

    if (!titled_)
    {
        auto header = getLocalBounds().removeFromTop(tokens_.integer("metric.panel.headerHeight"));
        g.setColour(tokens_.colour("color.surface.panel"));
        g.fillRect(header);
        g.setColour(tokens_.colour("color.border.hairline"));
        g.fillRect(header.removeFromBottom(tokens_.integer("stroke.hairline")));
        header.removeFromLeft(tokens_.integer("space.md"));
        g.setColour(tokens_.colour("color.text.tertiary"));
        g.setFont(lookAndFeel_.typography().caps("font.size.micro"));
        g.drawText(juce::String::fromUTF8("FLUX AUDIO"), header, juce::Justification::centredLeft);
    }

    {
        juce::Graphics::ScopedSaveState saved{g};
        g.reduceClipRegion(graphArea());
        paintLinks(g);
        for (const auto& node : graph_.nodes)
            paintNode(g, node);

        // The proposal: what it adds dashed, what it changes outlined, and
        // what the window shows said.
        if (proposal_)
        {
            const auto stroke = static_cast<float>(tokens_.integer("stroke.focus"));
            const auto dash = static_cast<float>(tokens_.integer("metric.flux.dash"));
            g.setColour(tokens_.colour("color.actor.copilot"));
            for (const auto& node : added_)
            {
                juce::Path outline;
                outline.addRoundedRectangle(boundsOf(node).expanded(stroke),
                                            tokens_.number("radius.md") * zoom_);
                const float pattern[] = {dash, dash};
                juce::Path dashed;
                juce::PathStrokeType{stroke}.createDashedStroke(dashed, outline, pattern, 2);
                g.fillPath(dashed);
            }
            for (const auto& node : changed_)
                g.drawRoundedRectangle(
                    boundsOf(node).expanded(stroke), tokens_.number("radius.md") * zoom_, stroke);

            auto banner = graphArea()
                              .reduced(tokens_.integer("space.sm"))
                              .removeFromTop(tokens_.integer("metric.flux.faderHeight"));
            g.setFont(lookAndFeel_.typography().sans("font.size.caption", "font.weight.medium"));
            const auto count = sentences_.size();
            g.drawText(
                juce::String::fromUTF8("proposition du mixage : ") + juce::String(static_cast<int>(count)) +
                    juce::String::fromUTF8(count > 1 ? " réglages" : " réglage") +
                    juce::String::fromUTF8(" · le son de l'essai à blanc · garder ou refuser dans le mixer"),
                banner,
                juce::Justification::centredLeft);
        }

        // The state listened to alone, and what the person hears said.
        if (const auto bounds = boundsOf(listened_); !bounds.isEmpty())
        {
            g.setColour(tokens_.colour("color.accent.live"));
            g.drawRoundedRectangle(bounds.expanded(static_cast<float>(tokens_.integer("stroke.focus"))),
                                   tokens_.number("radius.md") * zoom_,
                                   static_cast<float>(tokens_.integer("stroke.focus")));
            auto banner = graphArea()
                              .reduced(tokens_.integer("space.sm"))
                              .removeFromTop(tokens_.integer("metric.flux.faderHeight"));
            g.setFont(lookAndFeel_.typography().sans("font.size.caption", "font.weight.medium"));
            g.drawText(juce::String::fromUTF8("écoute seule : ") + text(graph_.find(listened_)->label) +
                           juce::String::fromUTF8(" · Échap : le morceau"),
                       banner,
                       juce::Justification::centredLeft);
        }

        // What is being pulled: a line from where it started to the mouse.
        if (pulling_ != Pulling::nothing && pullMoved_)
        {
            const auto from = boundsOf(pulled_);
            const auto start = pulling_ == Pulling::effect
                                   ? from.getCentre()
                                   : juce::Point<float>{from.getRight(), from.getCentreY()};
            g.setColour(tokens_.colour("color.state.focus"));
            g.drawLine({start, pullAt_}, static_cast<float>(tokens_.integer("stroke.focus")));
        }
    }

    if (!selected_.empty())
        paintDetail(g);
}

void FluxPanel::paintLinks(juce::Graphics& g) const
{
    const auto stroke = static_cast<float>(tokens_.integer("stroke.hairline")) * std::max(1.0f, zoom_);
    const auto dash = static_cast<float>(tokens_.integer("metric.flux.dash"));
    g.setFont(scaled(lookAndFeel_.typography().sans("font.size.micro", "font.weight.regular")));

    for (const auto& link : graph_.links)
    {
        const auto path = pathOf(link);
        if (path.isEmpty())
            continue;

        if (link.kind == LinkKind::send)
        {
            g.setColour(tokens_.colour("color.text.tertiary"));
            const float pattern[] = {dash, dash};
            juce::Path dashed;
            juce::PathStrokeType{stroke}.createDashedStroke(dashed, path, pattern, 2);
            g.fillPath(dashed);
            const auto middle = path.getPointAlongPath(path.getLength() / 2.0f);
            const auto label = juce::String(link.levelDb, 1) + " dB";
            const auto width = static_cast<float>(tokens_.integer("metric.flux.faderWidth")) * zoom_;
            const auto height = static_cast<float>(tokens_.integer("metric.flux.faderHeight")) * zoom_;
            g.drawText(label,
                       juce::Rectangle<float>{width, height}.withCentre(middle),
                       juce::Justification::centred);
        }
        else
        {
            g.setColour(tokens_.colour("color.border.strong"));
            g.strokePath(path, juce::PathStrokeType{stroke});
        }
    }
}

// The text of the graph grows and shrinks with it, the detail's does not.
juce::Font FluxPanel::scaled(juce::Font font) const
{
    return font.withHeight(font.getHeight() * zoom_);
}

void FluxPanel::paintWave(juce::Graphics& g,
                          juce::Rectangle<float> area,
                          const std::vector<float>& samples,
                          juce::Colour colour) const
{
    if (samples.empty() || area.getWidth() < 1.0f)
        return;
    const auto columns = static_cast<std::size_t>(area.getWidth());
    const auto spans = domain::flux::envelopeOf(samples.data(), samples.size(), columns);
    const auto middle = area.getCentreY();
    const auto half = area.getHeight() / 2.0f;
    g.setColour(colour);
    for (std::size_t column = 0; column < spans.size(); ++column)
    {
        const auto x = area.getX() + static_cast<float>(column);
        const auto top = middle - std::clamp(spans[column].high, -1.0f, 1.0f) * half;
        const auto bottom = middle - std::clamp(spans[column].low, -1.0f, 1.0f) * half;
        g.drawVerticalLine(static_cast<int>(x), top, std::max(bottom, top + 1.0f));
    }
}

void FluxPanel::paintNode(juce::Graphics& g, const Node& node) const
{
    const auto bounds = boundsOf(node.id);
    if (!graphArea().toFloat().intersects(bounds))
        return;

    const auto radius = tokens_.number("radius.md") * zoom_;
    const bool selected = node.id == selected_;
    g.setFont(scaled(lookAndFeel_.typography().sans("font.size.micro", "font.weight.medium")));

    switch (node.kind)
    {
    case NodeKind::state:
    {
        g.setColour(tokens_.colour("color.surface.panel"));
        g.fillRoundedRectangle(bounds, radius);
        g.setColour(tokens_.colour("color.border.hairline"));
        g.drawRoundedRectangle(bounds, radius, static_cast<float>(tokens_.integer("stroke.hairline")));

        auto inner = bounds.reduced(static_cast<float>(tokens_.integer("space.xxs")));
        const auto levelHeight = static_cast<float>(tokens_.integer("metric.flux.levelHeight"));
        auto level = inner.removeFromBottom(levelHeight);
        const auto* shown = shownFor(node.id);
        if (shown != nullptr &&
            inner.getWidth() >= static_cast<float>(tokens_.integer("metric.flux.smallestWavePx")))
            paintWave(g, inner, shown->samples, tokens_.colour("color.meter.level"));

        // The level, -60 dBFS to 0, a bar under the wave.
        const auto db = shown != nullptr ? shown->levelDb : -100.0;
        const auto fill = static_cast<float>(std::clamp((db + 60.0) / 60.0, 0.0, 1.0));
        g.setColour(tokens_.colour("color.meter.track"));
        g.fillRect(level);
        g.setColour(db >= 0.0 ? tokens_.colour("color.meter.peak")
                              : tokens_.colour("color.meter.levelActive"));
        g.fillRect(level.withWidth(level.getWidth() * fill));

        // A source and a sum say whose: the others are read by their place.
        if (node.state == StateKind::source || node.state == StateKind::sum ||
            (node.state == StateKind::afterFader && node.strip == domain::ProjectState::masterTrackId()))
        {
            g.setColour(tokens_.colour("color.text.secondary"));
            g.drawText(
                text(node.label),
                bounds.translated(0.0f, -bounds.getHeight() / 2.0f).withHeight(bounds.getHeight() / 2.0f),
                juce::Justification::centredBottom,
                true);
        }
        break;
    }
    case NodeKind::effect:
    {
        g.setColour(selected ? tokens_.colour("color.state.selected")
                             : tokens_.colour("color.surface.raised"));
        g.fillRoundedRectangle(bounds, radius);
        g.setColour(selected ? tokens_.colour("color.state.focus") : tokens_.colour("color.border.strong"));
        g.drawRoundedRectangle(bounds, radius, static_cast<float>(tokens_.integer("stroke.hairline")));

        // The dot says whether it works, as on the mixer's slot; a click on
        // it turns the effect on or off.
        const auto dotArea = dotOf(bounds);
        auto inner = bounds.reduced(static_cast<float>(tokens_.integer("space.xs")) * zoom_, 0.0f);
        inner.removeFromLeft(dotArea.getWidth());
        g.setColour(node.bypassed ? tokens_.colour("color.text.disabled")
                                  : tokens_.colour("color.accent.primary"));
        if (node.bypassed)
            g.drawEllipse(dotArea, static_cast<float>(tokens_.integer("stroke.hairline")));
        else
            g.fillEllipse(dotArea);
        inner.removeFromLeft(static_cast<float>(tokens_.integer("space.xs")) * zoom_);
        g.setColour(node.bypassed ? tokens_.colour("color.text.disabled")
                                  : tokens_.colour("color.text.primary"));
        g.drawText(text(node.label), inner, juce::Justification::centredLeft, true);
        break;
    }
    case NodeKind::fader:
        g.setColour(tokens_.colour("color.surface.raised"));
        g.fillRoundedRectangle(bounds, radius);
        g.setColour(node.silent ? tokens_.colour("color.text.disabled")
                                : tokens_.colour("color.text.secondary"));
        g.drawText(text(node.label), bounds, juce::Justification::centred, true);
        break;
    }
}

void FluxPanel::paintSpectrum(juce::Graphics& g,
                              juce::Rectangle<float> area,
                              const std::vector<double>& levels,
                              juce::Colour colour) const
{
    if (levels.size() < 2)
        return;
    // -90 dB at the bottom, 0 at the top; the bands evenly across.
    juce::Path path;
    for (std::size_t band = 0; band < levels.size(); ++band)
    {
        const auto x =
            area.getX() + area.getWidth() * static_cast<float>(band) / static_cast<float>(levels.size() - 1);
        const auto y =
            area.getBottom() -
            area.getHeight() * static_cast<float>(std::clamp((levels[band] + 90.0) / 90.0, 0.0, 1.0));
        if (band == 0)
            path.startNewSubPath(x, y);
        else
            path.lineTo(x, y);
    }
    g.setColour(colour);
    g.strokePath(path, juce::PathStrokeType{static_cast<float>(tokens_.integer("stroke.focus"))});
}

void FluxPanel::paintDetail(juce::Graphics& g) const
{
    auto area = detailArea();
    g.setColour(tokens_.colour("color.surface.panel"));
    g.fillRect(area);
    g.setColour(tokens_.colour("color.border.hairline"));
    g.fillRect(area.removeFromTop(tokens_.integer("stroke.hairline")));
    area = area.reduced(tokens_.integer("space.sm"));

    const auto* effect = graph_.find(selected_);
    const auto before = shownFor(neighbour(selected_, true));
    const auto after = shownFor(neighbour(selected_, false));

    auto caption = area.removeFromTop(tokens_.integer("metric.flux.faderHeight"));
    g.setFont(lookAndFeel_.typography().sans("font.size.caption", "font.weight.medium"));
    g.setColour(tokens_.colour("color.text.tertiary"));
    g.drawText(juce::String::fromUTF8("avant"),
               caption.removeFromLeft(caption.getWidth() / 2),
               juce::Justification::centredLeft);
    g.setColour(tokens_.colour("color.accent.primary"));
    auto said = juce::String::fromUTF8("après ") + (effect != nullptr ? text(effect->label) : juce::String{});
    for (const auto& sentence : sentencesAt(selected_))
        said += juce::String::fromUTF8(" · ") + text(sentence);
    g.drawText(said, caption, juce::Justification::centredLeft, true);

    // Left, the two waveforms one over the other; right, the two spectra.
    auto waves = area.removeFromLeft(area.getWidth() / 2).toFloat();
    waves.removeFromRight(static_cast<float>(tokens_.integer("space.sm")));
    const auto spectra = area.toFloat();
    g.setColour(tokens_.colour("color.surface.sunken"));
    g.fillRect(waves);
    g.fillRect(spectra);

    if (before != nullptr)
        paintWave(g, waves, before->samples, tokens_.colour("color.flux.before"));
    if (after != nullptr)
        paintWave(g, waves, after->samples, tokens_.colour("color.flux.afterWave"));
    paintSpectrum(g, spectra, spectrumBefore_, tokens_.colour("color.flux.before"));
    paintSpectrum(g, spectra, spectrumAfter_, tokens_.colour("color.flux.after"));
}

} // namespace daw::ui
