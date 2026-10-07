#include "daw/ui/panels/KitPanel.h"

#include <algorithm>

namespace daw::ui
{
namespace
{

juce::String text(const std::string& value)
{
    return juce::String::fromUTF8(value.c_str());
}

std::string fileOf(const std::string& path)
{
    const auto slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

} // namespace

KitPanel::KitPanel(const PanelContext& context)
    : tokens_(context.tokens)
    , lookAndFeel_(context.lookAndFeel)
    , kit_(context.kit)
    , titled_(context.titled)
{
    setOpaque(true);
    kit_.addChangeListener(this);

    const auto button = [this](juce::TextButton& which, const char* label, std::function<void()> click)
    {
        which.setButtonText(juce::String::fromUTF8(label));
        which.onClick = std::move(click);
        addAndMakeVisible(which);
    };
    button(measure_, "Mesurer mes samples", [this] { measure(); });
    button(compose_, "Composer un kit", [this] { compose(); });
    button(listen_, "Écouter", [this] { listen(); });
    button(stop_, "Stop", [this] { kit_.stop(); });
    button(pose_, "Poser le kit", [this] { static_cast<void>(pose()); });
    button(direction_,
           "Axes de la direction",
           [this]
           {
               axesFromDirection_ = true;
               setAxes(kit_.directionAxes());
           });

    const auto axis = [this](juce::Label& label, juce::Slider& slider, const char* name)
    {
        label.setText(juce::String::fromUTF8(name), juce::dontSendNotification);
        label.setColour(juce::Label::textColourId, tokens_.colour("color.text.secondary"));
        addAndMakeVisible(label);
        slider.setSliderStyle(juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle(juce::Slider::TextBoxRight,
                               false,
                               tokens_.integer("metric.kit.valueWidth"),
                               tokens_.integer("metric.kit.rowHeight"));
        slider.setRange(-2.0, 2.0, 0.1);
        slider.setValue(0.0, juce::dontSendNotification);
        slider.onDragEnd = [this] { axesFromDirection_ = false; };
        addAndMakeVisible(slider);
    };
    axis(brightLabel_, bright_, "sombre ↔ brillant");
    axis(ampleLabel_, ample_, "sec ↔ ample");
    axis(dirtyLabel_, dirty_, "propre ↔ saturé");

    status_.setColour(juce::Label::textColourId, tokens_.colour("color.text.tertiary"));
    addAndMakeVisible(status_);
    setAxes(kit_.directionAxes());
    refresh();
}

KitPanel::~KitPanel()
{
    kit_.removeChangeListener(this);
}

void KitPanel::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    if (source == &kit_)
        refresh();
}

void KitPanel::refresh()
{
    auto line = text(kit_.status());
    if (kit_.stage() == KitHost::Stage::indexing)
        line += " " + juce::String(juce::roundToInt(kit_.progress() * 100.0)) + " %";
    status_.setText(line, juce::dontSendNotification);
    measure_.setButtonText(
        juce::String::fromUTF8(kit_.stage() == KitHost::Stage::indexing ? "Annuler" : "Mesurer mes samples"));
    const bool ready = kit_.stage() == KitHost::Stage::ready;
    compose_.setEnabled(ready && kit_.librarySize() > 0);
    listen_.setEnabled(kit_.kit() != nullptr);
    pose_.setEnabled(kit_.kit() != nullptr && !kit_.kit()->picks.empty());
    repaint();
}

void KitPanel::measure()
{
    if (kit_.stage() == KitHost::Stage::indexing)
        kit_.cancel();
    else
        kit_.index();
}

void KitPanel::compose()
{
    if (axesFromDirection_)
        setAxes(kit_.directionAxes());
    kit_.choose(axes());
}

void KitPanel::listen()
{
    kit_.preview();
}

void KitPanel::listenTo(std::size_t pick)
{
    kit_.listenTo(pick);
}

bool KitPanel::pose()
{
    return kit_.pose();
}

void KitPanel::setAxes(const domain::kit::Axes& axes)
{
    bright_.setValue(axes.bright, juce::dontSendNotification);
    ample_.setValue(axes.ample, juce::dontSendNotification);
    dirty_.setValue(axes.dirty, juce::dontSendNotification);
}

domain::kit::Axes KitPanel::axes() const
{
    return {bright_.getValue(), ample_.getValue(), dirty_.getValue()};
}

std::vector<std::string> KitPanel::shown() const
{
    std::vector<std::string> lines;
    if (const auto* kit = kit_.kit(); kit != nullptr)
    {
        for (const auto& pick : kit->picks)
        {
            std::string line = std::string{domain::kit::nameOf(pick.role)} + " : " + fileOf(pick.path);
            for (const auto& reason : pick.reasons)
                line += " · " + reason;
            lines.push_back(line);
        }
        for (const auto& missing : kit->missing)
            lines.push_back("manque : " + missing);
    }
    return lines;
}

juce::Rectangle<int> KitPanel::listArea() const
{
    auto area = getLocalBounds().reduced(tokens_.integer("space.sm"), 0);
    if (!titled_)
        area.removeFromTop(tokens_.integer("metric.panel.headerHeight"));
    const auto row = tokens_.integer("metric.kit.rowHeight");
    const auto gap = tokens_.integer("space.xs");
    // The buttons, the three axes, the direction, the status line.
    constexpr int controlRows = 6;
    area.removeFromTop(controlRows * (row + gap) + gap);
    return area;
}

void KitPanel::resized()
{
    auto area = getLocalBounds().reduced(tokens_.integer("space.sm"), 0);
    if (!titled_)
        area.removeFromTop(tokens_.integer("metric.panel.headerHeight"));
    const auto row = tokens_.integer("metric.kit.rowHeight");
    const auto gap = tokens_.integer("space.xs");
    const auto label = tokens_.integer("metric.kit.labelWidth");
    const auto buttonWidth = tokens_.integer("metric.kit.buttonWidth");
    area.removeFromTop(gap);

    auto buttons = area.removeFromTop(row);
    for (auto* button : {&measure_, &compose_, &listen_, &stop_, &pose_})
    {
        button->setBounds(buttons.removeFromLeft(buttonWidth));
        buttons.removeFromLeft(gap);
    }
    area.removeFromTop(gap);

    for (auto [name, slider] :
         {std::pair{&brightLabel_, &bright_}, {&ampleLabel_, &ample_}, {&dirtyLabel_, &dirty_}})
    {
        auto line = area.removeFromTop(row);
        name->setBounds(line.removeFromLeft(label));
        slider->setBounds(line);
        area.removeFromTop(gap);
    }
    direction_.setBounds(area.removeFromTop(row).removeFromLeft(buttonWidth));
    area.removeFromTop(gap);
    status_.setBounds(area.removeFromTop(row));
}

void KitPanel::paint(juce::Graphics& g)
{
    g.fillAll(tokens_.colour("color.surface.sunken"));
    if (!titled_)
    {
        auto header = getLocalBounds().removeFromTop(tokens_.integer("metric.panel.headerHeight"));
        g.setColour(tokens_.colour("color.surface.panel"));
        g.fillRect(header);
        header.removeFromLeft(tokens_.integer("space.md"));
        g.setColour(tokens_.colour("color.text.tertiary"));
        g.setFont(lookAndFeel_.typography().caps("font.size.micro"));
        g.drawText("KIT", header, juce::Justification::centredLeft);
    }

    // The elements, each with why; a click listens to one alone.
    auto area = listArea();
    const auto pickHeight = tokens_.integer("metric.kit.pickHeight");
    const auto* kit = kit_.kit();
    if (kit == nullptr)
        return;
    for (const auto& pick : kit->picks)
    {
        auto line = area.removeFromTop(pickHeight);
        g.setColour(tokens_.colour("color.surface.raised"));
        const auto inset = tokens_.integer("space.xxs");
        g.fillRect(line.withTrimmedTop(inset).withTrimmedBottom(inset));
        line.removeFromLeft(tokens_.integer("space.sm"));
        auto top = line.removeFromTop(line.getHeight() / 2);
        g.setColour(tokens_.colour("color.text.primary"));
        g.setFont(lookAndFeel_.typography().sans("font.size.body", "font.weight.medium"));
        g.drawText(text(std::string{domain::kit::nameOf(pick.role)} + " · " + fileOf(pick.path)),
                   top,
                   juce::Justification::centredLeft,
                   true);
        std::string why;
        for (const auto& reason : pick.reasons)
            why += (why.empty() ? "" : " · ") + reason;
        g.setColour(tokens_.colour("color.text.tertiary"));
        g.setFont(lookAndFeel_.typography().sans("font.size.caption", "font.weight.regular"));
        g.drawText(text(why), line, juce::Justification::centredLeft, true);
    }
    for (const auto& missing : kit->missing)
    {
        g.setColour(tokens_.colour("color.accent.danger"));
        g.drawText(text("manque : " + missing),
                   area.removeFromTop(tokens_.integer("metric.kit.rowHeight")),
                   juce::Justification::centredLeft,
                   true);
    }
}

void KitPanel::mouseDown(const juce::MouseEvent& event)
{
    const auto* kit = kit_.kit();
    const auto area = listArea();
    if (kit == nullptr || !area.contains(event.getPosition()))
        return;
    const auto index =
        static_cast<std::size_t>((event.y - area.getY()) / tokens_.integer("metric.kit.pickHeight"));
    if (index < kit->picks.size())
        listenTo(index);
}

} // namespace daw::ui
