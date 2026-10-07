#include "VoiceInput.h"

#include "daw/domain/live/Router.h"
#include "daw/domain/serialization/Json.h"
#include "daw/engine/ProjectProjector.h"

#include <algorithm>
#include <utility>

namespace daw::app
{
namespace
{

using domain::Value;
using domain::voice::PushToTalk;

constexpr int tickMs = 33;
constexpr int installPollMs = 250;

juce::String fromUtf8(const std::string& text)
{
    return juce::String::fromUTF8(text.data(), static_cast<int>(text.size()));
}

std::string joined(const std::vector<std::string>& lines)
{
    std::string out;
    for (const auto& line : lines)
        out += (out.empty() ? "" : " ; ") + line;
    return out;
}

} // namespace

VoiceInput::VoiceInput(Wiring wiring)
    : wiring_(std::move(wiring))
    , service_(wiring_.services, wiring_.replay)
{
    inFront = [] { return juce::Process::isForegroundProcess(); };
    stage_ = service_.stage() == VoiceService::Stage::absent ? Stage::absent : Stage::idle;
}

VoiceInput::~VoiceInput()
{
    cancelPendingUpdate();
    stopTimer();
    mic_.discard();
    duck(false);
    if (confirmAnswer_)
        std::exchange(confirmAnswer_, nullptr)(false);
}

// --- the keys -----------------------------------------------------------------

void VoiceInput::keyEvent(int scanCode, bool extended, bool down, bool front, double seconds)
{
    {
        const std::lock_guard<std::mutex> lock{keysMutex_};
        keys_.push_back(Key{scanCode, extended, down, front, seconds});
    }
    triggerAsyncUpdate();
}

void VoiceInput::keyForTest(int scanCode, bool extended, bool down)
{
    keyEvent(scanCode, extended, down, true, domain::live::now());
}

void VoiceInput::handleAsyncUpdate()
{
    std::vector<Key> keys;
    {
        const std::lock_guard<std::mutex> lock{keysMutex_};
        keys.swap(keys_);
    }
    for (const auto& key : keys)
    {
        // A key pressed in another application is not for us: neither the
        // key that opens the microphone, nor one that holds a phrase. Its
        // release still comes, and still ends what it began.
        if (key.down && !key.inFront)
            continue;
        const bool ours = key.scanCode == PushToTalk::keyScanCode && key.extended;
        // Any key holds a sure phrase that counts down: it stays, and waits.
        if (key.down && !ours && stage_ == Stage::sure)
            holdPhrase();
        apply(talk_.key(key.scanCode, key.extended, key.down, key.seconds), key.seconds);
    }
}

void VoiceInput::press()
{
    const auto now = domain::live::now();
    apply(talk_.button(true, now), now);
}

void VoiceInput::release()
{
    const auto now = domain::live::now();
    apply(talk_.button(false, now), now);
}

void VoiceInput::apply(const PushToTalk::Step& step, double seconds)
{
    if (step.openMicrophone)
    {
        // A phrase shown and not sent is dropped by the next one.
        heard_.reset();
        removals_.clear();
        if (service_.stage() == VoiceService::Stage::absent ||
            service_.stage() == VoiceService::Stage::installing)
        {
            setStage(
                service_.stage() == VoiceService::Stage::absent ? Stage::absent : Stage::installing,
                "La reconnaissance vocale n'est pas installée : le bouton l'installe. Le clavier marche.");
            return;
        }
        juce::String error;
        if (!mic_.open(chosenMicrophone(), error))
        {
            juce::Logger::writeToLog("voix: " + error);
            setStage(Stage::failed, error.toStdString());
            return;
        }
        juce::Logger::writeToLog(juce::String::fromUTF8("voix: micro ouvert, « ") + mic_.openedName() +
                                 juce::String::fromUTF8(" »"));
        duck(true);
        service_.warmUp();
        levelDb_ = -100.0f;
        setStage(Stage::opening);
        startTimer(tickMs);
        return;
    }

    if (!step.closeMicrophone)
        return;

    duck(false);
    if (!mic_.isOpen())
    {
        // Opened nothing (not installed, no microphone): nothing to hear.
        talk_.transcribed();
        return;
    }
    if (step.ended != PushToTalk::Ended::released)
    {
        mic_.discard();
        juce::Logger::writeToLog(juce::String::fromUTF8("voix: ") +
                                 juce::String::fromUTF8(describe(step.ended)));
        setStage(Stage::idle, domain::voice::describe(step.ended));
        return;
    }

    auto samples = mic_.close();
    releasedAt_ = seconds;
    setStage(Stage::transcribing, "Je transcris…");
    juce::Logger::writeToLog(
        "voix: " + juce::String{static_cast<double>(samples.size()) / app::Microphone::rate16k, 2} +
        juce::String::fromUTF8(" s entendues, envoyées au transcripteur"));
    service_.transcribe(samples,
                        projectNames(),
                        [this](bool ok, VoiceService::Heard result, std::string failure)
                        { heard(ok, std::move(result), std::move(failure)); });
    std::fill(samples.begin(), samples.end(), 0.0f); // the voice is kept nowhere
}

void VoiceInput::heard(bool ok, VoiceService::Heard result, std::string failure)
{
    talk_.transcribed();
    shownAt_ = domain::live::now();
    latency_ = shownAt_ - releasedAt_;
    if (!ok)
    {
        juce::Logger::writeToLog(fromUtf8("voix: " + failure));
        setStage(service_.stage() == VoiceService::Stage::absent ? Stage::absent : Stage::failed, failure);
        return;
    }
    juce::Logger::writeToLog(
        fromUtf8("voix: « " + result.text + " » en " + std::to_string(latency_) + " s (" +
                 (result.doubtful ? "douteuse : " + joined(result.reasons) : "sûre") + ")"));
    const auto doubtful = result.doubtful;
    auto reasons = joined(result.reasons);
    heard_ = std::move(result);
    if (doubtful)
        setStage(Stage::doubtful, "Douteuse : " + reasons + ". Corrige, puis Entrée.");
    else
    {
        setStage(Stage::sure, {});
        startTimer(tickMs);
    }
}

void VoiceInput::timerCallback()
{
    const auto now = domain::live::now();
    if (stage_ == Stage::opening || stage_ == Stage::listening)
    {
        levelDb_ = mic_.takePeakDb();
        if (stage_ == Stage::opening && mic_.hearing())
            setStage(Stage::listening);
        apply(talk_.tick(now), now);
        if (talk_.state() == PushToTalk::State::listening && inFront && !inFront())
            apply(talk_.focusLost(now), now);
        sendChangeMessage();
        return;
    }
    if (stage_ == Stage::sure)
    {
        if (now - shownAt_ >= sureDelaySeconds && heard_.has_value())
            sendPhrase(heard_->text);
        sendChangeMessage();
        return;
    }
    if (stage_ == Stage::installing)
    {
        if (service_.stage() == VoiceService::Stage::stopped)
            setStage(Stage::idle, "La reconnaissance vocale est installée.");
        else if (service_.stage() == VoiceService::Stage::failed)
            setStage(Stage::failed, service_.status());
        sendChangeMessage();
        return;
    }
    stopTimer();
}

// --- what is shown ---------------------------------------------------------------

void VoiceInput::setStage(Stage stage, std::string message)
{
    stage_ = stage;
    message_ = std::move(message);
    sendChangeMessage();
}

double VoiceInput::elapsed() const
{
    return talk_.state() == PushToTalk::State::listening ? domain::live::now() - talk_.heldSince() : 0.0;
}

double VoiceInput::countdown() const
{
    if (stage_ != Stage::sure)
        return 0.0;
    return std::clamp(1.0 - (domain::live::now() - shownAt_) / sureDelaySeconds, 0.0, 1.0);
}

std::vector<ui::VoiceHost::Word> VoiceInput::words() const
{
    std::vector<Word> out;
    if (heard_.has_value())
        for (const auto& word : heard_->words)
            out.push_back(Word{word.text, word.uncertain, word.heard});
    return out;
}

// --- what leaves ----------------------------------------------------------------

void VoiceInput::holdPhrase()
{
    if (stage_ != Stage::sure)
        return;
    juce::Logger::writeToLog("voix: phrase retenue");
    setStage(Stage::doubtful, "Retenue : Entrée l'envoie, Échap l'oublie.");
}

void VoiceInput::sendPhrase(const std::string& asked)
{
    if (!heard_.has_value())
        return;
    // A copy: `asked` may be the phrase heard itself, which is dropped below.
    const std::string text = asked;
    const auto& said = *heard_;
    Value::Array confidences;
    for (const auto& word : said.words)
        confidences.push_back(
            Value::object({{"mot", Value{word.text}}, {"confiance", Value{word.confidence}}}));
    Value::Array reasons;
    for (const auto& reason : said.reasons)
        reasons.push_back(Value{reason});
    // What the journal keeps of a phrase said aloud: never the voice.
    const auto spoken = Value::object({{"source", Value{std::string{"voix"}}},
                                       {"entendu", Value{said.text}},
                                       {"envoye", Value{text}},
                                       {"corrigee", Value{text != said.text}},
                                       {"douteuse", Value{said.doubtful}},
                                       {"raisons", Value::array(std::move(reasons))},
                                       {"mots", Value::array(std::move(confidences))},
                                       {"transcripteur", Value{std::string{"parakeet-tdt-0.6b-v3-int8"}}}});
    heard_.reset();
    setStage(Stage::idle);
    juce::Logger::writeToLog(fromUtf8("voix: phrase envoyée au copilote, « " + text + " »"));
    if (wiring_.send)
        wiring_.send(text, spoken);
}

void VoiceInput::dropPhrase()
{
    heard_.reset();
    setStage(Stage::idle, "Phrase oubliée.");
}

void VoiceInput::askToConfirm(std::vector<std::string> removals, std::function<void(bool)> answer)
{
    if (confirmAnswer_)
        std::exchange(confirmAnswer_, nullptr)(false);
    removals_ = std::move(removals);
    confirmAnswer_ = std::move(answer);
    setStage(Stage::confirming, "La phrase dite " + joined(removals_) + " : confirmer ?");
}

void VoiceInput::confirm(bool accepted)
{
    if (!confirmAnswer_)
        return;
    std::exchange(confirmAnswer_, nullptr)(accepted);
    removals_.clear();
    setStage(Stage::idle, accepted ? std::string{} : std::string{"Rien n'est écrit."});
}

void VoiceInput::install()
{
    service_.install();
    setStage(Stage::installing, "Installation de la reconnaissance vocale (environ 500 Mo)…");
    startTimer(installPollMs);
}

void VoiceInput::duck(bool on)
{
    if (wiring_.projector != nullptr)
        wiring_.projector->setDucking(on ? duckDb : 0.0f);
}

std::vector<std::string> VoiceInput::projectNames() const
{
    std::vector<std::string> names;
    const auto add = [&names](const std::string& name)
    {
        if (!name.empty() && std::find(names.begin(), names.end(), name) == names.end())
            names.push_back(name);
    };
    for (const auto* tracks : {&wiring_.state.tracks(), &wiring_.state.buses()})
        for (const auto& track : *tracks)
        {
            add(track.name);
            for (const auto& plugin : track.plugins)
                add(plugin.ref.name);
        }
    for (const auto& plugin : wiring_.state.master().plugins)
        add(plugin.ref.name);
    for (const auto& pattern : wiring_.state.patterns())
        add(pattern.name);
    return names;
}

// --- the microphone chosen: this machine's ------------------------------------------

juce::File VoiceInput::choiceFile() const
{
    return wiring_.settings.getChildFile("micro.json");
}

juce::String VoiceInput::chosenMicrophone() const
{
    const auto inputs = mic_.inputs();
    const auto kept = juce::JSON::parse(choiceFile()).getProperty("micro", {}).toString();
    for (const auto& input : inputs)
        if (input.name == kept)
            return kept;
    return app::Microphone::defaultFrom(inputs);
}

std::vector<ui::VoiceHost::Microphone> VoiceInput::microphones() const
{
    std::vector<ui::VoiceHost::Microphone> out;
    for (const auto& input : mic_.inputs())
        out.push_back({input.name.toStdString(), input.bluetooth});
    return out;
}

std::string VoiceInput::microphone() const
{
    return chosenMicrophone().toStdString();
}

void VoiceInput::chooseMicrophone(const std::string& name)
{
    auto* object = new juce::DynamicObject();
    object->setProperty("micro", fromUtf8(name));
    static_cast<void>(choiceFile().replaceWithText(juce::JSON::toString(juce::var{object})));
    juce::Logger::writeToLog(fromUtf8("voix: micro choisi, « " + name + " »"));
    sendChangeMessage();
}

} // namespace daw::app
