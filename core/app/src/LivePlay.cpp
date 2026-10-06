#include "LivePlay.h"

#include "RawKeyboard.h"

namespace daw::app
{
namespace
{

// How often the message thread looks whether the window is in front. The
// keyboard thread also looks at each key; this is for the keys already down
// when the window goes behind.
constexpr int foregroundLookMs = 100;

} // namespace

LivePlay::LivePlay(domain::live::Router& router,
                   domain::CommandBus& bus,
                   const domain::ProjectState& state,
                   ui::Selection& selection,
                   ui::ProjectObserver& project,
                   tracktion::Edit& edit,
                   std::function<double()> outputLatency)
    : router_(router)
    , state_(state)
    , selection_(selection)
    , project_(project)
    , keys_(router)
    , raw_(std::make_unique<RawKeyboard>(keys_))
    , recorder_({router, bus, state, edit, std::move(outputLatency)})
{
    selection_.addChangeListener(this);
    project_.addChangeListener(this);
    juce::Desktop::getInstance().addFocusChangeListener(this);
    follow();
    startTimer(foregroundLookMs);
}

LivePlay::~LivePlay()
{
    stopTimer();
    raw_.reset(); // the keyboard thread stops before the keyboard it writes to
    juce::Desktop::getInstance().removeFocusChangeListener(this);
    project_.removeChangeListener(this);
    selection_.removeChangeListener(this);
    static_cast<void>(router_.releaseAll(domain::live::now()));
}

void LivePlay::follow()
{
    const auto chosen = selection_.track();
    const bool exists = !chosen.isNil() && state_.findTrack(chosen) != nullptr;
    const auto wanted = exists ? chosen.toString() : std::string{};
    if (wanted != router_.target())
        router_.setTarget(wanted);
}

void LivePlay::setKeyboardPlaying(bool playing)
{
    keys_.setPlaying(playing && keyboardAvailable());
    juce::Logger::writeToLog(juce::String("jeu: clavier de l'ordinateur ") +
                             (keys_.playing() ? "allumé" : "éteint"));
}

bool LivePlay::keyboardAvailable() const
{
    return raw_ != nullptr && raw_->running();
}

std::string LivePlay::targetName() const
{
    const auto chosen = router_.target();
    for (const auto& track : state_.tracks())
    {
        if (track.id.toString() == chosen)
            return track.name;
    }
    return {};
}

ui::LiveHost::Recording LivePlay::recording() const
{
    switch (recorder_.stage())
    {
    case TakeRecorder::Stage::counting:
        return Recording::counting;
    case TakeRecorder::Stage::recording:
        return Recording::recording;
    case TakeRecorder::Stage::idle:
        break;
    }
    return Recording::idle;
}

void LivePlay::changeListenerCallback(juce::ChangeBroadcaster*)
{
    follow();
}

void LivePlay::globalFocusChanged(juce::Component* focused)
{
    // In a text field the keys type, always: the copilot, Ctrl+G, renaming.
    keys_.setTyping(dynamic_cast<juce::TextEditor*>(focused) != nullptr);
}

void LivePlay::timerCallback()
{
    keys_.setForeground(juce::Process::isForegroundProcess());
}

} // namespace daw::app
