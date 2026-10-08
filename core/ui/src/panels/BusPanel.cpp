#include "daw/ui/panels/BusPanel.h"

#include <algorithm>

namespace daw::ui
{
namespace
{

juce::String text(const std::string& value)
{
    return juce::String::fromUTF8(value.c_str());
}

} // namespace

BusPanel::BusPanel(const PanelContext& context)
    : tokens_(context.tokens)
    , lookAndFeel_(context.lookAndFeel)
    , buses_(context.buses)
    , titled_(context.titled)
{
    setOpaque(true);
    buses_.addChangeListener(this);

    const auto button = [this](juce::TextButton& which, const char* label, std::function<void()> click)
    {
        which.setButtonText(juce::String::fromUTF8(label));
        which.onClick = std::move(click);
        addAndMakeVisible(which);
    };
    button(search_, "Chercher", [this] { buses_.propose(); });
    button(tryOut_,
           "Essayer à blanc",
           [this]
           {
               if (choice_.getSelectedItemIndex() >= 0)
                   buses_.tryOut(static_cast<std::size_t>(choice_.getSelectedItemIndex()));
           });
    button(before_, "Avant", [this] { buses_.listen(false); });
    button(after_, "Après", [this] { buses_.listen(true); });
    button(stop_, "Stop", [this] { buses_.stopListening(); });
    button(keep_, "Garder", [this] { static_cast<void>(buses_.keep()); });
    button(refuse_, "Refuser", [this] { buses_.refuse(); });
    button(reverb_, "Réverbération", [this] { answer(domain::buses::Kind::reverb); });
    button(delay_, "Écho", [this] { answer(domain::buses::Kind::delay); });
    button(neither_, "Ni l'un ni l'autre", [this] { answer(domain::buses::Kind::other); });
    choice_.onChange = [this] { enableForChoice(); };
    addAndMakeVisible(choice_);
    status_.setColour(juce::Label::textColourId, tokens_.colour("color.text.tertiary"));
    addAndMakeVisible(status_);
    refresh();
}

BusPanel::~BusPanel()
{
    buses_.removeChangeListener(this);
}

void BusPanel::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    if (source == &buses_)
        refresh();
}

void BusPanel::refresh()
{
    const auto selected = choice_.getSelectedItemIndex();
    choice_.clear(juce::dontSendNotification);
    const auto& proposals = buses_.proposals();
    for (std::size_t index = 0; index < proposals.size(); ++index)
        choice_.addItem(text(proposals[index].sentence), static_cast<int>(index) + 1);
    if (!proposals.empty())
        choice_.setSelectedItemIndex(std::clamp(selected, 0, static_cast<int>(proposals.size()) - 1),
                                     juce::dontSendNotification);

    auto line = text(buses_.status());
    if (buses_.stage() == BusHost::Stage::trying)
        line += " " + juce::String(juce::roundToInt(buses_.progress() * 100.0)) + " %";
    status_.setText(line, juce::dontSendNotification);
    const bool tried = buses_.stage() == BusHost::Stage::tried;
    for (auto* each : {&before_, &after_, &stop_, &keep_, &refuse_})
        each->setEnabled(tried);
    enableForChoice();
    repaint();
}

void BusPanel::enableForChoice()
{
    const auto& proposals = buses_.proposals();
    const auto index = choice_.getSelectedItemIndex();
    const auto* chosen = index >= 0 && static_cast<std::size_t>(index) < proposals.size()
                             ? &proposals[static_cast<std::size_t>(index)]
                             : nullptr;
    const bool idle = buses_.stage() != BusHost::Stage::trying;
    const bool asked = chosen != nullptr && chosen->toAsk();
    const bool send = chosen != nullptr && chosen->way == domain::buses::Way::send;
    tryOut_.setEnabled(chosen != nullptr && !asked && idle);
    reverb_.setEnabled(asked && idle);
    delay_.setEnabled(asked && idle);
    neither_.setEnabled(send && idle);
}

void BusPanel::answer(domain::buses::Kind kind)
{
    if (const auto index = choice_.getSelectedItemIndex(); index >= 0)
        buses_.answer(static_cast<std::size_t>(index), kind);
}

std::vector<std::string> BusPanel::shown() const
{
    std::vector<std::string> lines;
    for (const auto& proposal : buses_.proposals())
        lines.push_back(proposal.sentence);
    if (const auto* result = buses_.result(); result != nullptr)
        for (const auto& said : result->said)
            lines.push_back(said);
    return lines;
}

void BusPanel::resized()
{
    auto area = getLocalBounds().reduced(tokens_.integer("space.sm"), 0);
    if (!titled_)
        area.removeFromTop(tokens_.integer("metric.panel.headerHeight"));
    const auto row = tokens_.integer("metric.kit.rowHeight");
    const auto gap = tokens_.integer("space.xs");
    const auto width = tokens_.integer("metric.kit.buttonWidth");
    area.removeFromTop(gap);

    auto top = area.removeFromTop(row);
    search_.setBounds(top.removeFromLeft(width));
    top.removeFromLeft(gap);
    choice_.setBounds(top);
    area.removeFromTop(gap);

    auto buttons = area.removeFromTop(row);
    for (auto* each : {&tryOut_, &before_, &after_, &stop_, &keep_, &refuse_})
    {
        each->setBounds(buttons.removeFromLeft(width));
        buttons.removeFromLeft(gap);
    }
    area.removeFromTop(gap);

    auto answers = area.removeFromTop(row);
    for (auto* each : {&reverb_, &delay_, &neither_})
    {
        each->setBounds(answers.removeFromLeft(width));
        answers.removeFromLeft(gap);
    }
    area.removeFromTop(gap);
    status_.setBounds(area.removeFromTop(row));
}

void BusPanel::paint(juce::Graphics& g)
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
        g.drawText("BUS", header, juce::Justification::centredLeft);
    }

    // What the dry run said, a line each, under the controls.
    const auto* result = buses_.result();
    if (result == nullptr)
        return;
    auto area = status_.getBounds().withTrimmedTop(status_.getHeight());
    const auto row = tokens_.integer("metric.kit.rowHeight");
    g.setFont(lookAndFeel_.typography().sans("font.size.caption", "font.weight.regular"));
    g.setColour(tokens_.colour("color.text.secondary"));
    for (const auto& said : result->said)
        g.drawFittedText(text(said), area.removeFromTop(row + row), juce::Justification::topLeft, 2);
}

} // namespace daw::ui
