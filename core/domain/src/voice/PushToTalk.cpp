#include "daw/domain/voice/PushToTalk.h"

namespace daw::domain::voice
{

PushToTalk::Step PushToTalk::key(int scanCode, bool extended, bool down, double seconds)
{
    const bool ours = scanCode == keyScanCode && extended;
    if (ours)
        return down ? press(seconds) : release(seconds);
    // Another key while the phrase is held: a shortcut, not a phrase.
    if (down && state_ == State::listening)
        return cancel(Ended::otherKey);
    return {};
}

PushToTalk::Step PushToTalk::button(bool down, double seconds)
{
    return down ? press(seconds) : release(seconds);
}

PushToTalk::Step PushToTalk::focusLost(double)
{
    if (state_ == State::listening)
        return cancel(Ended::focusLost);
    return {};
}

PushToTalk::Step PushToTalk::tick(double seconds)
{
    if (state_ == State::listening && seconds - since_ > maxSeconds)
        return cancel(Ended::tooLong);
    return {};
}

void PushToTalk::transcribed()
{
    if (state_ == State::transcribing)
        state_ = State::idle;
}

PushToTalk::Step PushToTalk::press(double seconds)
{
    // A key held repeats its press; a phrase being transcribed is not
    // interrupted by the next one.
    if (state_ != State::idle)
        return {};
    state_ = State::listening;
    since_ = seconds;
    Step step;
    step.openMicrophone = true;
    return step;
}

PushToTalk::Step PushToTalk::release(double seconds)
{
    if (state_ != State::listening)
        return {};
    if (seconds - since_ < minSeconds)
        return cancel(Ended::tooShort);
    if (seconds - since_ > maxSeconds)
        return cancel(Ended::tooLong);
    state_ = State::transcribing;
    Step step;
    step.closeMicrophone = true;
    step.ended = Ended::released;
    return step;
}

PushToTalk::Step PushToTalk::cancel(Ended why)
{
    state_ = State::idle;
    Step step;
    step.closeMicrophone = true;
    step.ended = why;
    return step;
}

const char* describe(PushToTalk::Ended ended) noexcept
{
    switch (ended)
    {
    case PushToTalk::Ended::released:
        return "relâché";
    case PushToTalk::Ended::tooShort:
        return "trop court : tiens la touche pendant que tu parles";
    case PushToTalk::Ended::tooLong:
        return "plus de 15 secondes : rien n'est parti";
    case PushToTalk::Ended::otherKey:
        return "une autre touche : rien n'est parti";
    case PushToTalk::Ended::focusLost:
        return "la fenêtre a perdu la main : rien n'est parti";
    case PushToTalk::Ended::none:
    default:
        return "";
    }
}

} // namespace daw::domain::voice
