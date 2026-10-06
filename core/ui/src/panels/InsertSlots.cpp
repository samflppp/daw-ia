#include "daw/ui/panels/InsertSlots.h"

#include "daw/domain/commands/PluginCommands.h"
#include "daw/domain/project/InternalEffects.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>

namespace daw::ui
{
namespace
{

juce::String text(const std::string& value)
{
    return juce::String::fromUTF8(value.c_str());
}

// A number the French way: `decimals` after a comma.
std::string french(double value, int decimals = 0)
{
    char out[32];
    std::snprintf(out, sizeof(out), "%.*f", decimals, value);
    std::string written{out};
    std::replace(written.begin(), written.end(), '.', ',');
    return written;
}

// What the slot says of an effect without opening it.
std::string summaryOf(const domain::PluginInstance& instance)
{
    if (instance.ref.format == domain::PluginRef::internalFormat)
    {
        if (instance.ref.identifier == domain::internal::equaliser)
        {
            const auto highPass = domain::internalValue(instance, domain::internal::highPassFrequency);
            int bands = 0;
            for (const auto gain : {domain::internal::lowGain,
                                    domain::internal::mid1Gain,
                                    domain::internal::mid2Gain,
                                    domain::internal::highGain})
                bands += domain::internalValue(instance, gain) != 0.0 ? 1 : 0;
            std::string said;
            if (highPass > domain::internal::highPassOff)
                said = "coupe-bas " + french(highPass) + " Hz";
            if (bands > 0)
                said +=
                    (said.empty() ? "" : " · ") + std::to_string(bands) + (bands > 1 ? " bandes" : " bande");
            return said.empty() ? "plat" : said;
        }
        if (instance.ref.identifier == domain::internal::compressor)
            return "seuil " + french(domain::internalValue(instance, domain::internal::threshold), 0) +
                   " dB · " + french(domain::internalValue(instance, domain::internal::ratio), 1) + ":1";
        return {};
    }
    // A plugin of the person's: its settings mean nothing to the DAW.
    const auto touched = instance.params.size();
    if (touched == 0)
        return instance.state.digest.empty() ? "réglages d'origine" : "réglages gardés";
    return std::to_string(touched) + (touched > 1 ? " réglages touchés" : " réglage touché");
}

std::string nameOf(const domain::PluginInstance& instance)
{
    if (instance.ref.format == domain::PluginRef::internalFormat)
    {
        if (const auto* effect = domain::findInternalEffect(instance.ref.identifier); effect != nullptr)
            return std::string{effect->name};
    }
    return instance.ref.name;
}

constexpr int noneItem = 99;
constexpr int firstInternalItem = 1;
constexpr int firstInstalledItem = 100;

} // namespace

InsertSlots::InsertSlots(const Tokens& tokens,
                         DawLookAndFeel& lookAndFeel,
                         domain::CommandBus& bus,
                         const domain::ProjectState& state,
                         PluginHost& plugins,
                         domain::TrackId strip)
    : tokens_(tokens)
    , lookAndFeel_(lookAndFeel)
    , bus_(bus)
    , state_(state)
    , plugins_(plugins)
    , strip_(strip)
{
    refresh();
}

void InsertSlots::refresh()
{
    slots_.clear();
    if (const auto* strip = state_.findStrip(strip_); strip != nullptr)
    {
        for (const auto& plugin : strip->plugins)
        {
            Slot slot;
            slot.id = plugin.id;
            slot.name = nameOf(plugin);
            slot.summary = summaryOf(plugin);
            slot.bypassed = plugin.bypassed;
            slot.installed =
                plugin.ref.format == domain::PluginRef::internalFormat || plugins_.isInstalled(plugin.ref);
            slot.equaliser = plugin.ref.format == domain::PluginRef::internalFormat &&
                             plugin.ref.identifier == domain::internal::equaliser;
            slot.instance = plugin;
            slots_.push_back(std::move(slot));
        }
    }
    repaint();
}

int InsertSlots::rowHeight() const
{
    return tokens_.integer("metric.mixer.insertHeight");
}

int InsertSlots::rows() const
{
    return std::max(2, getHeight() / std::max(1, rowHeight()));
}

int InsertSlots::shownSlots() const
{
    const auto room = rows() - 1;
    const auto count = static_cast<int>(slots_.size());
    return count > room ? room - 1 : count;
}

std::optional<std::size_t> InsertSlots::slotAt(juce::Point<int> position) const
{
    const auto row = position.y / std::max(1, rowHeight());
    if (row < 0 || row >= shownSlots())
        return std::nullopt;
    return static_cast<std::size_t>(row);
}

bool InsertSlots::onDot(juce::Point<int> position) const
{
    return position.x < rowHeight();
}

bool InsertSlots::onAdd(juce::Point<int> position) const
{
    const auto row = position.y / std::max(1, rowHeight());
    const auto addRow = shownSlots() + (shownSlots() < static_cast<int>(slots_.size()) ? 1 : 0);
    return row == addRow;
}

std::vector<std::string> InsertSlots::shown() const
{
    std::vector<std::string> lines;
    for (const auto& slot : slots_)
        lines.push_back(std::string{slot.bypassed ? "○ " : "● "} + slot.name +
                        (slot.summary.empty() ? "" : " · " + slot.summary) +
                        (slot.installed ? "" : " · absent"));
    lines.emplace_back("＋ effet");
    return lines;
}

// --- the gestures ----------------------------------------------------------------

void InsertSlots::toggleBypass(std::size_t slot)
{
    if (slot < slots_.size())
        static_cast<void>(bus_.execute(
            std::make_unique<domain::SetPluginBypassed>(slots_[slot].id, !slots_[slot].bypassed)));
}

void InsertSlots::remove(std::size_t slot)
{
    if (slot < slots_.size())
        static_cast<void>(bus_.execute(std::make_unique<domain::RemovePlugin>(slots_[slot].id)));
}

void InsertSlots::move(std::size_t from, std::size_t to)
{
    if (from < slots_.size() && to < slots_.size() && from != to)
        static_cast<void>(bus_.execute(std::make_unique<domain::MovePlugin>(slots_[from].id, to)));
}

void InsertSlots::insert(const domain::PluginRef& ref)
{
    // The identifier is made here and travels in the payload: a replay
    // rebuilds this instance, not another one.
    domain::PluginInstance instance{};
    instance.id = domain::PluginId::generate();
    instance.ref = ref;
    static_cast<void>(bus_.execute(std::make_unique<domain::InsertPlugin>(strip_, instance, slots_.size())));
}

void InsertSlots::open(std::size_t slot)
{
    if (slot < slots_.size() && plugins_.hasEditor(slots_[slot].id))
        plugins_.openEditor(slots_[slot].id);
}

// --- the mouse ---------------------------------------------------------------------

void InsertSlots::mouseDown(const juce::MouseEvent& event)
{
    pressed_ = slotAt(event.getPosition());
    dropAt_.reset();
    dragging_ = false;
    if (event.mods.isPopupMenu() && pressed_.has_value())
    {
        showSlotMenu(*pressed_);
        pressed_.reset();
    }
}

void InsertSlots::mouseDrag(const juce::MouseEvent& event)
{
    if (!pressed_.has_value() || event.getDistanceFromDragStart() < rowHeight() / 2)
        return;
    dragging_ = true;
    const auto row =
        std::clamp(event.getPosition().y / std::max(1, rowHeight()), 0, std::max(0, shownSlots() - 1));
    dropAt_ = static_cast<std::size_t>(row);
    repaint();
}

void InsertSlots::mouseUp(const juce::MouseEvent& event)
{
    const auto pressed = pressed_;
    const auto dropAt = dropAt_;
    const auto dragged = dragging_;
    pressed_.reset();
    dropAt_.reset();
    dragging_ = false;
    repaint();

    if (event.mods.isPopupMenu())
        return;
    if (dragged && pressed.has_value() && dropAt.has_value())
    {
        move(*pressed, *dropAt);
        return;
    }
    if (pressed.has_value() && onDot(event.getPosition()) && slotAt(event.getPosition()) == pressed)
    {
        toggleBypass(*pressed);
        return;
    }
    if (!pressed.has_value() && onAdd(event.getPosition()))
        showAddMenu();
}

void InsertSlots::mouseDoubleClick(const juce::MouseEvent& event)
{
    if (const auto slot = slotAt(event.getPosition()); slot.has_value() && !onDot(event.getPosition()))
        open(*slot);
}

void InsertSlots::showSlotMenu(std::size_t slot)
{
    if (slot >= slots_.size())
        return;
    juce::PopupMenu menu;
    menu.addItem(1, juce::String::fromUTF8("Ouvrir"), plugins_.hasEditor(slots_[slot].id));
    menu.addItem(2, juce::String::fromUTF8(slots_[slot].bypassed ? "Rétablir" : "Contourner"));
    menu.addSeparator();
    menu.addItem(3, juce::String::fromUTF8("Retirer"));
    const auto id = slots_[slot].id;
    menu.showMenuAsync(juce::PopupMenu::Options{}.withTargetComponent(this),
                       [this, id](int choice)
                       {
                           // Found again by its identifier: the chain may have
                           // changed while the menu was open.
                           for (std::size_t index = 0; index < slots_.size(); ++index)
                           {
                               if (slots_[index].id != id)
                                   continue;
                               if (choice == 1)
                                   open(index);
                               else if (choice == 2)
                                   toggleBypass(index);
                               else if (choice == 3)
                                   remove(index);
                               return;
                           }
                       });
}

void InsertSlots::showAddMenu()
{
    juce::PopupMenu menu;
    std::vector<domain::PluginRef> refs;

    menu.addSectionHeader(juce::String::fromUTF8("Les effets de DAW IA"));
    for (const auto& effect : domain::internalEffects())
    {
        refs.push_back(domain::PluginRef{std::string{domain::PluginRef::internalFormat},
                                         std::string{effect.identifier},
                                         std::string{effect.name}});
        menu.addItem(firstInternalItem + static_cast<int>(refs.size()) - 1, text(std::string{effect.name}));
    }

    std::vector<domain::PluginRef> installed;
    for (const auto& ref : plugins_.available())
    {
        if (plugins_.isInstalled(ref) && !plugins_.isInstrument(ref))
            installed.push_back(ref);
    }
    std::sort(
        installed.begin(), installed.end(), [](const auto& a, const auto& b) { return a.name < b.name; });
    menu.addSectionHeader(juce::String::fromUTF8("Tes effets"));
    if (installed.empty())
        menu.addItem(noneItem, juce::String::fromUTF8("Aucun effet installé trouvé"), false);
    for (std::size_t index = 0; index < installed.size(); ++index)
        menu.addItem(firstInstalledItem + static_cast<int>(index), text(installed[index].name));

    menu.showMenuAsync(juce::PopupMenu::Options{}.withTargetComponent(this),
                       [this, refs, installed](int choice)
                       {
                           if (choice >= firstInstalledItem &&
                               choice < firstInstalledItem + static_cast<int>(installed.size()))
                               insert(installed[static_cast<std::size_t>(choice - firstInstalledItem)]);
                           else if (choice >= firstInternalItem &&
                                    choice < firstInternalItem + static_cast<int>(refs.size()))
                               insert(refs[static_cast<std::size_t>(choice - firstInternalItem)]);
                       });
}

// --- painting ----------------------------------------------------------------------

void InsertSlots::drawCurve(juce::Graphics& g,
                            const domain::PluginInstance& instance,
                            juce::Rectangle<int> area) const
{
    // 20 Hz to 20 kHz on a log scale, ±12 dB over the height: the curve the
    // engine plays, computed by the domain (equaliserGainDb).
    juce::Path curve;
    const auto width = std::max(2, area.getWidth());
    for (int x = 0; x < width; ++x)
    {
        const auto frequency = 20.0 * std::pow(1000.0, static_cast<double>(x) / (width - 1));
        const auto gain = std::clamp(domain::equaliserGainDb(instance, frequency), -12.0, 12.0);
        const auto y = static_cast<float>(area.getCentreY()) -
                       static_cast<float>(gain / 12.0) * (static_cast<float>(area.getHeight()) / 2.0f);
        if (x == 0)
            curve.startNewSubPath(static_cast<float>(area.getX()), y);
        else
            curve.lineTo(static_cast<float>(area.getX() + x), y);
    }
    g.strokePath(curve, juce::PathStrokeType{static_cast<float>(tokens_.integer("stroke.hairline"))});
}

void InsertSlots::paint(juce::Graphics& g)
{
    const auto row = rowHeight();
    const auto shown = shownSlots();
    g.setFont(lookAndFeel_.typography().sans("font.size.micro", "font.weight.regular"));

    for (int index = 0; index < shown; ++index)
    {
        const auto& slot = slots_[static_cast<std::size_t>(index)];
        auto area = juce::Rectangle<int>{0, index * row, getWidth(), row};
        if (pressed_ == static_cast<std::size_t>(index) && dragging_)
        {
            g.setColour(tokens_.colour("color.state.pressed"));
            g.fillRect(area);
        }

        auto dot = area.removeFromLeft(row).reduced(row / 4).toFloat();
        g.setColour(tokens_.colour(slot.bypassed ? "color.text.disabled" : "color.accent.primary"));
        if (slot.bypassed)
            g.drawEllipse(dot, static_cast<float>(tokens_.integer("stroke.hairline")));
        else
            g.fillEllipse(dot);

        const auto textColour = !slot.installed ? "color.accent.danger"
                                : slot.bypassed ? "color.text.disabled"
                                                : "color.text.primary";
        if (slot.equaliser)
        {
            auto curveArea =
                area.removeFromRight(area.getWidth() / 2).withTrimmedTop(row / 6).withTrimmedBottom(row / 6);
            g.setColour(tokens_.colour(slot.bypassed ? "color.text.disabled" : "color.accent.primary"));
            drawCurve(g, slot.instance, curveArea);
            g.setColour(tokens_.colour(textColour));
            g.drawText(text(slot.name), area, juce::Justification::centredLeft, true);
        }
        else
        {
            g.setColour(tokens_.colour(textColour));
            g.drawText(text(slot.name), area, juce::Justification::centredLeft, true);
            g.setColour(tokens_.colour("color.text.tertiary"));
            g.drawText(text(slot.installed ? slot.summary : "absent"),
                       area,
                       juce::Justification::centredRight,
                       true);
        }
    }

    auto next = shown;
    if (shown < static_cast<int>(slots_.size()))
    {
        g.setColour(tokens_.colour("color.text.tertiary"));
        g.drawText(juce::String::fromUTF8("… ") + juce::String(static_cast<int>(slots_.size()) - shown) +
                       juce::String::fromUTF8(" de plus, page Plugins"),
                   juce::Rectangle<int>{0, next * row, getWidth(), row},
                   juce::Justification::centredLeft,
                   true);
        ++next;
    }

    g.setColour(tokens_.colour("color.text.secondary"));
    g.drawText(juce::String::fromUTF8("＋ effet"),
               juce::Rectangle<int>{0, next * row, getWidth(), row}.withTrimmedLeft(row / 4),
               juce::Justification::centredLeft,
               true);

    // Where a dragged effect will land.
    if (dragging_ && dropAt_.has_value())
    {
        const auto down = pressed_.has_value() && *dropAt_ > *pressed_;
        const auto y = static_cast<int>(*dropAt_) * row + (down ? row : 0);
        g.setColour(tokens_.colour("color.state.focus"));
        g.fillRect(
            0, y - tokens_.integer("stroke.hairline"), getWidth(), 2 * tokens_.integer("stroke.hairline"));
    }
}

} // namespace daw::ui
