#include "TakeRecorder.h"

#include "daw/domain/commands/TransportCommands.h"

#include <algorithm>
#include <cmath>

namespace daw::app
{
namespace
{

constexpr int pollMs = 15;

// A note played this much before the take begins is the take's: the first
// downbeat, played a hair early.
constexpr double anticipationBeats = 0.125;

} // namespace

TakeRecorder::TakeRecorder(Wiring wiring)
    : wiring_(std::move(wiring))
{
    if (!wiring_.latency)
        wiring_.latency = [] { return 0.0; };
}

TakeRecorder::~TakeRecorder()
{
    stopTimer();
    wiring_.router.setRecording(false);
}

double TakeRecorder::secondsOf(double beats) const
{
    return wiring_.edit.tempoSequence.toTime(tracktion::BeatPosition::fromBeats(beats)).inSeconds();
}

double TakeRecorder::beatsOf(double seconds) const
{
    return wiring_.edit.tempoSequence.toBeats(tracktion::TimePosition::fromSeconds(seconds)).inBeats();
}

void TakeRecorder::changed()
{
    if (onChanged)
        onChanged();
}

void TakeRecorder::toggle()
{
    if (stage_ == Stage::idle)
        start();
    else
        stop();
}

void TakeRecorder::start()
{
    if (stage_ != Stage::idle)
        return;

    if (wiring_.router.target().empty())
    {
        said_ = "Aucune piste choisie : choisis un canal dans le rack.";
        changed();
        return;
    }

    const auto& transport = wiring_.state.transport();
    const auto bar = wiring_.state.beatsPerBar();
    mode_ = transport.mode;
    const bool playing = transport.playing;

    domain::live::TakeTiming timing;
    timing.latencySeconds = wiring_.latency();
    timing.beatsAt = [this](double seconds) { return beatsOf(seconds); };

    if (mode_ == domain::PlayMode::pattern)
    {
        pattern_ = transport.auditionedPattern;
        const auto* pattern = wiring_.state.findPattern(pattern_);
        if (pattern == nullptr)
        {
            said_ = "Aucun pattern en cours : rien à enregistrer.";
            changed();
            return;
        }
        const auto length = pattern->lengthBeats;
        timing.loopStartBeats = 0.0;
        timing.loopLengthBeats = length;
        // The count-in is the pattern's last bar; the take begins as the loop
        // comes round.
        prerollBeats_ = std::max(0.0, length - bar);
        startBeats_ = length;
    }
    else
    {
        pattern_ = {};
        const auto at = std::floor(std::max(0.0, transport.positionBeats) / bar) * bar;
        startBeats_ = at;
        prerollBeats_ = at >= bar ? at - bar : at;
    }

    const bool counting = countIn_ && !playing && prerollBeats_ < startBeats_;
    if (!counting)
        prerollBeats_ = mode_ == domain::PlayMode::pattern ? 0.0 : startBeats_;
    if (mode_ == domain::PlayMode::pattern && !counting)
        startBeats_ = 0.0;

    take_ = std::make_unique<domain::live::TakeBuilder>(std::move(timing));
    startClock_.reset();
    written_ = 0;

    // What a previous take left in the queue is not this one's.
    domain::live::Event stale;
    while (wiring_.router.popTake(stale))
    {
    }
    wiring_.router.setRecording(true);

    startedTransport_ = !playing;
    if (playing)
    {
        // Recording over a song already playing: it starts now.
        startClock_ = domain::live::now();
    }
    else
    {
        static_cast<void>(wiring_.bus.execute(std::make_unique<domain::TransportSetPosition>(prerollBeats_)));
        static_cast<void>(wiring_.bus.execute(std::make_unique<domain::TransportPlay>()));
    }

    stage_ = counting ? Stage::counting : Stage::recording;
    said_ = counting ? "Décompte…" : "Enregistrement…";
    applyClick();
    startTimer(pollMs);
    juce::Logger::writeToLog(juce::String("jeu: prise commencée, ") +
                             (mode_ == domain::PlayMode::pattern ? "PAT" : "SONG") +
                             (counting ? ", une mesure de décompte" : ", sans décompte") +
                             ", latence de sortie " + juce::String(wiring_.latency() * 1000.0, 1) + " ms");
    changed();
}

void TakeRecorder::stop()
{
    if (stage_ == Stage::idle)
        return;
    poll();
    finish();
}

void TakeRecorder::poll()
{
    if (stage_ == Stage::idle || take_ == nullptr)
        return;

    domain::live::Position position;
    const bool known = wiring_.router.position(position);

    // When the take begins on the input clock: where the song renders the
    // end of the count-in, plus what the card adds before it is heard.
    if (!startClock_ && known && position.playing)
    {
        const auto preroll = secondsOf(prerollBeats_);
        const auto start = secondsOf(startBeats_);
        auto rendered = position.editSeconds;
        // In pattern mode the count-in's bar ends where the loop goes round:
        // a first reading taken after it is a pass further on.
        if (mode_ == domain::PlayMode::pattern && rendered < preroll - 0.05)
            rendered += start;
        if (rendered >= preroll - 0.05)
            startClock_ = position.clock + (start - rendered) + wiring_.latency();
    }

    domain::live::Event event;
    while (wiring_.router.popTake(event))
    {
        if (!startClock_)
            continue; // before the song even started: heard, not written
        const auto anticipation = secondsOf(startBeats_ + anticipationBeats) - secondsOf(startBeats_);
        if (event.seconds < *startClock_ - anticipation)
            continue; // the count-in's
        const auto track = domain::TrackId::parse(wiring_.router.trackOf(event.slot));
        if (!track)
            continue;
        take_->add(event, track.value(), position);
    }

    if (stage_ == Stage::counting && startClock_ && domain::live::now() >= *startClock_)
    {
        stage_ = Stage::recording;
        said_ = "Enregistrement…";
        changed();
    }
}

void TakeRecorder::timerCallback()
{
    poll();

    // The song stopped (Space, or the end of the material): the take ends.
    if (stage_ != Stage::idle && !wiring_.state.transport().playing)
        finish();
}

void TakeRecorder::finish()
{
    if (stage_ == Stage::idle)
        return;
    stopTimer();
    wiring_.router.setRecording(false);

    const auto at = domain::live::now();
    domain::live::Position position;
    static_cast<void>(wiring_.router.position(position));
    std::vector<domain::live::TakeNote> notes;
    if (take_ != nullptr)
    {
        take_->stop(at, position);
        notes = take_->notes(at, position);
    }
    take_.reset();
    stage_ = Stage::idle;

    domain::live::TakeDestination destination;
    destination.mode = mode_;
    if (mode_ == domain::PlayMode::pattern)
    {
        destination.pattern = pattern_;
    }
    else if (!notes.empty())
    {
        // A pattern of its own, from the bar of its first note to the bar
        // after its last.
        const auto bar = wiring_.state.beatsPerBar();
        double first = notes.front().startBeats;
        double last = first;
        for (const auto& note : notes)
        {
            first = std::min(first, note.startBeats);
            last = std::max(last, note.startBeats + note.lengthBeats);
        }
        destination.pattern = domain::PatternId::generate();
        destination.placement = domain::PlacementId::generate();
        destination.atBeats = std::floor(std::max(0.0, first) / bar) * bar;
        destination.lengthBeats = std::max(bar, std::ceil((last - destination.atBeats) / bar) * bar);
    }

    const domain::live::TakeIds ids{[] { return domain::NoteId::generate(); },
                                    [] { return domain::ClipId::generate(); }};
    auto commands = domain::live::takeCommands(wiring_.state, destination, notes, ids);
    written_ = static_cast<int>(std::count_if(commands.begin(),
                                              commands.end(),
                                              [](const std::unique_ptr<domain::Command>& command)
                                              { return command->type() == "note.add"; }));
    if (commands.empty())
    {
        said_ = "Rien de joué : la prise n'écrit rien.";
    }
    else
    {
        domain::GroupOptions group;
        group.label = "prise : " + std::to_string(written_) + (written_ > 1 ? " notes" : " note");
        if (wiring_.bus.executeGroup(std::move(commands), group).ok())
            said_ = "Prise écrite : " + std::to_string(written_) + (written_ > 1 ? " notes." : " note.");
        else
        {
            said_ = "La prise n'a pas pu être écrite.";
            written_ = 0;
        }
    }
    juce::Logger::writeToLog("jeu: " + juce::String::fromUTF8(said_.c_str()));
    applyClick();
    changed();
}

std::vector<domain::live::TakeNote> TakeRecorder::notes() const
{
    if (take_ == nullptr || stage_ != Stage::recording)
        return {};
    domain::live::Position position;
    static_cast<void>(wiring_.router.position(position));
    return take_->notes(domain::live::now(), position);
}

void TakeRecorder::setMetronome(bool on)
{
    metronome_ = on;
    applyClick();
    changed();
}

void TakeRecorder::applyClick()
{
    // The count-in always clicks; the take and the song when the metronome
    // is on.
    const bool wanted = metronome_ || stage_ == Stage::counting;
    if (wiring_.edit.clickTrackEnabled.get() != wanted)
        wiring_.edit.clickTrackEnabled = wanted;
}

} // namespace daw::app
