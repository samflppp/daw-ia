#include "MainWindow.h"
#include "daw/domain/BuildInfo.h"
#include "daw/ui/Tokens.h"

#include <juce_gui_extra/juce_gui_extra.h>
#include <tracktion_engine/tracktion_engine.h>

#include <memory>

namespace daw::app
{

class Application final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise(const juce::String&) override
    {
        const auto domainVersion = domain::versionString();
        juce::Logger::writeToLog("core domain " + juce::String(domainVersion.data(), domainVersion.size()));

        engine_ = std::make_unique<tracktion::Engine>(getApplicationName());
        window_ = std::make_unique<MainWindow>(getApplicationName(), ui::Tokens::builtIn());
    }

    void shutdown() override
    {
        window_.reset();
        engine_.reset();
    }

    void systemRequestedQuit() override { quit(); }

private:
    std::unique_ptr<tracktion::Engine> engine_;
    std::unique_ptr<MainWindow> window_;
};

} // namespace daw::app

START_JUCE_APPLICATION(daw::app::Application)
