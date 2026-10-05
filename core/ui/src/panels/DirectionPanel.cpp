#include "daw/ui/panels/DirectionPanel.h"

#include "daw/domain/direction/Direction.h"
#include "daw/ui/DawLookAndFeel.h"
#include "daw/ui/model/ProjectObserver.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace daw::ui
{
namespace
{

juce::String text(const std::string& value)
{
    return juce::String::fromUTF8(value.c_str());
}

// The key menu: item 1 gives the key back to the references, items 2 to 25
// are the 24 keys, tonic by tonic, major then minor.
constexpr int fromReferencesItem = 1;

int itemOf(domain::generation::Key key)
{
    return 2 + key.tonic * 2 + (key.mode == domain::generation::Mode::minor ? 1 : 0);
}

domain::generation::Key keyOf(int item)
{
    const auto index = item - 2;
    return domain::generation::Key{
        index / 2, index % 2 == 1 ? domain::generation::Mode::minor : domain::generation::Mode::major};
}

std::string percent(double share)
{
    return std::to_string(static_cast<int>(std::lround(share * 100.0))) + " %";
}

} // namespace

// One reference: its name and what was read, its weight, a way out.
class DirectionPanel::Row final : public juce::Component
{
public:
    Row(DirectionPanel& panel, const domain::direction::Reference& reference)
        : panel_{panel}
        , digest_{reference.reading.digest}
    {
        const auto& reading = reference.reading;
        std::string read = reading.name;
        read += reading.bpm ? " · " + std::to_string(static_cast<int>(std::lround(*reading.bpm))) + " BPM"
                            : " · tempo ?";
        read += reading.key ? " · " + domain::generation::describe(*reading.key) : " · tonalité ?";
        name_.setText(text(read), juce::dontSendNotification);
        name_.setColour(juce::Label::textColourId, panel.tokens_.colour("color.text.primary"));
        addAndMakeVisible(name_);

        weight_.setSliderStyle(juce::Slider::LinearHorizontal);
        weight_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        weight_.setRange(0.25, 4.0);
        weight_.setSkewFactorFromMidPoint(1.0);
        weight_.setValue(reference.weight, juce::dontSendNotification);
        weight_.setTooltip(juce::String::fromUTF8("Poids de cette référence"));
        weight_.onDragEnd = [this] { panel_.direction_.setWeight(digest_, weight_.getValue()); };
        addAndMakeVisible(weight_);

        remove_.setButtonText(juce::String::fromUTF8("Retirer"));
        remove_.onClick = [this] { panel_.direction_.removeReference(digest_); };
        addAndMakeVisible(remove_);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        remove_.setBounds(area.removeFromRight(panel_.tokens_.integer("metric.generation.buttonWidth")));
        weight_.setBounds(area.removeFromRight(panel_.tokens_.integer("metric.direction.labelWidth")));
        name_.setBounds(area);
    }

private:
    DirectionPanel& panel_;
    std::string digest_;
    juce::Label name_;
    juce::Slider weight_;
    juce::TextButton remove_;
};

DirectionPanel::DirectionPanel(const PanelContext& context)
    : tokens_(context.tokens)
    , lookAndFeel_(context.lookAndFeel)
    , state_(context.state)
    , project_(context.project)
    , direction_(context.direction)
    , titled_(context.titled)
{
    setLookAndFeel(&lookAndFeel_);
    setOpaque(true); // paint() fills the whole rectangle

    add_.setButtonText(juce::String::fromUTF8("Ajouter une référence…"));
    add_.onClick = [this] { chooseReference(); };
    cancel_.setButtonText(juce::String::fromUTF8("Annuler"));
    cancel_.onClick = [this] { direction_.cancel(); };
    clear_.setButtonText(juce::String::fromUTF8("Sans direction"));
    clear_.onClick = [this] { direction_.clear(); };
    status_.setColour(juce::Label::textColourId, tokens_.colour("color.text.tertiary"));
    for (auto* component :
         std::initializer_list<juce::Component*>{&add_, &cancel_, &clear_, &status_, &progressBar_})
        addAndMakeVisible(*component);

    tempoEdit_.setTextToShowWhenEmpty(juce::String::fromUTF8("tempo à la main"),
                                      tokens_.colour("color.text.tertiary"));
    tempoEdit_.setInputRestrictions(6, "0123456789.,");
    tempoEdit_.onReturnKey = [this] { typeTempo(tempoEdit_.getText()); };
    tempoAuto_.setButtonText("auto");
    tempoAuto_.onClick = [this] { direction_.correctTempo(std::nullopt); };

    keyChoice_.addItem(juce::String::fromUTF8("d'après les références"), fromReferencesItem);
    for (int tonic = 0; tonic < 12; ++tonic)
        for (const auto mode : {domain::generation::Mode::major, domain::generation::Mode::minor})
        {
            const domain::generation::Key key{tonic, mode};
            keyChoice_.addItem(text(domain::generation::describe(key)), itemOf(key));
        }
    keyChoice_.onChange = [this] { chooseKey(keyChoice_.getSelectedId()); };

    contradictions_.setColour(juce::Label::textColourId, tokens_.colour("color.accent.danger"));
    amountLabel_.setText(juce::String::fromUTF8("Part de direction"), juce::dontSendNotification);
    amountLabel_.setColour(juce::Label::textColourId, tokens_.colour("color.text.secondary"));
    amount_.setSliderStyle(juce::Slider::LinearHorizontal);
    amount_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    amount_.setRange(0.0, 1.0);
    amount_.onDragEnd = [this] { direction_.setAmount(amount_.getValue()); };

    for (auto* component : std::initializer_list<juce::Component*>{&tempo_,
                                                                   &tempoEdit_,
                                                                   &tempoAuto_,
                                                                   &key_,
                                                                   &keyChoice_,
                                                                   &contradictions_,
                                                                   &amountLabel_,
                                                                   &amount_})
        addAndMakeVisible(*component);
    for (auto* label : {&tempo_, &key_})
        label->setColour(juce::Label::textColourId, tokens_.colour("color.text.primary"));

    project_.addChangeListener(this);
    direction_.addChangeListener(this);
    refresh();
}

DirectionPanel::~DirectionPanel()
{
    direction_.removeChangeListener(this);
    project_.removeChangeListener(this);
    setLookAndFeel(nullptr);
}

void DirectionPanel::changeListenerCallback(juce::ChangeBroadcaster*)
{
    refresh();
}

void DirectionPanel::chooseReference()
{
    chooser_ =
        std::make_unique<juce::FileChooser>(juce::String::fromUTF8("Un morceau qui donne la direction"),
                                            juce::File::getSpecialLocation(juce::File::userMusicDirectory),
                                            "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this](const juce::FileChooser& chooser)
                          {
                              const auto file = chooser.getResult();
                              if (file.existsAsFile())
                                  direction_.addReference(file.getFullPathName().toStdString());
                          });
}

void DirectionPanel::typeTempo(const juce::String& typed)
{
    const auto bpm = typed.replaceCharacter(',', '.').getDoubleValue();
    if (bpm >= 20.0 && bpm <= 400.0)
        direction_.correctTempo(bpm);
    tempoEdit_.clear();
}

void DirectionPanel::chooseKey(int itemId)
{
    if (itemId == fromReferencesItem)
    {
        if (state_.direction().corrections.key.has_value())
            direction_.correctKey(std::nullopt);
        return;
    }
    if (itemId >= 2 && itemId <= 25)
    {
        const auto key = keyOf(itemId);
        if (state_.direction().corrections.key != key)
            direction_.correctKey(key);
    }
}

void DirectionPanel::refresh()
{
    const auto& direction = state_.direction();
    const auto combined = domain::direction::combine(direction);
    const auto reading = direction_.stage() == DirectionHost::Stage::reading;

    status_.setText(text(direction_.status()), juce::dontSendNotification);
    progress_ = direction_.progress();
    progressBar_.setVisible(reading);
    cancel_.setVisible(reading);
    add_.setEnabled(!reading);
    clear_.setEnabled(!direction.empty());

    // The references: rebuilt when their list changed.
    const auto same = rows_.size() == direction.references.size();
    if (!same)
    {
        rows_.clear();
        for (const auto& reference : direction.references)
        {
            rows_.push_back(std::make_unique<Row>(*this, reference));
            addAndMakeVisible(*rows_.back());
        }
    }

    std::string tempo = "Tempo : ";
    if (combined.bpm)
        tempo += std::to_string(static_cast<int>(std::lround(*combined.bpm))) + " BPM" +
                 (combined.bpmCorrected ? " (à la main)" : " (références)");
    else
        tempo += direction.references.empty() ? "aucune référence" : "non tranché";
    tempo_.setText(text(tempo), juce::dontSendNotification);

    std::string key = "Tonalité : ";
    if (combined.key)
        key += domain::generation::describe(*combined.key) +
               (combined.keyCorrected ? " (à la main)" : " (références)");
    else if (!combined.keyCandidates.empty())
    {
        key += "à choisir :";
        for (std::size_t index = 0; index < combined.keyCandidates.size(); ++index)
            key += (index == 0 ? " " : " ou ") + domain::generation::describe(combined.keyCandidates[index]);
    }
    else
        key += direction.references.empty() ? "aucune référence" : "non tranchée";
    key_.setText(text(key), juce::dontSendNotification);
    keyChoice_.setSelectedId(direction.corrections.key ? itemOf(*direction.corrections.key)
                                                       : fromReferencesItem,
                             juce::dontSendNotification);

    std::string said;
    for (const auto& sentence : combined.contradictions)
        said += (said.empty() ? "" : " ") + sentence;
    contradictions_.setText(text(said), juce::dontSendNotification);

    if (!amount_.isMouseButtonDown())
        amount_.setValue(direction.amount, juce::dontSendNotification);
    amount_.setEnabled(!direction.empty());

    resized();
    repaint();
}

std::string DirectionPanel::sectionLetters() const
{
    std::string letters;
    for (const auto& section : domain::direction::combine(state_.direction()).sections)
        letters.push_back(section.label);
    return letters;
}

namespace
{

// "A 1–8": the letter, and the bars of the project the section covers.
std::string onGridText(const domain::direction::GridSection& section, double beatsPerBar)
{
    return std::string(1, section.label) + " " +
           std::to_string(domain::direction::firstBar(section, beatsPerBar)) + "–" +
           std::to_string(domain::direction::lastBar(section, beatsPerBar));
}

} // namespace

std::string DirectionPanel::sectionBars() const
{
    std::string bars;
    for (const auto& section : domain::direction::onGrid(domain::direction::combine(state_.direction())))
        bars += (bars.empty() ? "" : ", ") + onGridText(section, state_.beatsPerBar());
    return bars;
}

juce::Rectangle<int> DirectionPanel::sectionsArea() const
{
    return contradictions_.getBounds()
        .withY(contradictions_.getBottom() + tokens_.integer("space.xs"))
        .withHeight(tokens_.integer("metric.direction.sectionsHeight"));
}

void DirectionPanel::paint(juce::Graphics& g)
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
        g.drawText("DIRECTION", header, juce::Justification::centredLeft, false);
    }

    // The sections of the reference that counts most, in proportion: on the
    // project's grid, with the bars they cover, when they were cut on a
    // tempo (S23); in seconds, with their letter alone, when not.
    const auto combined = domain::direction::combine(state_.direction());
    const auto area = sectionsArea();
    if (combined.sections.empty() || area.isEmpty())
        return;

    struct Box
    {
        double from;
        double to;
        std::string text;
    };
    std::vector<Box> boxes;
    const auto placed = domain::direction::onGrid(combined);
    if (!placed.empty())
    {
        for (const auto& section : placed)
            boxes.push_back({section.fromBeats, section.toBeats, onGridText(section, state_.beatsPerBar())});
    }
    else
    {
        for (const auto& section : combined.sections)
            boxes.push_back({section.fromSeconds, section.toSeconds, std::string(1, section.label)});
    }

    const auto length = std::max(1e-9, boxes.back().to);
    g.setFont(lookAndFeel_.typography().sans("font.size.caption", "font.weight.medium"));
    for (const auto& section : boxes)
    {
        const auto from =
            area.getX() + static_cast<int>(std::lround(area.getWidth() * section.from / length));
        const auto to = area.getX() + static_cast<int>(std::lround(area.getWidth() * section.to / length));
        const juce::Rectangle<int> box{from, area.getY(), std::max(1, to - from), area.getHeight()};
        g.setColour(tokens_.colour("color.state.selected"));
        g.fillRect(box.reduced(tokens_.integer("stroke.hairline"), 0));
        g.setColour(tokens_.colour("color.text.primary"));
        // Too narrow for its bars, a section keeps its letter.
        const auto text = juce::String::fromUTF8(section.text.c_str());
        const auto fits =
            juce::GlyphArrangement::getStringWidthInt(g.getCurrentFont(), text) <= box.getWidth();
        g.drawText(fits ? text : text.substring(0, 1), box, juce::Justification::centred, false);
    }
}

