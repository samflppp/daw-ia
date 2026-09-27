#include "daw/ui/TitleBarView.h"

#include <utility>

namespace daw::ui
{
namespace
{

constexpr int statusFadeMs = 3000;

void call(const std::function<void()>& action)
{
    if (action)
        action();
}

} // namespace

// Clears a good-news status after a few seconds: "enregistré" is worth
// reading once, not forever.
class TitleBarView::StatusFade final : public juce::Timer
{
public:
    explicit StatusFade(TitleBarView& owner)
        : owner_(owner)
    {
    }

    void timerCallback() override
    {
        stopTimer();
        owner_.status_.clear();
        owner_.repaint();
    }

private:
    TitleBarView& owner_;
};

TitleBarView::TitleBarView(const Tokens& tokens,
                           DawLookAndFeel& lookAndFeel,
                           WorkspaceHost& workspaces,
                           Actions actions)
    : tokens_(tokens)
    , lookAndFeel_(lookAndFeel)
    , workspaces_(workspaces)
    , actions_(std::move(actions))
    , fade_(std::make_unique<StatusFade>(*this))
{
    setLookAndFeel(&lookAndFeel_);

    file_.setWantsKeyboardFocus(false);
    file_.onClick = [this] { showFileMenu(); };
    addAndMakeVisible(file_);

    for (const auto& entry : workspaces_.available())
    {
        auto button = std::make_unique<juce::TextButton>(juce::String::fromUTF8(entry.label.c_str()));
        const auto id = entry.id;

        button->setWantsKeyboardFocus(false);
        button->onClick = [this, id] { workspaces_.request(id); };

        addAndMakeVisible(*button);
        workspaceButtons_.push_back(std::move(button));
    }

    minimise_.setButtonText(juce::String::fromUTF8("\xe2\x80\x93")); // –
    minimise_.setTooltip(juce::String::fromUTF8("Réduire"));
    minimise_.onClick = [this] { call(actions_.minimise); };

    maximise_.setButtonText(juce::String::fromUTF8("\xe2\x96\xa1")); // □
    maximise_.setTooltip("Agrandir");
    maximise_.onClick = [this] { call(actions_.toggleMaximise); };

    close_.setButtonText(juce::String::fromUTF8("\xc3\x97")); // ×
    close_.setTooltip("Fermer");
    close_.onClick = [this] { call(actions_.close); };

    for (auto* button : {&minimise_, &maximise_, &close_})
    {
        button->setWantsKeyboardFocus(false);
        addAndMakeVisible(*button);
    }

    refresh();
}

TitleBarView::~TitleBarView()
{
    setLookAndFeel(nullptr);
}

void TitleBarView::setProjectName(const juce::String& name)
{
    projectName_ = name;
    repaint();
}

void TitleBarView::setStatus(const juce::String& status, bool lasting)
{
    status_ = status;
    fade_->stopTimer();
    if (!lasting)
        fade_->startTimer(statusFadeMs);
    repaint();
}

void TitleBarView::refresh()
{
    const auto current = workspaces_.current();
    const auto entries = workspaces_.available();

    for (std::size_t index = 0; index < workspaceButtons_.size() && index < entries.size(); ++index)
        workspaceButtons_[index]->setToggleState(entries[index].id == current, juce::dontSendNotification);
}

void TitleBarView::showFileMenu()
{
    juce::PopupMenu menu;
    menu.addItem(newItem, "Nouveau projet...");
    menu.addItem(openItem, "Ouvrir...");
    menu.addSeparator();
    menu.addItem(saveItem, "Enregistrer");
    menu.addItem(saveAsItem, "Enregistrer sous...");
    menu.addSeparator();
    menu.addItem(exportItem, "Exporter...");

    // What the generator learns from, said and undone here, and nowhere else
    // than on this machine.
    const auto ask = [](const std::function<bool()>& question) { return question ? question() : false; };
    juce::PopupMenu generation;
    generation.addItem(
        learnItem, juce::String::fromUTF8("Apprendre de mes projets"), true, ask(actions_.learning));
    generation.addItem(projectLearnItem,
                       juce::String::fromUTF8("Apprendre de ce projet"),
                       ask(actions_.learning),
                       ask(actions_.projectLearning));
    generation.addSeparator();
    generation.addItem(forgetItem, juce::String::fromUTF8("Oublier ce qui a été appris..."));
    menu.addSeparator();
    menu.addSubMenu(juce::String::fromUTF8("Génération"), generation);

    juce::Component::SafePointer<TitleBarView> self{this};
    menu.showMenuAsync(juce::PopupMenu::Options{}.withTargetComponent(&file_),
                       [self](int chosen)
                       {
                           if (self != nullptr)
                               self->runMenuItem(chosen);
                       });
}

void TitleBarView::runMenuItem(int item)
{
    switch (item)
    {
    case newItem:
        call(actions_.newProject);
        break;
    case openItem:
        call(actions_.openProject);
        break;
    case saveItem:
        call(actions_.save);
        break;
    case saveAsItem:
        call(actions_.saveAs);
        break;
    case exportItem:
        call(actions_.exportSong);
        break;
    case learnItem:
        call(actions_.toggleLearning);
        break;
    case projectLearnItem:
        call(actions_.toggleProjectLearning);
        break;
    case forgetItem:
        call(actions_.forgetLearning);
        break;
    default:
        break;
    }
}

void TitleBarView::paint(juce::Graphics& g)
{
    g.fillAll(tokens_.colour("color.surface.base"));

    auto bottom = getLocalBounds();
    g.setColour(tokens_.colour("color.border.hairline"));
    g.fillRect(bottom.removeFromBottom(tokens_.integer("stroke.hairline")));

    auto text = textArea_;
    g.setColour(tokens_.colour("color.text.primary"));
    g.setFont(lookAndFeel_.typography().sans("font.size.caption", "font.weight.medium"));

    const auto name = projectName_.isEmpty() ? juce::String{"DAW IA"} : projectName_;
    const auto nameWidth =
        juce::jmin(text.getWidth(), juce::GlyphArrangement::getStringWidthInt(g.getCurrentFont(), name));
    g.drawText(name, text.removeFromLeft(nameWidth), juce::Justification::centredLeft, true);

    if (status_.isNotEmpty())
    {
        text.removeFromLeft(tokens_.integer("space.md"));
        g.setColour(tokens_.colour("color.text.tertiary"));
        g.setFont(lookAndFeel_.typography().sans("font.size.caption", "font.weight.regular"));
        g.drawText(status_, text, juce::Justification::centredLeft, true);
    }
}

void TitleBarView::resized()
{
    auto area = getLocalBounds();
    area.removeFromBottom(tokens_.integer("stroke.hairline"));

    const auto buttonWidth = tokens_.integer("metric.titleBar.buttonWidth");
    close_.setBounds(area.removeFromRight(buttonWidth));
    maximise_.setBounds(area.removeFromRight(buttonWidth));
    minimise_.setBounds(area.removeFromRight(buttonWidth));
    area.removeFromRight(tokens_.integer("space.lg"));

    auto inner = area.reduced(tokens_.integer("space.sm"), tokens_.integer("space.xs"));
    file_.setBounds(inner.removeFromLeft(tokens_.integer("metric.titleBar.fileWidth")));
    inner.removeFromLeft(tokens_.integer("space.lg"));

    const auto switchWidth = tokens_.integer("metric.titleBar.switchWidth");
    for (auto button = workspaceButtons_.rbegin(); button != workspaceButtons_.rend(); ++button)
    {
        (*button)->setBounds(inner.removeFromRight(switchWidth));
        inner.removeFromRight(tokens_.integer("space.xs"));
    }

    textArea_ = inner;
}

// The band is the window's handle, as a system title bar would be: pressed and
// dragged anywhere a button is not, it moves the window; double-clicked, it
// maximises it or gives it back its size.
void TitleBarView::mouseDown(const juce::MouseEvent& event)
{
    dragging_ = !(actions_.maximised && actions_.maximised());
    if (!dragging_)
        return;

    if (auto* window = getTopLevelComponent(); window != nullptr)
        dragger_.startDraggingComponent(window, event.getEventRelativeTo(window));
}

void TitleBarView::mouseDrag(const juce::MouseEvent& event)
{
    if (!dragging_)
        return;

    if (auto* window = getTopLevelComponent(); window != nullptr)
        dragger_.dragComponent(window, event.getEventRelativeTo(window), nullptr);
}

void TitleBarView::mouseDoubleClick(const juce::MouseEvent&)
{
    call(actions_.toggleMaximise);
}

} // namespace daw::ui
