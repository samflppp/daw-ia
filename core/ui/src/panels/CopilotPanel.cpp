#include "daw/ui/panels/CopilotPanel.h"

#include <algorithm>
#include <cmath>

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

[[nodiscard]] juce::String utf8(const std::string& text)
{
    return juce::String::fromUTF8(text.data(), static_cast<int>(text.size()));
}

constexpr int voiceRefreshMs = 50;

} // namespace

CopilotPanel::HoldButton::HoldButton()
    : juce::TextButton(juce::String::fromUTF8("Parler"))
{
    setTooltip(juce::String::fromUTF8("Tenir pour parler (ou tenir Ctrl droit), relâcher pour envoyer"));
}

void CopilotPanel::HoldButton::mouseDown(const juce::MouseEvent& event)
{
    juce::TextButton::mouseDown(event);
    if (onPress)
        onPress();
}

void CopilotPanel::HoldButton::mouseUp(const juce::MouseEvent& event)
{
    juce::TextButton::mouseUp(event);
    if (onRelease)
        onRelease();
    if (onTap)
        onTap();
}

CopilotPanel::CopilotPanel(const PanelContext& context)
    : tokens_(context.tokens)
    , lookAndFeel_(context.lookAndFeel)
    , copilot_(context.copilot)
    , voice_(context.voice)
{
    titled_ = context.titled;
    setLookAndFeel(&lookAndFeel_);
    setOpaque(true); // paint() fills the whole rectangle: what is behind is never painted

    request_.setMultiLine(false);
    request_.setReturnKeyStartsNewLine(false);
    request_.setTextToShowWhenEmpty(u8"Demandez quelque chose, ou tenez Ctrl droit et parlez...",
                                    tokens_.colour("color.text.disabled"));
    request_.onReturnKey = [this] { sendRequest(); };
    request_.addListener(this);
    addAndMakeVisible(request_);

    send_.onClick = [this] { sendRequest(); };
    addAndMakeVisible(send_);

    restart_.onClick = [this] { copilot_.restart(); };
    addAndMakeVisible(restart_);

    talk_.onPress = [this]
    {
        if (voice_.stage() != VoiceHost::Stage::absent && voice_.stage() != VoiceHost::Stage::installing)
            voice_.press();
    };
    talk_.onRelease = [this]
    {
        if (voice_.stage() != VoiceHost::Stage::absent && voice_.stage() != VoiceHost::Stage::installing)
            voice_.release();
    };
    talk_.onTap = [this]
    {
        if (voice_.stage() == VoiceHost::Stage::absent)
            voice_.install();
    };
    addAndMakeVisible(talk_);

    confirm_.onClick = [this] { voice_.confirm(true); };
    refuse_.onClick = [this] { voice_.confirm(false); };
    addChildComponent(confirm_);
    addChildComponent(refuse_);

    copilot_.addChangeListener(this);
    voice_.addChangeListener(this);
    refresh();
}

CopilotPanel::~CopilotPanel()
{
    stopTimer();
    voice_.removeChangeListener(this);
    copilot_.removeChangeListener(this);
    request_.removeListener(this);
    setLookAndFeel(nullptr);
}

void CopilotPanel::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    juce::ignoreUnused(source);
    refresh();
}

bool CopilotPanel::voiceSpeaks() const
{
    const auto stage = voice_.stage();
    return stage == VoiceHost::Stage::opening || stage == VoiceHost::Stage::listening ||
           stage == VoiceHost::Stage::transcribing || stage == VoiceHost::Stage::sure ||
           stage == VoiceHost::Stage::confirming || stage == VoiceHost::Stage::installing;
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

    const auto stage = voice_.stage();
    talk_.setButtonText(stage == VoiceHost::Stage::absent       ? juce::String::fromUTF8("Installer la voix")
                        : stage == VoiceHost::Stage::installing ? juce::String::fromUTF8("Installation...")
                                                                : juce::String::fromUTF8("Parler"));
    talk_.setEnabled(stage != VoiceHost::Stage::installing && stage != VoiceHost::Stage::transcribing &&
                     stage != VoiceHost::Stage::confirming);
    confirm_.setVisible(stage == VoiceHost::Stage::confirming);
    refuse_.setVisible(stage == VoiceHost::Stage::confirming);

    // A phrase heard arrives in the field once, when its stage begins.
    if ((stage == VoiceHost::Stage::sure || stage == VoiceHost::Stage::doubtful) && shownStage_ != stage &&
        shownStage_ != VoiceHost::Stage::sure)
        showPhrase();
    shownStage_ = stage;

    if (voiceSpeaks())
        startTimer(voiceRefreshMs);
    else
        stopTimer();

    resized();
    repaint();
}

void CopilotPanel::timerCallback()
{
    repaint(statusArea());
}

void CopilotPanel::showPhrase()
{
    // The words heard, those the transcriber is unsure of in the colour of a
    // danger and underlined: what to read twice before it leaves.
    request_.clear();
    const auto plain = lookAndFeel_.typography().sans("font.size.caption", "font.weight.regular");
    auto marked = plain;
    marked.setUnderline(true);
    bool first = true;
    for (const auto& word : voice_.words())
    {
        request_.setFont(plain);
        request_.setColour(juce::TextEditor::textColourId, tokens_.colour("color.text.primary"));
        if (!first)
            request_.insertTextAtCaret(" ");
        first = false;
        if (word.uncertain)
        {
            request_.setFont(marked);
            request_.setColour(juce::TextEditor::textColourId, tokens_.colour("color.accent.danger"));
        }
        request_.insertTextAtCaret(utf8(word.text));
    }
    request_.setFont(plain);
    request_.setColour(juce::TextEditor::textColourId, tokens_.colour("color.text.primary"));
    placed_ = request_.getText();
    request_.grabKeyboardFocus();
}

