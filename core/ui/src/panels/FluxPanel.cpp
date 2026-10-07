#include "daw/ui/panels/FluxPanel.h"

#include "daw/domain/flux/Picture.h"

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

} // namespace

FluxPanel::FluxPanel(const PanelContext& context)
    : tokens_(context.tokens)
    , lookAndFeel_(context.lookAndFeel)
    , state_(context.state)
    , project_(context.project)
    , plugins_(context.plugins)
    , flux_(context.flux)
    , titled_(context.titled)
{
    setOpaque(true);
    setWantsKeyboardFocus(true);
    project_.addChangeListener(this);
    rebuild();
}

FluxPanel::~FluxPanel()
{
    project_.removeChangeListener(this);
    disarm();
}

void FluxPanel::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    if (source == &project_)
    {
        rebuild();
        repaint();
    }
}

void FluxPanel::rebuild()
{
    graph_ = domain::flux::graphOf(
        state_, [this](const domain::PluginRef& ref) { return plugins_.isInstrument(ref); });
    if (!selected_.empty() && graph_.find(selected_) == nullptr)
        selected_.clear();
    shown_.erase(std::remove_if(shown_.begin(),
                                shown_.end(),
                                [this](const Shown& shown) { return graph_.find(shown.node) == nullptr; }),
                 shown_.end());
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
    if (node != nullptr && node->kind == NodeKind::effect)
        select(node->id);
    else if (node == nullptr)
        select({});
}

void FluxPanel::mouseDrag(const juce::MouseEvent& event)
{
    if (!dragging_)
        return;
    origin_ = originAtDrag_ - (event.position - dragStart_) / zoom_;
    repaint();
}

void FluxPanel::mouseUp(const juce::MouseEvent& event)
{
    juce::ignoreUnused(event);
    dragging_ = false;
}

void FluxPanel::mouseDoubleClick(const juce::MouseEvent& event)
{
    // An effect's editor, as from the mixer's slot.
    const auto* node = nodeAt(event.position);
    if (node != nullptr && node->kind == NodeKind::effect && plugins_.hasEditor(node->plugin))
        plugins_.openEditor(node->plugin);
}

bool FluxPanel::keyPressed(const juce::KeyPress& key)
{
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
        disarm();
}

void FluxPanel::parentHierarchyChanged()
{
    if (!isShowing())
        disarm();
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
        disarm();
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
        const auto from = boundsOf(link.from);
        const auto to = boundsOf(link.to);
        if (from.isEmpty() || to.isEmpty())
            continue;

        const juce::Point<float> start{from.getRight(), from.getCentreY()};
        const juce::Point<float> end{to.getX(), to.getCentreY()};
        // A long way stays on its row, kept free for it, up to the gap before
        // the node it reaches; there it bends down or up into it.
        juce::Path path;
        path.startNewSubPath(start);
        const auto gap = static_cast<float>(tokens_.integer("metric.flux.columnWidth") -
                                            tokens_.integer("metric.flux.stateWidth")) *
                         zoom_;
        const auto straight = std::max(start.x, end.x - gap);
        if (straight > start.x)
            path.lineTo(straight, start.y);
        const auto bend = (end.x - straight) / 2.0f;
        path.cubicTo({straight + bend, start.y}, end.translated(-bend, 0.0f), end);

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

        // The dot says whether it works, as on the mixer's slot.
        auto inner = bounds.reduced(static_cast<float>(tokens_.integer("space.xs")) * zoom_, 0.0f);
        const auto dot = static_cast<float>(tokens_.integer("metric.flux.dotSize")) * zoom_;
        const auto dotArea = inner.removeFromLeft(dot).withSizeKeepingCentre(dot, dot);
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
    g.drawText(juce::String::fromUTF8("après ") + (effect != nullptr ? text(effect->label) : juce::String{}),
               caption,
               juce::Justification::centredLeft);

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
