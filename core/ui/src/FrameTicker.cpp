#include "daw/ui/FrameTicker.h"

#include "daw/ui/Tokens.h"

#include <algorithm>
#include <utility>
#include <vector>

namespace daw::ui
{
namespace
{

// Every ticker alive, on the message thread, so a change of pace reaches them
// all without a restart.
std::vector<FrameTicker*>& tickers()
{
    static std::vector<FrameTicker*> all;
    return all;
}

FrameTicker::Pace& currentPace()
{
    static auto pace = FrameTicker::Pace::fluid;
    return pace;
}

} // namespace

FrameTicker::FrameTicker(juce::Component& owner, std::function<void()> onFrame)
    : owner_(owner)
    , onFrame_(std::move(onFrame))
{
    tickers().push_back(this);
    follow(currentPace());
}

FrameTicker::~FrameTicker()
{
    stopTimer();
    auto& all = tickers();
    all.erase(std::remove(all.begin(), all.end(), this), all.end());
}

void FrameTicker::setPace(Pace pace)
{
    currentPace() = pace;
    for (auto* ticker : tickers())
        ticker->follow(pace);
}

FrameTicker::Pace FrameTicker::pace() noexcept
{
    return currentPace();
}

void FrameTicker::follow(Pace pace)
{
    if (pace == Pace::fluid)
    {
        stopTimer();
        if (vblank_ == nullptr)
            vblank_ = std::make_unique<juce::VBlankAttachment>(&owner_, onFrame_);
        return;
    }

    vblank_.reset();
    startTimerHz(Tokens::builtIn().integer("motion.light.framesPerSecond"));
}

void FrameTicker::timerCallback()
{
    if (owner_.isShowing() && onFrame_)
        onFrame_();
}

} // namespace daw::ui
