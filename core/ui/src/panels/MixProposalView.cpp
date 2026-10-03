#include "daw/ui/panels/MixProposalView.h"

#include <algorithm>
#include <cstdio>
#include <string>

namespace daw::ui
{
namespace
{

[[nodiscard]] juce::String text(const std::string& value)
{
    return juce::String::fromUTF8(value.c_str());
}

[[nodiscard]] std::string french(double value, int decimals = 1)
{
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%.*f", decimals, value);
    std::string out{buffer};
    std::replace(out.begin(), out.end(), '.', ',');
    return out;
}

// What a change does, in a few words, before its sentence says why.
[[nodiscard]] std::string what(const domain::mix::Change& change)
{
    using Kind = domain::mix::Change::Kind;
    switch (change.kind)
    {
    case Kind::volume:
        return "fader à " + french(change.value) + " dB";
    case Kind::pan:
        return "pan à " + french(change.value, 2);
    case Kind::equaliser:
        return "égaliseur";
    case Kind::compressor:
        return "compresseur";
    }
    return {};
}

[[nodiscard]] bool busy(MixHost::Stage stage)
{
    return stage == MixHost::Stage::measuring || stage == MixHost::Stage::deciding ||
           stage == MixHost::Stage::verifying;
}

void axis(juce::Slider& slider, juce::Label& label, const char* words)
{
    label.setText(juce::String::fromUTF8(words), juce::dontSendNotification);
    slider.setSliderStyle(juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    slider.setRange(-1.0, 1.0);
    slider.setDoubleClickReturnValue(true, 0.0);
}

} // namespace

// --- the rows: one per strip the proposal changes -------------------------------

class MixProposalView::Rows final : public juce::Component
{
public:
    struct Row
    {
        domain::TrackId track;
        std::unique_ptr<juce::TextButton> keep;
        std::unique_ptr<juce::Label> title;
        std::unique_ptr<juce::Label> sentences;
        juce::String plain;
    };

    Rows(const Tokens& tokens, DawLookAndFeel& lookAndFeel)
        : tokens_(tokens)
        , lookAndFeel_(lookAndFeel)
    {
    }

    void add(domain::TrackId track, const juce::String& name, const juce::String& sentences, MixHost& mix)
    {
        Row row;
        row.track = track;
        // Lit: kept. A click leaves the strip out, another takes it back.
        row.keep = std::make_unique<juce::TextButton>("garder");
        row.keep->setClickingTogglesState(true);
        row.keep->setToggleState(!mix.isRefused(track), juce::dontSendNotification);
        row.keep->onClick = [&mix, track, toggle = row.keep.get()]
        { mix.refuseTrack(track, !toggle->getToggleState()); };
        addAndMakeVisible(*row.keep);
        row.title = std::make_unique<juce::Label>();
        row.title->setText(name, juce::dontSendNotification);
        addAndMakeVisible(*row.title);
        row.sentences = std::make_unique<juce::Label>();
        row.sentences->setText(sentences, juce::dontSendNotification);
        row.sentences->setJustificationType(juce::Justification::topLeft);
        row.sentences->setColour(juce::Label::textColourId, tokens_.colour("color.text.secondary"));
        row.sentences->setMinimumHorizontalScale(1.0f);
        addAndMakeVisible(*row.sentences);
        row.plain = sentences;
        rows_.push_back(std::move(row));
    }

    void clear()
    {
        rows_.clear();
        removeAllChildren();
    }

    void sync(const MixHost& mix)
    {
        for (auto& row : rows_)
            row.keep->setToggleState(!mix.isRefused(row.track), juce::dontSendNotification);
    }

    // Lays the rows out at `width` and says how tall they are.
    int place(int width)
    {
        const auto line = tokens_.integer("metric.mixer.rowHeight");
        const auto gap = tokens_.integer("space.sm");
        const auto indent = tokens_.integer("space.lg");
        const auto font = lookAndFeel_.typography().sans("font.size.body", "font.weight.regular");
        int y = 0;
        for (auto& row : rows_)
        {
            auto head = juce::Rectangle<int>{0, y, width, line};
            row.keep->setBounds(head.removeFromLeft(tokens_.integer("metric.generation.buttonWidth")));
            head.removeFromLeft(gap);
            row.title->setBounds(head);
            y += line;
            juce::AttributedString attributed;
            attributed.append(row.plain, font);
            attributed.setWordWrap(juce::AttributedString::byWord);
            juce::TextLayout layout;
            layout.createLayout(attributed, static_cast<float>(std::max(1, width - indent)));
            const auto height = static_cast<int>(std::ceil(layout.getHeight())) + gap;
            row.sentences->setFont(font);
            row.sentences->setBounds(indent, y, std::max(1, width - indent), height);
            y += height + gap;
        }
        return y;
    }

