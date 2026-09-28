#include "daw/ui/panels/GenerationPanel.h"

#include <algorithm>

namespace daw::ui
{
namespace
{

// How often the "reading" dots move. Slow enough to read as breathing, fast
// enough that a second of waiting shows four changes.
constexpr int readingTickMs = 250;

} // namespace

GenerationPanel::GenerationPanel(const Tokens& tokens, DawLookAndFeel& lookAndFeel)
    : tokens_(tokens)
    , lookAndFeel_(lookAndFeel)
{
    setLookAndFeel(&lookAndFeel_);

    field_.setFont(lookAndFeel_.typography().sans("font.size.caption", "font.weight.regular"));
    field_.setTabKeyUsedAsCharacter(false);
    field_.setMultiLine(false);
    field_.setReturnKeyStartsNewLine(false);
    field_.onKey = [this](const juce::KeyPress& key) { return onKey && onKey(key); };
    addAndMakeVisible(field_);

    previous_.setButtonText(juce::String::fromUTF8(u8"◀"));
    previous_.setTooltip(juce::String::fromUTF8(u8"La variante précédente (Alt + molette)"));
    previous_.onClick = [this]
    {
        if (onVariant)
            onVariant(-1);
    };
    addChildComponent(previous_);

    next_.setButtonText(juce::String::fromUTF8(u8"▶"));
    next_.setTooltip(juce::String::fromUTF8(u8"Une autre variante (Alt + molette)"));
    next_.onClick = [this]
    {
        if (onVariant)
            onVariant(+1);
    };
    addChildComponent(next_);

    details_.setTooltip(juce::String::fromUTF8(u8"Ce que le générateur a choisi, en détail"));
    details_.onClick = [this] { setDetailsOpen(!detailsOpen_); };
    addChildComponent(details_);

    accept_.setButtonText(juce::String::fromUTF8(u8"Valider (Tab)"));
    accept_.setTooltip(
        juce::String::fromUTF8(u8"Écrit les notes grises dans le pattern. Ctrl+Z les retire."));
    accept_.onClick = [this]
    {
        if (onAccept)
            onAccept();
    };
    addAndMakeVisible(accept_);

    close_.setButtonText(juce::String::fromUTF8(u8"Fermer"));
    close_.setTooltip(juce::String::fromUTF8(u8"Ferme sans rien écrire (Échap)"));
    close_.onClick = [this]
    {
        if (onClose)
            onClose();
    };
    addAndMakeVisible(close_);

    refreshButtons();
}

GenerationPanel::~GenerationPanel()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

void GenerationPanel::open(const juce::String& example)
{
    field_.setTextToShowWhenEmpty(example, tokens_.colour("color.text.disabled"));
    state_ = State::empty;
    shown_ = {};
    message_ = {};
    stopTimer();
    refreshButtons();
    repaint();
}

void GenerationPanel::showReading()
{
    state_ = State::reading;
    tick_ = 0;
    startTimer(readingTickMs);
    refreshButtons();
    repaint();
}

void GenerationPanel::showProposal(const Shown& shown)
{
    state_ = State::proposed;
    shown_ = shown;
    message_ = {};
    stopTimer();
    refreshButtons();
    repaint();
}

void GenerationPanel::showMessage(const juce::String& message)
{
    state_ = State::message;
    shown_ = {};
    message_ = message;
    stopTimer();
    refreshButtons();
    repaint();
}

void GenerationPanel::setDetailsOpen(bool open)
{
    if (open == detailsOpen_)
        return;
    detailsOpen_ = open;
    refreshButtons();
    if (onHeightChanged)
        onHeightChanged();
    resized();
    repaint();
}

int GenerationPanel::preferredHeight() const
{
    const auto rows = state_ == State::proposed && detailsOpen_ ? 3 : 2;
    return rows * tokens_.integer("metric.generation.rowHeight") + tokens_.integer("space.xs") * 2;
}

void GenerationPanel::timerCallback()
{
    ++tick_;
    repaint(statusRow());
}

void GenerationPanel::refreshButtons()
{
    const auto proposed = state_ == State::proposed;
    accept_.setEnabled(proposed);
    previous_.setVisible(proposed);
    next_.setVisible(proposed);
    previous_.setEnabled(proposed && shown_.rank > 0);
    details_.setVisible(proposed && shown_.details.isNotEmpty());
    details_.setButtonText(juce::String::fromUTF8(detailsOpen_ ? u8"Détails ▾" : u8"Détails ▸"));
    resized();
}

juce::Rectangle<int> GenerationPanel::statusRow() const
{
    auto area = getLocalBounds().reduced(tokens_.integer("space.sm"), tokens_.integer("space.xs"));
    const auto row = tokens_.integer("metric.generation.rowHeight");
    area.removeFromTop(row);
    return area.removeFromTop(row);
}

juce::Rectangle<int> GenerationPanel::detailsRow() const
{
    auto area = getLocalBounds().reduced(tokens_.integer("space.sm"), tokens_.integer("space.xs"));
    const auto row = tokens_.integer("metric.generation.rowHeight");
    area.removeFromTop(row * 2);
    return area.removeFromTop(row);
}

void GenerationPanel::resized()
{
    const auto gap = tokens_.integer("space.xs");
    const auto buttonWidth = tokens_.integer("metric.generation.buttonWidth");
    const auto arrowWidth = tokens_.integer("metric.generation.arrowWidth");

    auto first = getLocalBounds()
                     .reduced(tokens_.integer("space.sm"), tokens_.integer("space.xs"))
                     .removeFromTop(tokens_.integer("metric.generation.rowHeight"))
                     .withTrimmedTop(tokens_.integer("space.xxs"))
                     .withTrimmedBottom(tokens_.integer("space.xxs"));
    close_.setBounds(first.removeFromRight(buttonWidth));
    first.removeFromRight(gap);
    accept_.setBounds(first.removeFromRight(buttonWidth));
    first.removeFromRight(gap);
    field_.setBounds(first);

    auto status = statusRow()
                      .withTrimmedTop(tokens_.integer("space.xxs"))
                      .withTrimmedBottom(tokens_.integer("space.xxs"));
    if (details_.isVisible())
        details_.setBounds(status.removeFromRight(buttonWidth));
    status.removeFromRight(gap);
    if (next_.isVisible())
        next_.setBounds(status.removeFromRight(arrowWidth));
    // Room for "3 / 5" between the arrows.
    status.removeFromRight(arrowWidth * 2);
    if (previous_.isVisible())
        previous_.setBounds(status.removeFromRight(arrowWidth));
}

void GenerationPanel::paint(juce::Graphics& g)
{
    g.fillAll(tokens_.colour("color.surface.raised"));
    g.setColour(tokens_.colour("color.border.hairline"));
    g.fillRect(getLocalBounds().removeFromTop(tokens_.integer("stroke.hairline")));

    auto status = statusRow();
    const auto caption = lookAndFeel_.typography().sans("font.size.caption", "font.weight.regular");
    const auto strong = lookAndFeel_.typography().sans("font.size.caption", "font.weight.medium");
    const auto gap = tokens_.integer("space.sm");

    // The right of the row belongs to the buttons.
    auto text = status;
    if (state_ == State::proposed)
    {
        const auto reserved = tokens_.integer("metric.generation.buttonWidth") +
                              tokens_.integer("metric.generation.arrowWidth") * 4 +
                              tokens_.integer("space.xs");
        text.removeFromRight(reserved);

        auto counter = status;
        counter.removeFromRight(tokens_.integer("metric.generation.buttonWidth") +
                                tokens_.integer("metric.generation.arrowWidth") +
                                tokens_.integer("space.xs"));
        counter = counter.removeFromRight(tokens_.integer("metric.generation.arrowWidth") * 2);
        g.setFont(caption);
        g.setColour(tokens_.colour("color.text.tertiary"));
        g.drawText(juce::String(shown_.rank + 1) + " / " + juce::String(shown_.drawn),
                   counter,
                   juce::Justification::centred,
                   false);
    }

    switch (state_)
    {
    case State::empty:
        g.setFont(caption);
        g.setColour(tokens_.colour("color.text.tertiary"));
        g.drawText(juce::String::fromUTF8(u8"Dis ce que tu veux entendre dans la zone, puis Entrée."),
                   text,
                   juce::Justification::centredLeft,
                   true);
        break;

    case State::reading:
    {
        g.setFont(caption);
        g.setColour(tokens_.colour("color.text.secondary"));
        juce::String dots;
        for (int dot = 0; dot < tick_ % 4; ++dot)
            dots << ".";
        g.drawText(juce::String::fromUTF8(u8"Je lis ta demande") + dots,
                   text,
                   juce::Justification::centredLeft,
                   false);
        break;
    }

    case State::message:
        g.setFont(caption);
        g.setColour(tokens_.colour("color.text.secondary"));
        g.drawText(message_, text, juce::Justification::centredLeft, true);
        break;

    case State::proposed:
    {
        g.setFont(strong);
        g.setColour(tokens_.colour("color.actor.generator"));
        const auto width =
            std::min(text.getWidth(), juce::GlyphArrangement::getStringWidthInt(strong, shown_.sentence));
        g.drawText(shown_.sentence, text.removeFromLeft(width), juce::Justification::centredLeft, true);

        juce::String aside = shown_.notice;
        if (shown_.unused.isNotEmpty())
            aside << (aside.isEmpty() ? "" : " ") << shown_.unused;
        if (aside.isNotEmpty())
        {
            text.removeFromLeft(gap);
            g.setFont(caption);
            g.setColour(tokens_.colour("color.text.tertiary"));
            g.drawText(aside, text, juce::Justification::centredLeft, true);
        }

        if (detailsOpen_)
        {
            g.setFont(lookAndFeel_.typography().sans("font.size.micro", "font.weight.regular"));
            g.setColour(tokens_.colour("color.text.secondary"));
            g.drawText(shown_.details, detailsRow(), juce::Justification::centredLeft, true);
        }
        break;
    }
    }
}

} // namespace daw::ui
