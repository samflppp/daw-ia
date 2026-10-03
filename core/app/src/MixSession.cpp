#include "MixSession.h"

#include "daw/domain/commands/SetTrackVolume.h"
#include "daw/domain/serialization/Json.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace daw::app
{

using domain::Value;

namespace
{

constexpr int progressPollMs = 100; // how often the progress bar learns where the render is

std::string french(double value, int decimals = 1)
{
    char text[32];
    std::snprintf(text, sizeof text, "%.*f", decimals, value);
    std::string out{text};
    std::replace(out.begin(), out.end(), '.', ',');
    return out;
}

juce::String toJuce(const std::string& text)
{
    return juce::String::fromUTF8(text.data(), static_cast<int>(text.size()));
}

} // namespace

MixSession::MixSession(Wiring wiring)
    : wiring_(wiring)
{
    if (wiring_.device != nullptr)
        comparison_ = std::make_unique<MixComparison>(*wiring_.device);
}

MixSession::~MixSession()
{
    stopTimer();
    alive_->store(false);
    cancelled_.store(true);

    // A render still running needs the message thread to stop: it is turned
    // until every worker is done, for at most five seconds.
    const auto until = juce::Time::getMillisecondCounterHiRes() + 5000.0;
    const auto running = [this]
    {
        return std::any_of(
            workers_.begin(), workers_.end(), [](const Worker& worker) { return !worker.done->load(); });
    };
    while (running() && juce::Time::getMillisecondCounterHiRes() < until)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
    for (auto& worker : workers_)
    {
        if (worker.thread.joinable())
            worker.thread.join();
    }
    workers_.clear();
    comparison_.reset();
    render_.reset();
    static_cast<void>(beforeFile_.deleteFile());
    static_cast<void>(afterFile_.deleteFile());
}

void MixSession::setStage(Stage stage, std::string status)
{
    stage_ = stage;
    status_ = std::move(status);
    juce::Logger::writeToLog("mix: " + toJuce(status_));
    sendChangeMessage();
}

void MixSession::timerCallback()
{
    // The progress of a render, drawn by the panel.
    if (stage_ == Stage::measuring || stage_ == Stage::verifying)
        sendChangeMessage();
}

std::string MixSession::keyOf() const
{
    // What sounds: the project without its transport. Playing, stopping,
    // moving the playhead or changing a view never measures again.
    auto value = wiring_.state.toValue();
    Value::Object kept;
    if (const auto* members = value.asObject(); members != nullptr)
    {
        for (const auto& [key, member] : *members)
        {
            if (key != "transport")
                kept.emplace_back(key, member);
        }
    }
    return domain::json::write(Value::object(std::move(kept)));
}

void MixSession::offThread(std::function<void()> work, std::function<void()> then)
{
    reap();
    const auto generation = ++generation_;
    auto done = std::make_shared<std::atomic<bool>>(false);
    Worker worker;
    worker.done = done;
    worker.thread = std::thread{
        [this, work = std::move(work), then = std::move(then), alive = alive_, generation, done]() mutable
        {
            work();
            done->store(true);
            juce::MessageManager::callAsync(
                [this, then = std::move(then), alive, generation]
                {
                    if (!alive->load())
                        return;
                    reap();
                    if (generation != generation_ || cancelled_.load())
                        return;
                    then();
                });
        }};
    workers_.push_back(std::move(worker));
}

void MixSession::reap()
{
    for (auto worker = workers_.begin(); worker != workers_.end();)
    {
        if (worker->done->load())
        {
            if (worker->thread.joinable())
                worker->thread.join();
            worker = workers_.erase(worker);
        }
        else
        {
            ++worker;
        }
    }
}

// --- start, cancel -------------------------------------------------------------

void MixSession::start()
{
    if (stage_ == Stage::measuring || stage_ == Stage::deciding || stage_ == Stage::verifying)
        return;
    cancelled_.store(false);
    clearProposal();

    if (before_ != nullptr && measuredKey_ == keyOf())
    {
        measureReused_ = true;
        decide();
        return;
    }
    measureReused_ = false;
    measure();
}

void MixSession::cancel()
{
    cancelled_.store(true);
    ++generation_;
    stopTimer();

    // The render stops at its next block; its worker keeps it until then.
    if (render_ != nullptr && !workers_.empty())
        workers_.back().retired = std::move(render_);
    render_.reset();
    clearProposal();
    setStage(Stage::idle, "Mixage annulé : rien n'est écrit.");
}

void MixSession::measure()
{
    setStage(Stage::measuring, "Je mesure le morceau, piste par piste…");
    progress_.store(0.0);
    startTimer(progressPollMs);

    render_ =
        engine::MixRender::prepare(wiring_.edit, wiring_.state, nullptr, wiring_.catalogue, wiring_.store);
    if (render_ == nullptr)
    {
        stopTimer();
        setStage(Stage::failed, "Le morceau n'a pas pu être rendu pour la mesure.");
        return;
    }
    prepareMs_ = render_->prepareMs();
    const auto key = keyOf();
    auto* render = render_.get();
    auto measured = std::make_shared<std::unique_ptr<engine::MixRender::Measured>>();
    offThread([this, render, measured]
              { *measured = render->run(cancelled_, [this](double done) { progress_.store(done); }); },
              [this, measured, key]
              {
                  stopTimer();
                  if (*measured == nullptr)
                  {
                      render_.reset();
                      setStage(Stage::failed, "La mesure n'a pas abouti.");
                      return;
                  }
                  static_cast<void>(beforeFile_.deleteFile());
                  beforeFile_ = render_->releaseFile();
                  render_.reset();
                  before_ = std::move(*measured);
                  measuredKey_ = key;
                  measureSeconds_ = before_->renderSeconds;
                  decide();
              });
}

// --- decide --------------------------------------------------------------------

domain::mix::Axes MixSession::effectiveAxes() const
{
    if (referenceMeasure_.has_value() && before_ != nullptr)
        return domain::mix::towards(target(), before_->master, axes_, referenceAmount_);
    return axes_;
}

domain::mix::Target MixSession::target() const
{
    if (referenceMeasure_.has_value())
        return domain::mix::targetOf(*referenceMeasure_, referenceName_);
    domain::mix::Target fromAxes;
    fromAxes.source = "axes";
    return fromAxes;
}

void MixSession::decide()
{
    if (before_ == nullptr)
        return;
    setStage(Stage::deciding, "Je décide des réglages…");
    brief_ = std::make_unique<domain::mix::Brief>(
        domain::mix::briefOf(wiring_.state, before_->tracks, before_->master, effectiveAxes(), target()));
    decisionNote_.clear();
    usage_ = Value{};

    if (useModel_ && wiring_.copilot != nullptr && wiring_.copilot->canInterpret())
    {
        decideWithModel(1, Value{}, Value{});
        return;
    }
    decided(domain::mix::baseMix(*brief_));
}

void MixSession::decideWithModel(int round, Value previous, Value refusals)
{
    const auto generation = generation_;
    wiring_.copilot->decideMix(
        brief_->toValue(),
        previous,
        refusals,
        [this, alive = alive_, generation, round](domain::Result<Value> answer, Value usage)
        {
            if (!alive->load() || generation != generation_ || cancelled_.load() || brief_ == nullptr)
                return;
            // Two rounds cost two requests: the tokens add up.
            const auto tokens = [](const Value& from, const char* key)
            {
                const auto found = from.intAt(key);
                return found ? found.value() : std::int64_t{0};
            };
            usage_ = Value::object(
                {{"inputTokens", Value{tokens(usage_, "inputTokens") + tokens(usage, "inputTokens")}},
                 {"outputTokens", Value{tokens(usage_, "outputTokens") + tokens(usage, "outputTokens")}}});
            if (!answer)
            {
                decisionNote_ = "Le modèle n'a pas proposé (" + answer.error().message +
                                ") : mixage de base, par les règles.";
                decided(domain::mix::baseMix(*brief_));
                return;
            }
            auto proposal = domain::mix::Proposal::fromValue(answer.value());
            if (!proposal)
            {
                decisionNote_ = "La proposition du modèle était illisible : mixage de base, par les règles.";
                decided(domain::mix::baseMix(*brief_));
                return;
            }
            const auto refused = domain::mix::check(*brief_, proposal.value());
            if (!refused.empty() && round == 1)
            {
                Value::Array said;
                for (const auto& refusal : refused)
                {
                    // Kept in daw.log (S21): what the guards refused at the
                    // first round is shown to the model and never on screen.
                    juce::Logger::writeToLog(toJuce("mix: refusé au premier tour : " + refusal.why));
                    said.push_back(refusal.toValue());
                }
                decideWithModel(2, proposal.value().toValue(), Value::array(std::move(said)));
                return;
            }
            decided(std::move(proposal).value());
        });
}

void MixSession::decided(domain::mix::Proposal proposal)
{
    refusals_ = domain::mix::check(*brief_, proposal);
    for (const auto& refusal : refusals_)
        juce::Logger::writeToLog(toJuce("mix: refusé : " + refusal.why));
    proposal_ = std::make_unique<domain::mix::Proposal>(domain::mix::without(proposal, refusals_));
    verify();
}

// --- verify --------------------------------------------------------------------

void MixSession::verify()
{
    setStage(Stage::verifying, "J'essaie la proposition à blanc, sur une copie du morceau…");
    progress_.store(0.0);
    startTimer(progressPollMs);

    // The proposal applied to a copy of the project: the commands it would
    // write, each applied to that copy only.
    auto proposed = wiring_.state;
    for (const auto& command :
         domain::mix::compile(wiring_.state, *proposal_, [] { return domain::PluginId::generate(); }))
        static_cast<void>(command->apply(proposed));

    render_ =
        engine::MixRender::prepare(wiring_.edit, wiring_.state, &proposed, wiring_.catalogue, wiring_.store);
    if (render_ == nullptr)
    {
        stopTimer();
        setStage(Stage::failed, "La proposition n'a pas pu être essayée.");
        return;
    }
    auto* render = render_.get();
    auto measured = std::make_shared<std::unique_ptr<engine::MixRender::Measured>>();
    auto state = std::make_shared<domain::ProjectState>(std::move(proposed));
    offThread(
        [this, render, measured]
        { *measured = render->run(cancelled_, [this](double done) { progress_.store(done); }); },
        [this, measured, state]
        {
            stopTimer();
            if (*measured == nullptr)
            {
                render_.reset();
                setStage(Stage::failed, "L'essai de la proposition n'a pas abouti.");
                return;
            }
            static_cast<void>(afterFile_.deleteFile());
            afterFile_ = render_->releaseFile();
            render_.reset();
            after_ = std::move(*measured);

            // The master under -1 dBTP, by its fader, and said.
            masterTrim_ = domain::mix::masterTrim(*state, after_->master.truePeakDb);
            masterSentence_.clear();
            if (masterTrim_.has_value())
            {
                masterSentence_ = "Après le mixage, le master atteignait " +
                                  french(after_->master.truePeakDb) + " dBTP : je l'ai baissé de " +
                                  french(wiring_.state.master().volumeDb - *masterTrim_) +
                                  " dB pour rester sous -1 dBTP.";
            }

            // The trim is not in the file, and needs not be: the
            // comparison brings both to the same loudness anyway.
            if (comparison_ != nullptr)
                static_cast<void>(comparison_->load(
                    beforeFile_, before_->master.integratedLufs, afterFile_, after_->master.integratedLufs));

            const auto count = proposal_->changes.size();
            std::string line = "Proposition prête : " + std::to_string(count) + " réglage" +
                               (count > 1 ? "s" : "") + ", par " + proposal_->decidedBy + ".";
            if (!refusals_.empty())
                line += " " + std::to_string(refusals_.size()) + " refusé" +
                        (refusals_.size() > 1 ? "s" : "") + " par les garde-fous.";
            if (!decisionNote_.empty())
                line += " " + decisionNote_;
            // What the person will be asked about later, kept in daw.log: the
            // master before and after, who decided, what it cost, each sentence.
            std::string record = "mix: master avant " + french(before_->master.integratedLufs) + " LUFS, " +
                                 french(before_->master.truePeakDb) + " dBTP ; après l'essai " +
                                 french(after_->master.integratedLufs) + " LUFS, " +
                                 french(after_->master.truePeakDb) + " dBTP ; mesure " +
                                 french(measureSeconds_) + " s ; décidé par " + proposal_->decidedBy;
            if (const auto in = usage_.intAt("inputTokens"); in)
                record += " ; jetons " + std::to_string(in.value()) + " en entrée, " +
                          std::to_string(usage_.intAt("outputTokens") ? usage_.intAt("outputTokens").value()
                                                                       : 0) +
                          " en sortie";
            juce::Logger::writeToLog(toJuce(record));
            for (const auto& strip : brief_->strips)
                juce::Logger::writeToLog(toJuce("mix:   " + strip.name + " (" +
                                                domain::mix::roleLabel(strip.role.role) + ") " +
                                                french(strip.measure.integratedLufs) + " LUFS, joue " +
                                                french(strip.measure.activeShare * 100.0, 0) + " %, fader " +
                                                french(strip.volumeDb) + " dB"));
            for (const auto& change : proposal_->changes)
                juce::Logger::writeToLog(toJuce("mix:   " + change.sentence));
            setStage(Stage::ready, line);
        });
}

// --- the proposal --------------------------------------------------------------

const domain::mix::Proposal* MixSession::proposal() const
{
    return stage_ == Stage::ready ? proposal_.get() : nullptr;
}

const domain::mix::Brief* MixSession::brief() const
{
    return brief_.get();
}

void MixSession::refuseTrack(domain::TrackId track, bool refused)
{
    if (refused)
        refusedTracks_.insert(track.toString());
    else
        refusedTracks_.erase(track.toString());
    sendChangeMessage();
}

bool MixSession::isRefused(domain::TrackId track) const
{
    return refusedTracks_.contains(track.toString());
}

void MixSession::accept()
{
    if (stage_ != Stage::ready || proposal_ == nullptr)
        return;
    if (comparison_ != nullptr)
        comparison_->stop();

    domain::mix::Proposal kept;
    kept.decidedBy = proposal_->decidedBy;
    for (const auto& change : proposal_->changes)
    {
        if (!isRefused(change.track))
            kept.changes.push_back(change);
    }
    auto commands = domain::mix::compile(wiring_.state, kept, [] { return domain::PluginId::generate(); });
    if (masterTrim_.has_value() && !isRefused(domain::ProjectState::masterTrackId()))
        commands.push_back(
            std::make_unique<domain::SetTrackVolume>(domain::ProjectState::masterTrackId(), *masterTrim_));

    if (!commands.empty())
    {
        domain::GroupOptions group{};
        group.label = "Mixage par l'IA : " + std::to_string(kept.changes.size()) + " réglages";
        group.origin.actor = domain::Actor::copilot;

        // What the copilot acted upon, by digest: the measures and the
        // sentences, kept with the project and never in a payload.
        if (wiring_.store != nullptr && brief_ != nullptr)
        {
            auto context = Value::object({{"brief", brief_->toValue()}, {"proposal", kept.toValue()}});
            if (!masterSentence_.empty())
                static_cast<void>(context.set("master", Value{masterSentence_}));
            const auto text = domain::json::write(context);
            if (auto blob = wiring_.store->put(text.data(), text.size()); blob)
                group.origin.context = blob.value();
        }
        if (auto done = wiring_.bus.executeGroup(std::move(commands), group); !done)
        {
            setStage(Stage::failed, "Le mixage n'a pas pu être écrit : " + done.error().message);
            return;
        }
    }
    clearProposal();
    setStage(Stage::idle, "Mixage gardé : une entrée d'historique, un Ctrl+Z le défait.");
}

std::vector<std::string> MixSession::sentencesOf(const domain::BlobRef& context) const
{
    std::vector<std::string> sentences;
    if (wiring_.store == nullptr)
        return sentences;
    const auto bytes = wiring_.store->get(context);
    if (!bytes)
        return sentences;
    const auto value = domain::json::read(
        std::string_view{static_cast<const char*>(bytes.value().getData()), bytes.value().getSize()});
    if (!value)
        return sentences;
    const auto* proposal = value.value().find("proposal");
    if (proposal == nullptr || proposal->find("changes") == nullptr)
        return sentences;
    auto kept = domain::mix::Proposal::fromValue(*proposal);
    if (!kept)
        return sentences;
    for (const auto& change : kept.value().changes)
        sentences.push_back(change.sentence);
    if (const auto master = value.value().stringAt("master"); master)
        sentences.push_back(master.value());
    return sentences;
}

void MixSession::reject()
{
    clearProposal();
    setStage(Stage::idle, "Mixage refusé : rien n'est écrit.");
}

void MixSession::clearProposal()
{
    if (comparison_ != nullptr)
        comparison_->unload();
    proposal_.reset();
    refusals_.clear();
    refusedTracks_.clear();
    masterTrim_.reset();
    masterSentence_.clear();
    after_.reset();
    static_cast<void>(afterFile_.deleteFile());
    afterFile_ = juce::File{};
}

void MixSession::listen(bool after)
{
    if (comparison_ == nullptr || stage_ != Stage::ready)
        return;
    if (!comparison_->playing())
    {
        double seconds = 0.0;
        if (wiring_.clock != nullptr)
            seconds = wiring_.edit.tempoSequence
                          .toTime(tracktion::BeatPosition::fromBeats(wiring_.clock->positionBeats()))
                          .inSeconds();
        comparison_->play(seconds, after);
    }
    else
    {
        comparison_->select(after);
    }
    sendChangeMessage();
}

bool MixSession::listeningAfter() const
{
    return comparison_ != nullptr && comparison_->playing() && comparison_->afterSelected();
}

void MixSession::setAxes(domain::mix::Axes axes)
{
    axes_ = axes;
    if (before_ != nullptr && (stage_ == Stage::ready || stage_ == Stage::failed))
    {
        clearProposal();
        decide();
    }
}

// --- the reference -------------------------------------------------------------

void MixSession::setReference(const std::string& path)
{
    // One render at a time: the reference waits for the song.
    if (stage_ == Stage::measuring || stage_ == Stage::deciding || stage_ == Stage::verifying)
        return;
    cancelled_.store(false);
    const juce::File file{toJuce(path)};
    auto measured = std::make_shared<std::optional<domain::mix::StreamMeasure>>();
    setStage(Stage::measuring, "Je mesure la référence « " + file.getFileName().toStdString() + " »…");
    offThread(
        [file, measured]
        {
            juce::AudioFormatManager formats;
            formats.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> reader{formats.createReaderFor(file)};
            if (reader == nullptr)
                return;
            domain::mix::StreamAnalyser analyser{reader->sampleRate};
            juce::AudioBuffer<float> block{2, 8192};
            for (juce::int64 at = 0; at < reader->lengthInSamples; at += block.getNumSamples())
            {
                const auto count = static_cast<int>(
                    std::min<juce::int64>(block.getNumSamples(), reader->lengthInSamples - at));
                block.clear();
                reader->read(&block, 0, count, at, true, true);
                analyser.process(block.getReadPointer(0),
                                 block.getReadPointer(reader->numChannels > 1 ? 1 : 0),
                                 static_cast<std::size_t>(count));
            }
            *measured = analyser.finish();
        },
        [this, measured, file]
        {
            if (!measured->has_value())
            {
                setStage(Stage::failed,
                         "Cette référence ne se lit pas : " + file.getFileName().toStdString());
                return;
            }
            referenceMeasure_ = std::move(*measured);
            referenceName_ = file.getFileName().toStdString();
            setStage(Stage::idle,
                     "Référence : « " + referenceName_ + " », " + french(referenceMeasure_->integratedLufs) +
                         " LUFS. « Mixer » ira vers elle.");
            if (before_ != nullptr && proposal_ != nullptr)
                decide();
        });
}

void MixSession::clearReference()
{
    referenceMeasure_.reset();
    referenceName_.clear();
    sendChangeMessage();
}

void MixSession::setReferenceAmount(double amount)
{
    referenceAmount_ = std::clamp(amount, 0.0, 1.0);
    if (referenceMeasure_.has_value() && before_ != nullptr && stage_ == Stage::ready)
    {
        clearProposal();
        decide();
    }
}

// --- the copilot -----------------------------------------------------------------

Value MixSession::startFromCopilot(const Value& arguments)
{
    auto axes = axes_;
    if (const auto punch = arguments.doubleAt("punch"); punch)
        axes.punch = std::clamp(punch.value(), -1.0, 1.0);
    if (const auto focus = arguments.doubleAt("focus"); focus)
        axes.focus = std::clamp(focus.value(), -1.0, 1.0);
    if (const auto width = arguments.doubleAt("width"); width)
        axes.width = std::clamp(width.value(), -1.0, 1.0);
    axes_ = axes;
    start();
    return Value::object(
        {{"started", Value{stage_ != Stage::failed}},
         {"status", Value{status_}},
         {"axes", axes_.toValue()},
         {"says",
          Value{std::string{"Le mixage se mesure puis s'essaie à blanc ; la proposition s'affiche dans "
                            "le mixer, réglage par réglage, avec sa phrase. La personne l'écoute avant "
                            "et après et la garde ou la refuse. Rien n'est écrit avant."}}}});
}

} // namespace daw::app
