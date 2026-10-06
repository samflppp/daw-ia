#include "daw/ui/panels/AudioPanel.h"

#include "daw/ui/DawLookAndFeel.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <utility>

namespace daw::ui
{
namespace
{

// The measures change while the card plays; four times a second is enough
// to read them and costs nothing.
constexpr int refreshMs = 250;

juce::String text(const std::string& value)
{
    return juce::String::fromUTF8(value.c_str());
}

// Milliseconds the French way: one decimal, a comma.
std::string milliseconds(double seconds)
{
    char out[32];
    std::snprintf(out, sizeof(out), "%.1f", seconds * 1000.0);
    std::string value{out};
    std::replace(value.begin(), value.end(), '.', ',');
    return value + " ms";
}

} // namespace

AudioPanel::AudioPanel(const PanelContext& context)
    : tokens_(context.tokens)
    , lookAndFeel_(context.lookAndFeel)
    , audio_(context.audio)
    , titled_(context.titled)
{
    setLookAndFeel(&lookAndFeel_);
    setOpaque(true); // paint() fills the whole rectangle

    typeLabel_.setText(juce::String::fromUTF8("Pilote"), juce::dontSendNotification);
    outputLabel_.setText(juce::String::fromUTF8("Sortie"), juce::dontSendNotification);
    bufferLabel_.setText(juce::String::fromUTF8("Tampon"), juce::dontSendNotification);
    for (auto* label : {&typeLabel_, &outputLabel_, &bufferLabel_})
        label->setColour(juce::Label::textColourId, tokens_.colour("color.text.secondary"));
    for (auto* label : {&rate_, &latency_, &blocks_, &advice_})
    {
        label->setColour(juce::Label::textColourId, tokens_.colour("color.text.primary"));
        label->setJustificationType(juce::Justification::topLeft);
        label->setMinimumHorizontalScale(1.0f);
    }
    said_.setColour(juce::Label::textColourId, tokens_.colour("color.text.tertiary"));
    said_.setJustificationType(juce::Justification::topLeft);
    said_.setMinimumHorizontalScale(1.0f);

    type_.onChange = [this]
    {
        const auto index = type_.getSelectedItemIndex();
        if (index >= 0 && index < static_cast<int>(types_.size()))
            chooseType(types_[static_cast<std::size_t>(index)]);
    };
    output_.onChange = [this]
    {
        const auto index = output_.getSelectedItemIndex();
        const auto now = audio_.current();
        if (index >= 0 && index < static_cast<int>(outputs_.size()) &&
            outputs_[static_cast<std::size_t>(index)] != now.output)
        {
            const auto& output = outputs_[static_cast<std::size_t>(index)];
            applyFrom(
                now.type, output, domain::live::closestBuffer(audio_.buffers(now.type, output), now.buffer));
        }
    };
    buffer_.onChange = [this]
    {
        const auto index = buffer_.getSelectedItemIndex();
        if (index >= 0 && index < static_cast<int>(buffers_.size()))
            chooseBuffer(buffers_[static_cast<std::size_t>(index)]);
    };
    takeAdvice_.setButtonText(juce::String::fromUTF8("Appliquer le conseil"));
    takeAdvice_.onClick = [this] { takeAdvice(); };
    trial_.onClick = [this]
    {
        if (audio_.trialRunning())
            audio_.cancelTrial();
        else
            startTrial();
    };

    for (auto* component : std::initializer_list<juce::Component*>{&typeLabel_,
                                                                   &type_,
                                                                   &outputLabel_,
                                                                   &output_,
                                                                   &bufferLabel_,
                                                                   &buffer_,
                                                                   &rate_,
                                                                   &latency_,
                                                                   &blocks_,
                                                                   &advice_,
                                                                   &takeAdvice_,
                                                                   &trial_,
                                                                   &said_})
        addAndMakeVisible(*component);

    audio_.addChangeListener(this);
    refresh();
    startTimer(refreshMs);
}

AudioPanel::~AudioPanel()
{
    stopTimer();
    audio_.removeChangeListener(this);
    setLookAndFeel(nullptr);
}

void AudioPanel::changeListenerCallback(juce::ChangeBroadcaster*)
{
    refresh();
}

void AudioPanel::timerCallback()
{
    refreshMeasures();
}

void AudioPanel::chooseType(const std::string& type)
{
    const auto now = audio_.current();
    if (type == now.type)
        return;
    // The same output in the new driver when it has it, its default otherwise.
    const auto outputs = audio_.outputs(type);
    auto output = now.output;
    if (std::find(outputs.begin(), outputs.end(), output) == outputs.end())
        output = outputs.empty() ? std::string{} : outputs.front();
    applyFrom(type, output, domain::live::closestBuffer(audio_.buffers(type, output), now.buffer));
}

void AudioPanel::chooseBuffer(int samples)
{
    const auto now = audio_.current();
    if (samples != now.buffer)
        applyFrom(now.type, now.output, samples);
}

void AudioPanel::takeAdvice()
{
    const auto advice = audio_.advice();
    if (!advice.already)
        applyFrom(advice.type, audio_.current().output, advice.buffer);
}

void AudioPanel::startTrial()
{
    audio_.startTrial();
    refresh();
}

void AudioPanel::applyFrom(const std::string& type, const std::string& output, int buffer)
{
    audio_.apply(AudioHost::Choice{type, output, buffer});
}

void AudioPanel::refresh()
{
    const auto now = audio_.current();

    types_ = audio_.types();
    type_.clear(juce::dontSendNotification);
    for (std::size_t index = 0; index < types_.size(); ++index)
        type_.addItem(text(types_[index]), static_cast<int>(index) + 1);
    if (const auto at = std::find(types_.begin(), types_.end(), now.type); at != types_.end())
        type_.setSelectedItemIndex(static_cast<int>(at - types_.begin()), juce::dontSendNotification);

    outputs_ = audio_.outputs(now.type);
    output_.clear(juce::dontSendNotification);
    for (std::size_t index = 0; index < outputs_.size(); ++index)
        output_.addItem(text(outputs_[index]), static_cast<int>(index) + 1);
    if (const auto at = std::find(outputs_.begin(), outputs_.end(), now.output); at != outputs_.end())
        output_.setSelectedItemIndex(static_cast<int>(at - outputs_.begin()), juce::dontSendNotification);

    buffers_ = audio_.buffers(now.type, now.output);
    buffer_.clear(juce::dontSendNotification);
    const auto rate = audio_.sampleRate();
    for (std::size_t index = 0; index < buffers_.size(); ++index)
    {
        const auto size = buffers_[index];
        auto label = std::to_string(size) + " échantillons";
        if (rate > 0.0)
            label += " (" + milliseconds(size / rate) + ")";
        buffer_.addItem(text(label), static_cast<int>(index) + 1);
    }
    if (const auto at = std::find(buffers_.begin(), buffers_.end(), now.buffer); at != buffers_.end())
        buffer_.setSelectedItemIndex(static_cast<int>(at - buffers_.begin()), juce::dontSendNotification);

    const auto advice = audio_.advice();
    advice_.setText(text("Conseil : " + advice.sentence), juce::dontSendNotification);
    takeAdvice_.setEnabled(!advice.already && !audio_.trialRunning());
    for (auto* choice : {&type_, &output_, &buffer_})
        choice->setEnabled(!audio_.trialRunning());
    said_.setText(text(audio_.said()), juce::dontSendNotification);
    refreshMeasures();
}

void AudioPanel::refreshMeasures()
{
    const auto now = audio_.current();
    const auto rate = audio_.sampleRate();
    std::string frequency =
        rate > 0.0 ? std::to_string(static_cast<int>(rate)) + " Hz" : "aucune carte ouverte";
    if (rate > 0.0 && now.buffer > 0)
        frequency += " · tampon de " + milliseconds(now.buffer / rate);
    rate_.setText(text("Fréquence : " + frequency), juce::dontSendNotification);

    const auto latency = audio_.latency();
    latency_.setText(
        text(domain::live::describeLatency(latency.playSeconds, latency.playMeasured, latency.outputSeconds)),
        juce::dontSendNotification);

    const auto& blocks = latency.blocks;
    std::string line;
    if (blocks.blocks == 0)
        line = "Aucun bloc mesuré : la carte ne joue pas.";
    else
        line = "Mesuré : " + std::to_string(blocks.lastSize) + " échantillons toutes les " +
               milliseconds(blocks.meanSeconds) + " (au pire " + milliseconds(blocks.worstSeconds) + "), " +
               std::to_string(blocks.late) + (blocks.late > 1 ? " décrochages" : " décrochage") + ".";
    blocks_.setText(text(line), juce::dontSendNotification);

    // The trial's button: its progress while it runs, why it cannot run.
    if (audio_.trialRunning())
    {
        trial_.setButtonText(juce::String::fromUTF8("Annuler l'essai (") +
                             juce::String(static_cast<int>(audio_.trialProgress() * 100.0)) + " %)");
        trial_.setEnabled(true);
        trial_.setTooltip({});
    }
    else
    {
        const auto why = audio_.whyNoTrial();
        trial_.setButtonText(juce::String::fromUTF8("Tester ma carte (≈ 30 s, son coupé)"));
        trial_.setEnabled(why.empty());
        trial_.setTooltip(text(why));
    }
}

void AudioPanel::paint(juce::Graphics& g)
{
    g.fillAll(tokens_.colour("color.surface.sunken"));

    auto header = getLocalBounds().removeFromTop(tokens_.integer("metric.panel.headerHeight"));
    g.setColour(tokens_.colour("color.surface.panel"));
    g.fillRect(header);
    g.setColour(tokens_.colour("color.border.hairline"));
    g.fillRect(header.removeFromBottom(tokens_.integer("stroke.hairline")));
    if (!titled_)
    {
        header.removeFromLeft(tokens_.integer("space.md"));
        g.setColour(tokens_.colour("color.text.tertiary"));
        g.setFont(lookAndFeel_.typography().caps("font.size.micro"));
        g.drawText("AUDIO", header, juce::Justification::centredLeft, false);
    }
}

void AudioPanel::resized()
{
    const auto row = tokens_.integer("metric.audio.rowHeight");
    const auto gap = tokens_.integer("space.xs");
    const auto label = tokens_.integer("metric.audio.labelWidth");
    const auto button = tokens_.integer("metric.audio.buttonWidth");

    auto area = getLocalBounds().reduced(tokens_.integer("space.sm"), 0);
    area.removeFromTop(tokens_.integer("metric.panel.headerHeight") + gap);

    for (auto [name, choice] : {std::pair{&typeLabel_, &type_},
                                std::pair{&outputLabel_, &output_},
                                std::pair{&bufferLabel_, &buffer_}})
    {
        auto line = area.removeFromTop(row);
        name->setBounds(line.removeFromLeft(label));
        choice->setBounds(line);
        area.removeFromTop(gap);
    }
    rate_.setBounds(area.removeFromTop(row));
    area.removeFromTop(gap);
    latency_.setBounds(area.removeFromTop(row * 2));
    blocks_.setBounds(area.removeFromTop(row));
    area.removeFromTop(gap);
    advice_.setBounds(area.removeFromTop(row * 2));
    auto buttons = area.removeFromTop(row);
    takeAdvice_.setBounds(buttons.removeFromLeft(button));
    buttons.removeFromLeft(gap);
    trial_.setBounds(buttons.removeFromLeft(button * 3 / 2));
    area.removeFromTop(gap);
    said_.setBounds(area.removeFromTop(row * 2));
}

} // namespace daw::ui
