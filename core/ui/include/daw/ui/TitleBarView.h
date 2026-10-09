#pragma once

#include "daw/ui/DawLookAndFeel.h"
#include "daw/ui/Tokens.h"
#include "daw/ui/model/WorkspaceHost.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

namespace daw::ui
{

// The band across the top of the main window, in place of the system's title
// bar: the File menu, the project's name, the workspace switch, and the three
// window buttons.
//
// It does not move the window: Windows does (S18 bis). The band tells the system
// which of its parts is a caption and which are the three window buttons, the
// way a native title bar would, and the system drags, snaps to the edges, opens
// the layouts of Windows 11 over the maximise button, and maximises on a
// double-click. The band itself does nothing else. Every button calls one of
// the actions the application hands it, so the band knows neither the project
// on disk nor how a window is minimised.
class TitleBarView final : public juce::Component
{
public:
    struct Actions
    {
        std::function<void()> newProject;
        std::function<void()> openProject;
        std::function<void()> save;
        std::function<void()> saveAs;
        std::function<void()> exportSong;

        // Fichier > Génération: the generator learning from the person.
        std::function<bool()> learning;
        std::function<void()> toggleLearning;
        std::function<bool()> projectLearning;
        std::function<void()> toggleProjectLearning;
        std::function<void()> forgetLearning;

        // Fichier > Affichage: fluid or light, a setting of this machine and
        // never of the project (S18 bis).
        std::function<bool()> lightDisplay;
        std::function<void(bool light)> setLightDisplay;

        // Fichier > À propos de DAW IA (S26).
        std::function<void()> about;

        std::function<void()> minimise;
        std::function<void()> toggleMaximise;
        std::function<void()> close;
    };

    TitleBarView(const Tokens& tokens,
                 DawLookAndFeel& lookAndFeel,
                 WorkspaceHost& workspaces,
                 Actions actions);
    ~TitleBarView() override;

    void setProjectName(const juce::String& name);

    // A short word after the name — "enregistré", or why it was not — that
    // fades back to nothing after a few seconds when it is good news.
    void setStatus(const juce::String& status, bool lasting);

    [[nodiscard]] const juce::String& status() const noexcept { return status_; }
    [[nodiscard]] const juce::String& projectName() const noexcept { return projectName_; }

    // Re-reads which workspace is shown.
    void refresh();

    // The menu as the verification opens it: the same items, without a popup.
    void runMenuItem(int item);

    enum MenuItem
    {
        newItem = 1,
        openItem,
        saveItem,
        saveAsItem,
        exportItem,
        learnItem,
        projectLearnItem,
        forgetItem,
        fluidDisplayItem,
        lightDisplayItem,
        aboutItem,
    };

    void paint(juce::Graphics& g) override;
    void resized() override;

    // What is under a point of the band, for the system: a window button, a
    // control of the band (the File menu, the workspace switch), or the
    // caption, which is everything else. Asked by the main window, which is
    // the component the system asks.
    [[nodiscard]] WindowControlKind findControlAtPoint(juce::Point<float> point) const override;

private:
    void showFileMenu();

    const Tokens& tokens_;
    DawLookAndFeel& lookAndFeel_;
    WorkspaceHost& workspaces_;
    Actions actions_;

    juce::TextButton file_{"Fichier"};
    std::vector<std::unique_ptr<juce::TextButton>> workspaceButtons_;
    juce::TextButton minimise_;
    juce::TextButton maximise_;
    juce::TextButton close_;

    juce::String projectName_;
    juce::String status_;
    juce::Rectangle<int> textArea_;

    class StatusFade;
    std::unique_ptr<StatusFade> fade_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TitleBarView)
};

} // namespace daw::ui