void CopilotPanel::textEditorTextChanged(juce::TextEditor&)
{
    // Correcting a sure phrase holds it: it waits for Entrée. The change
    // notice of the phrase put there by the panel arrives later, and is not one.
    if (voice_.stage() == VoiceHost::Stage::sure && request_.getText() != placed_)
        voice_.holdPhrase();
}

void CopilotPanel::textEditorEscapeKeyPressed(juce::TextEditor&)
{
    const auto stage = voice_.stage();
    if (stage == VoiceHost::Stage::sure || stage == VoiceHost::Stage::doubtful)
    {
        voice_.dropPhrase();
        request_.clear();
    }
}

void CopilotPanel::sendRequest()
{
    const auto text = request_.getText().trim();
    if (text.isEmpty())
        return;

    request_.clear();
    // A phrase heard and shown leaves as a phrase said, corrected or not.
    const auto stage = voice_.stage();
    if (stage == VoiceHost::Stage::sure || stage == VoiceHost::Stage::doubtful)
    {
        voice_.sendPhrase(text.toStdString());
        return;
    }
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

juce::Rectangle<int> CopilotPanel::statusArea() const
{
    auto status = getLocalBounds();
    status.removeFromBottom(tokens_.integer("metric.plugin.slotHeight"));
    return status.removeFromBottom(tokens_.integer("metric.status.height"));
}

juce::String CopilotPanel::statusText() const
{
    // The voice speaks first while it has something to say.
    const auto said = utf8(voice_.message());
    switch (voice_.stage())
    {
    case VoiceHost::Stage::opening:
        return juce::String::fromUTF8("Le micro s'ouvre...");
    case VoiceHost::Stage::listening:
        return juce::String::fromUTF8("J'écoute — ") + juce::String(voice_.elapsed(), 1) + " s";
    case VoiceHost::Stage::transcribing:
        return juce::String::fromUTF8("Je transcris...");
    case VoiceHost::Stage::sure:
        return juce::String::fromUTF8("Part dans ") +
               juce::String(voice_.countdown() * VoiceHost::sureDelaySeconds, 1) +
               juce::String::fromUTF8(" s — une touche la retient, Échap l'oublie");
    case VoiceHost::Stage::installing:
        return juce::String::fromUTF8("Installation de la voix : ") +
               juce::String(juce::roundToInt(voice_.progress() * 100.0)) + " %";
    case VoiceHost::Stage::doubtful:
    case VoiceHost::Stage::confirming:
    case VoiceHost::Stage::failed:
    case VoiceHost::Stage::absent:
    case VoiceHost::Stage::idle:
    default:
        if (said.isNotEmpty())
            return said;
        break;
    }

    const auto message = copilot_.statusMessage();
    if (!message.empty())
        return utf8(message);

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
    switch (voice_.stage())
    {
    case VoiceHost::Stage::opening:
    case VoiceHost::Stage::listening:
        return tokens_.colour("color.accent.record");
    case VoiceHost::Stage::sure:
    case VoiceHost::Stage::transcribing:
        return tokens_.colour("color.accent.live");
    case VoiceHost::Stage::doubtful:
    case VoiceHost::Stage::confirming:
    case VoiceHost::Stage::failed:
        return tokens_.colour("color.accent.danger");
    case VoiceHost::Stage::absent:
    case VoiceHost::Stage::installing:
    case VoiceHost::Stage::idle:
    default:
        break;
    }

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
    if (!titled_)
    {
        g.drawText("COPILOTE", header, juce::Justification::centredLeft, false);
    }

    // The state, on its own line above the field, so that "thinking" — or
    // « j'écoute » — is read without looking for it.
    auto status = statusArea();
    const auto stage = voice_.stage();

    // While the microphone is held, its level under the line; while a sure
    // phrase waits, the time it has left.
    const auto barHeight = tokens_.integer("stroke.focus");
    auto bar = status.removeFromBottom(barHeight).reduced(tokens_.integer("space.md"), 0);
    if (stage == VoiceHost::Stage::listening || stage == VoiceHost::Stage::opening)
    {
        g.setColour(tokens_.colour("color.meter.track"));
        g.fillRect(bar);
        const auto level = std::clamp((voice_.levelDb() + 60.0f) / 60.0f, 0.0f, 1.0f);
        g.setColour(tokens_.colour("color.meter.level"));
        g.fillRect(bar.withWidth(juce::roundToInt(static_cast<float>(bar.getWidth()) * level)));
    }
    else if (stage == VoiceHost::Stage::sure)
    {
        g.setColour(tokens_.colour("color.accent.live"));
        g.fillRect(bar.withWidth(juce::roundToInt(bar.getWidth() * voice_.countdown())));
    }

    if (stage == VoiceHost::Stage::confirming)
        status.removeFromRight(tokens_.integer("space.xxl") * 4);
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

    const auto button = tokens_.integer("space.xxl") * 2;
    auto buttons = footer.removeFromRight(button * 3);
    if (restart_.isVisible())
        restart_.setBounds(buttons.removeFromRight(button));

    talk_.setBounds(buttons.removeFromRight(button));
    send_.setBounds(buttons);
    footer.removeFromRight(tokens_.integer("space.xs"));
    request_.setBounds(footer);

    // Confirm and refuse sit at the end of the state's line.
    auto status = statusArea().reduced(tokens_.integer("space.xs"), tokens_.integer("space.xxs"));
    auto choices = status.removeFromRight(button * 2);
    refuse_.setBounds(choices.removeFromRight(button));
    confirm_.setBounds(choices);
}

} // namespace daw::ui
