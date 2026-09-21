#include "daw/ui/panels/CopilotPanel.h"

#include <algorithm>

namespace daw::ui
{
namespace
{

// The colour of a line says who wrote it, the same way the history colours an
// actor: the copilot is read at a glance, not by reading.
[[nodiscard]] const char* lineColourPath(CopilotHost::Line::From from) noexcept
{
    switch (from)
    {
    case CopilotHost::Line::From::copilot:
        return "color.actor.copilot";
    case CopilotHost::Line::From::failure:
        return "color.accent.danger";
    case CopilotHost::Line::From::user:
    default:
        return "color.text.primary";
    }
}

} // namespace

CopilotPanel::CopilotPanel(const PanelContext& context)
    : tokens_(context.tokens)
    , lookAndFeel_(context.lookAndFeel)
    , copilot_(context.copilot)
{
    setLookAndFeel(&lookAndFeel_);

    request_.setMultiLine(false);
    request_.setReturnKeyStartsNewLine(false);
    request_.setTextToShowWhenEmpty(u8"Demandez quelque chose...", tokens_.colour("color.text.disabled"));
    request_.onReturnKey = [this] { sendRequest(); };
    addAndMakeVisible(request_);

    send_.onClick = [this] { sendRequest(); };
    addAndMakeVisible(send_);

    restart_.onClick = [this] { copilot_.restart(); };
    addAndMakeVisible(restart_);

    copilot_.addChangeListener(this);
    refresh();
}

CopilotPanel::~CopilotPanel()
{
    copilot_.removeChangeListener(this);
    setLookAndFeel(nullptr);
}

void CopilotPanel::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    juce::ignoreUnused(source);
    refresh();
}

void CopilotPanel::refresh()
{
    const auto status = copilot_.status();

    // While the model thinks, the field is closed and the window is not: a
    // second request on top of the first would leave two histories racing.
    const bool working = status == CopilotHost::Status::working;
    request_.setEnabled(!working);
    send_.setEnabled(!working && status != CopilotHost::Status::stopped &&
                     status != CopilotHost::Status::failed);
    restart_.setVisible(status == CopilotHost::Status::stopped || status == CopilotHost::Status::failed);

    repaint();
}

void CopilotPanel::sendRequest()
{
    const auto text = request_.getText().trim();
    if (text.isEmpty())
        return;

    request_.clear();
    copilot_.ask(text.toStdString());
}

juce::Rectangle<int> CopilotPanel::transcriptArea() const
{
    auto area = getLocalBounds();
    area.removeFromTop(tokens_.integer("metric.panel.headerHeight"));
    area.removeFromBottom(tokens_.integer("metric.plugin.slotHeight"));
    area.removeFromBottom(tokens_.integer("metric.status.height"));
    return area.reduced(tokens_.integer("space.md"), tokens_.integer("space.sm"));
}

juce::String CopilotPanel::statusText() const
{
    const auto message = copilot_.statusMessage();
    if (!message.empty())
        return juce::String::fromUTF8(message.data(), static_cast<int>(message.size()));

    switch (copilot_.status())
    {
    case CopilotHost::Status::ready:
        return u8"Prêt";
    case CopilotHost::Status::working:
        return u8"Le copilote réfléchit...";
    case CopilotHost::Status::starting:
        return u8"Démarrage...";
    case CopilotHost::Status::failed:
        return u8"Indisponible";
    case CopilotHost::Status::stopped:
    default:
        return u8"Arrêté";
    }
}

juce::Colour CopilotPanel::statusColour() const
{
    switch (copilot_.status())
    {
    case CopilotHost::Status::ready:
        return tokens_.colour("color.actor.copilot");
    case CopilotHost::Status::working:
        return tokens_.colour("color.accent.primary");
    case CopilotHost::Status::failed:
        return tokens_.colour("color.accent.danger");
    case CopilotHost::Status::starting:
    case CopilotHost::Status::stopped:
    default:
        return tokens_.colour("color.text.disabled");
    }
}

void CopilotPanel::paint(juce::Graphics& g)
{
    g.fillAll(tokens_.colour("color.surface.panel"));

    auto header = getLocalBounds().removeFromTop(tokens_.integer("metric.panel.headerHeight"));
    g.setColour(tokens_.colour("color.border.hairline"));
    g.fillRect(header.removeFromBottom(tokens_.integer("stroke.hairline")));

    header.removeFromLeft(tokens_.integer("space.md"));
    g.setColour(tokens_.colour("color.text.tertiary"));
    g.setFont(lookAndFeel_.typography().caps("font.size.micro"));
    g.drawText("COPILOTE", header, juce::Justification::centredLeft, false);

    // The state, on its own line above the field, so that "thinking" is read
    // without looking for it.
    auto status = getLocalBounds();
    status.removeFromBottom(tokens_.integer("metric.plugin.slotHeight"));
    status = status.removeFromBottom(tokens_.integer("metric.status.height"));
    status.removeFromLeft(tokens_.integer("space.md"));

    const auto dotSize = tokens_.integer("metric.history.dotSize");
    auto dot =
        status.removeFromLeft(tokens_.integer("space.md")).withSizeKeepingCentre(dotSize, dotSize).toFloat();
    g.setColour(statusColour());
    g.fillEllipse(dot);

    status.removeFromLeft(tokens_.integer("space.sm"));
    g.setColour(tokens_.colour("color.text.secondary"));
    g.setFont(lookAndFeel_.typography().sans("font.size.micro", "font.weight.regular"));
    g.drawText(statusText(), status, juce::Justification::centredLeft, true);

    const auto& lines = copilot_.transcript();
    auto area = transcriptArea();

    if (lines.empty())
    {
        g.setColour(tokens_.colour("color.text.disabled"));
        g.setFont(lookAndFeel_.typography().sans("font.size.caption", "font.weight.regular"));
        g.drawText(u8"Dites ce que vous voulez faire.", area, juce::Justification::centred, false);
        return;
    }

    // Newest last, and the oldest lines scroll out of the top: a conversation
    // is read from its end.
    const auto rowHeight = tokens_.integer("metric.history.rowHeight");
    const auto visible = std::max(1, area.getHeight() / rowHeight);
    const auto first = lines.size() > static_cast<std::size_t>(visible)
                           ? lines.size() - static_cast<std::size_t>(visible)
                           : std::size_t{0};

    g.setFont(lookAndFeel_.typography().sans("font.size.caption", "font.weight.regular"));
    for (auto index = first; index < lines.size(); ++index)
    {
        const auto& line = lines[index];
        auto row = area.removeFromTop(rowHeight);

        g.setColour(tokens_.colour(lineColourPath(line.from)));
        g.drawText(juce::String::fromUTF8(line.text.data(), static_cast<int>(line.text.size())),
                   row,
                   juce::Justification::centredLeft,
                   true);
    }
}

void CopilotPanel::resized()
{
    auto footer = getLocalBounds().removeFromBottom(tokens_.integer("metric.plugin.slotHeight"));
    footer = footer.reduced(tokens_.integer("space.xs"), tokens_.integer("space.xxs"));

    auto buttons = footer.removeFromRight(tokens_.integer("space.xxl") * 4);
    if (restart_.isVisible())
        restart_.setBounds(buttons.removeFromRight(buttons.getWidth() / 2));

    send_.setBounds(buttons);
    footer.removeFromRight(tokens_.integer("space.xs"));
    request_.setBounds(footer);
}

} // namespace daw::ui