void DirectionPanel::resized()
{
    const auto row = tokens_.integer("metric.direction.rowHeight");
    const auto gap = tokens_.integer("space.xs");
    const auto label = tokens_.integer("metric.direction.labelWidth");
    const auto button = tokens_.integer("metric.generation.buttonWidth");

    auto area = getLocalBounds().reduced(tokens_.integer("space.sm"), 0);
    area.removeFromTop(tokens_.integer("metric.panel.headerHeight") + gap);

    auto top = area.removeFromTop(row);
    add_.setBounds(top.removeFromLeft(label * 2));
    top.removeFromLeft(gap);
    clear_.setBounds(top.removeFromRight(label));
    cancel_.setBounds(top.removeFromRight(button));
    area.removeFromTop(gap);
    auto statusRow = area.removeFromTop(row);
    progressBar_.setBounds(statusRow.removeFromRight(label));
    status_.setBounds(statusRow);
    area.removeFromTop(gap);

    for (auto& reference : rows_)
        reference->setBounds(area.removeFromTop(row));
    area.removeFromTop(gap);

    auto tempoRow = area.removeFromTop(row);
    tempoAuto_.setBounds(tempoRow.removeFromRight(button));
    tempoEdit_.setBounds(tempoRow.removeFromRight(label));
    tempo_.setBounds(tempoRow);
    area.removeFromTop(gap);
    auto keyRow = area.removeFromTop(row);
    keyChoice_.setBounds(keyRow.removeFromRight(label + button));
    key_.setBounds(keyRow);
    area.removeFromTop(gap);
    contradictions_.setBounds(area.removeFromTop(row));
    area.removeFromTop(tokens_.integer("metric.direction.sectionsHeight") + 2 * gap);
    auto amountRow = area.removeFromTop(row);
    amountLabel_.setBounds(amountRow.removeFromLeft(label));
    amount_.setBounds(amountRow);
}

} // namespace daw::ui