    [[nodiscard]] const std::vector<Row>& rows() const { return rows_; }

private:
    const Tokens& tokens_;
    DawLookAndFeel& lookAndFeel_;
    std::vector<Row> rows_;
};

// --- the view -----------------------------------------------------------------

MixProposalView::MixProposalView(MixHost& mix,
                                 const domain::ProjectState& state,
                                 const Tokens& tokens,
                                 DawLookAndFeel& lookAndFeel)
    : mix_(mix)
    , state_(state)
    , tokens_(tokens)
    , lookAndFeel_(lookAndFeel)
    , rows_(std::make_unique<Rows>(tokens, lookAndFeel))
{
    setOpaque(true); // paint() fills the whole rectangle

    addAndMakeVisible(status_);
    status_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(progressBar_);
    progressBar_.setPercentageDisplay(false);

    for (auto* button : {&before_, &after_, &keep_, &reject_})
        addAndMakeVisible(*button);
    before_.setButtonText("Avant");
    after_.setButtonText(juce::String::fromUTF8("Après"));
    keep_.setButtonText("Garder");
    reject_.setButtonText("Refuser");
    before_.setClickingTogglesState(false);
    after_.setClickingTogglesState(false);
    before_.onClick = [this] { mix_.listen(false); };
    after_.onClick = [this] { mix_.listen(true); };
    keep_.onClick = [this] { mix_.accept(); };
    reject_.onClick = [this] { mix_.reject(); };

    axis(punch_, punchLabel_, "doux ↔ percutant");
    axis(focus_, focusLabel_, "voix devant ↔ instru devant");
    axis(width_, widthLabel_, "serré ↔ large");
    for (auto* slider : {&punch_, &focus_, &width_})
    {
        addAndMakeVisible(*slider);
        // Deciding again on release, not on every pixel: one decision per gesture.
        slider->onDragEnd = [this] { mix_.setAxes(axesShown()); };
    }
    for (auto* label : {&punchLabel_, &focusLabel_, &widthLabel_})
        addAndMakeVisible(*label);

    addAndMakeVisible(reference_);
    reference_.onClick = [this]
    {
        if (!mix_.reference().empty())
            mix_.clearReference();
        else
            chooseReference();
    };
    addAndMakeVisible(referenceName_);
    addAndMakeVisible(referenceAmount_);
    referenceAmount_.setSliderStyle(juce::Slider::LinearHorizontal);
    referenceAmount_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    referenceAmount_.setRange(0.0, 1.0);
    referenceAmount_.onDragEnd = [this] { mix_.setReferenceAmount(referenceAmount_.getValue()); };

    addAndMakeVisible(viewport_);
    viewport_.setViewedComponent(rows_.get(), false);
    viewport_.setScrollBarsShown(true, false);
    viewport_.setScrollBarThickness(tokens_.integer("metric.scrollbar.thickness"));

    mix_.addChangeListener(this);
    refresh();
}

MixProposalView::~MixProposalView()
{
    mix_.removeChangeListener(this);
}

domain::mix::Axes MixProposalView::axesShown() const
{
    domain::mix::Axes axes;
    axes.punch = punch_.getValue();
    axes.focus = focus_.getValue();
    axes.width = width_.getValue();
    return axes;
}

void MixProposalView::chooseReference()
{
    chooser_ =
        std::make_unique<juce::FileChooser>(juce::String::fromUTF8("Mixer comme ce morceau"),
                                            juce::File::getSpecialLocation(juce::File::userMusicDirectory),
                                            "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this](const juce::FileChooser& chooser)
                          {
                              const auto file = chooser.getResult();
                              if (file.existsAsFile())
                                  mix_.setReference(file.getFullPathName().toStdString());
                          });
}

void MixProposalView::changeListenerCallback(juce::ChangeBroadcaster*)
{
    refresh();
}

void MixProposalView::refresh()
{
    const auto stage = mix_.stage();
    status_.setText(text(mix_.status()), juce::dontSendNotification);
    progress_ = mix_.progress();
    progressBar_.setVisible(stage == MixHost::Stage::measuring || stage == MixHost::Stage::verifying);

    const auto ready = stage == MixHost::Stage::ready;
    for (auto* button : {&before_, &after_, &keep_, &reject_})
        button->setEnabled(ready);
    before_.setToggleState(ready && mix_.listening() && !mix_.listeningAfter(), juce::dontSendNotification);
    after_.setToggleState(ready && mix_.listeningAfter(), juce::dontSendNotification);

    const auto axes = mix_.axes();
    for (auto [slider, value] :
         {std::pair{&punch_, axes.punch}, std::pair{&focus_, axes.focus}, std::pair{&width_, axes.width}})
    {
        slider->setEnabled(!busy(stage));
        if (!slider->isMouseButtonDown())
            slider->setValue(value, juce::dontSendNotification);
    }

    const auto reference = mix_.reference();
    reference_.setButtonText(reference.empty() ? juce::String::fromUTF8("Référence…")
                                               : juce::String::fromUTF8("Sans référence"));
    reference_.setEnabled(!busy(stage));
    referenceName_.setText(reference.empty() ? juce::String{} : text("vers « " + reference + " »"),
                           juce::dontSendNotification);
    referenceAmount_.setVisible(!reference.empty());
    if (!referenceAmount_.isMouseButtonDown())
        referenceAmount_.setValue(mix_.referenceAmount(), juce::dontSendNotification);

    const auto* proposal = mix_.proposal();
    if (proposal != shownProposal_ || (proposal != nullptr && proposal->changes.size() != shownChanges_))
        rebuildRows();
    else
        rows_->sync(mix_);
    repaint();
}

void MixProposalView::rebuildRows()
{
    rows_->clear();
    shownProposal_ = mix_.proposal();
    shownChanges_ = shownProposal_ != nullptr ? shownProposal_->changes.size() : 0;
    if (shownProposal_ != nullptr)
    {
        // In the order of the mixer: the project's tracks, then the master.
        for (const auto& track : state_.tracks())
        {
            const auto changes = shownProposal_->of(track.id);
            if (changes.empty())
                continue;
            std::string what_;
            std::string said;
            for (const auto* change : changes)
            {
                what_ += (what_.empty() ? "" : ", ") + what(*change);
                said += (said.empty() ? "" : "\n") + change->sentence;
            }
            rows_->add(track.id, text(track.name + " — " + what_), text(said), mix_);
        }
        if (const auto master = mix_.masterSentence(); !master.empty())
            rows_->add(domain::ProjectState::masterTrackId(), juce::String{"Master"}, text(master), mix_);
    }
    resized();
}

int MixProposalView::rowCount() const
{
    return static_cast<int>(rows_->rows().size());
}

juce::Button* MixProposalView::rowToggle(domain::TrackId track) const
{
    for (const auto& row : rows_->rows())
    {
        if (row.track == track)
            return row.keep.get();
    }
    return nullptr;
}

juce::String MixProposalView::shownSentences() const
{
    juce::StringArray all;
    for (const auto& row : rows_->rows())
        all.add(row.sentences->getText());
    return all.joinIntoString("\n");
}

void MixProposalView::paint(juce::Graphics& g)
{
    g.fillAll(tokens_.colour("color.surface.sunken"));
}

void MixProposalView::resized()
{
    auto area = getLocalBounds().reduced(tokens_.integer("space.sm"));
    const auto line = tokens_.integer("metric.mixer.rowHeight");
    const auto gap = tokens_.integer("space.xs");
    const auto buttonWidth = tokens_.integer("metric.generation.buttonWidth");
    const auto labelWidth = tokens_.integer("metric.mixer.headerButtonWidth");

    auto top = area.removeFromTop(line);
    if (progressBar_.isVisible())
        progressBar_.setBounds(top.removeFromRight(labelWidth));
    status_.setBounds(top);
    area.removeFromTop(gap);

    auto buttons = area.removeFromTop(line);
    for (auto* button : {&before_, &after_, &keep_, &reject_})
    {
        button->setBounds(buttons.removeFromLeft(buttonWidth));
        buttons.removeFromLeft(gap);
    }
    area.removeFromTop(gap);

    for (auto [label, slider] : {std::pair{&punchLabel_, &punch_},
                                 std::pair{&focusLabel_, &focus_},
                                 std::pair{&widthLabel_, &width_}})
    {
        auto row = area.removeFromTop(line);
        label->setBounds(row.removeFromLeft(labelWidth * 2));
        slider->setBounds(row);
    }
    area.removeFromTop(gap);

    auto reference = area.removeFromTop(line);
    reference_.setBounds(reference.removeFromLeft(labelWidth));
    reference.removeFromLeft(gap);
    referenceName_.setBounds(reference.removeFromLeft(labelWidth * 2));
    referenceAmount_.setBounds(reference);
    area.removeFromTop(gap);

    viewport_.setBounds(area);
    const auto width = std::max(1, area.getWidth() - tokens_.integer("metric.scrollbar.thickness"));
    rows_->setSize(width, std::max(1, rows_->place(width)));
}

} // namespace daw::ui
