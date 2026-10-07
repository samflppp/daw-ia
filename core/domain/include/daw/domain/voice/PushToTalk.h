#pragma once

namespace daw::domain::voice
{

// The push-to-talk's gesture (S25, decided with the founder on 7 October
// 2026): hold a key, speak, release. What a press, a release, another key, a
// lost focus or the clock mean, decided here, apart from any device, so that
// every case is tested.
//
// The key is the right Ctrl, read by its place like the rest of the keyboard
// (scan code 0x1D with the E0 prefix). It plays no note — with Ctrl held, no
// key plays (S23) —, it types nothing in a text field, and AltGr, which
// Windows sends as a left Ctrl, is not it. The button on the screen, held with
// the mouse, does the same.
//
// Every end but a release in time ends without a phrase:
//   - released before minSeconds: too short;
//   - held past maxSeconds: too long, said;
//   - another key pressed meanwhile: that key is a shortcut (Ctrl+Z stays
//     Ctrl+Z), the phrase is dropped;
//   - the window losing the focus.
class PushToTalk
{
public:
    static constexpr int keyScanCode = 0x1D; // with the E0 prefix: the right Ctrl
    static constexpr double minSeconds = 0.5;
    static constexpr double maxSeconds = 15.0;

    enum class State
    {
        idle,
        listening,    // the key is held, the microphone open
        transcribing, // released in time, the phrase is being transcribed
    };

    enum class Ended
    {
        none,
        released, // in time: the phrase goes to the transcriber
        tooShort,
        tooLong,
        otherKey,
        focusLost,
    };

    // What the application must do after an event.
    struct Step
    {
        bool openMicrophone{false};
        bool closeMicrophone{false}; // with `ended`: released → transcribe, else forget
        Ended ended{Ended::none};
    };

    // A key of the computer's keyboard, by its place.
    Step key(int scanCode, bool extended, bool down, double seconds);
    // The button on the screen, held with the mouse.
    Step button(bool down, double seconds);
    Step focusLost(double seconds);
    // The clock, for the longest phrase.
    Step tick(double seconds);
    // The transcription is over, whatever it gave.
    void transcribed();

    [[nodiscard]] State state() const noexcept { return state_; }
    [[nodiscard]] double heldSince() const noexcept { return since_; }

private:
    Step press(double seconds);
    Step release(double seconds);
    Step cancel(Ended why);

    State state_{State::idle};
    double since_{0.0};
};

[[nodiscard]] const char* describe(PushToTalk::Ended ended) noexcept;

} // namespace daw::domain::voice
