#include "DirectionSession.h"

#include "daw/domain/commands/DirectionCommands.h"
#include "daw/domain/rights/Rights.h"
#include "daw/engine/ContentStore.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>

#include <algorithm>
#include <map>
#include <thread>
#include <utility>

namespace daw::app
{

namespace
{

// How often the panel is told a reading moved: a progress bar.
constexpr int pollHz = 10;

juce::String toJuce(const std::string& text)
{
    return juce::String::fromUTF8(text.c_str());
}

// A stem, read whole: a reference is minutes, read once.
std::optional<domain::direction::Stereo> readStem(const juce::File& file, double& rate)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader{formats.createReaderFor(file)};
    if (reader == nullptr || reader->lengthInSamples <= 0)
        return std::nullopt;
    rate = reader->sampleRate;
    const auto frames = static_cast<int>(reader->lengthInSamples);
    juce::AudioBuffer<float> buffer{2, frames};
    static_cast<void>(reader->read(&buffer, 0, frames, 0, true, true));
    domain::direction::Stereo stereo;
    stereo.left.assign(buffer.getReadPointer(0), buffer.getReadPointer(0) + frames);
    stereo.right.assign(buffer.getReadPointer(1), buffer.getReadPointer(1) + frames);
    return stereo;
}

} // namespace

struct DirectionSession::Poll final : juce::Timer
{
    explicit Poll(DirectionSession& owner)
        : session{owner}
    {
    }

    void timerCallback() override
    {
        session.sendChangeMessage();
        if (session.stage_ != Stage::reading)
            stopTimer();
    }

    DirectionSession& session;
};

DirectionSession::DirectionSession(Wiring wiring)
    : wiring_{std::move(wiring)}
    , alive_{std::make_shared<std::atomic<bool>>(true)}
    , poll_{std::make_unique<Poll>(*this)}
{
    separation_ = wiring_.fake.empty()
                      ? std::make_unique<StemSeparation>(wiring_.services, wiring_.cache)
                      : std::make_unique<StemSeparation>(wiring_.services, wiring_.cache, wiring_.fake);
}

DirectionSession::~DirectionSession()
{
    alive_->store(false);
    poll_->stopTimer();
}

DirectionSession::Stage DirectionSession::stage() const
{
    return stage_;
}

double DirectionSession::progress() const
{
    // The separation is most of the time; the reading, the rest.
    return stage_ == Stage::reading ? 0.9 * separation_->progress() : 0.0;
}

std::string DirectionSession::status() const
{
    if (stage_ == Stage::reading && separation_->running())
        return "Référence : " + separation_->status();
    return status_;
}

void DirectionSession::setFailure(std::string message)
{
    stage_ = Stage::failed;
    status_ = std::move(message);
    juce::Logger::writeToLog(toJuce("direction: " + status_));
    sendChangeMessage();
}

void DirectionSession::addReference(const std::string& path)
{
    if (stage_ == Stage::reading)
        return;
    if (!domain::rights::allows(domain::rights::Feature::direction))
    {
        setFailure(domain::rights::refusal(domain::rights::Feature::direction));
        return;
    }
    const juce::File file{toJuce(path)};
    if (!file.existsAsFile())
    {
        setFailure("Cette référence n'existe pas : " + path);
        return;
    }

    stage_ = Stage::reading;
    status_ = "Je lis la référence « " + file.getFileName().toStdString() + " »…";
    const auto generation = ++generation_;
    poll_->startTimerHz(pollHz);
    sendChangeMessage();

    // The digest off the message thread: a song is tens of megabytes.
    std::thread{
        [alive = alive_, generation, file, this]
        {
            juce::MemoryBlock bytes;
            const auto loaded = file.loadFileAsData(bytes);
            const auto digest =
                loaded ? engine::ContentStore::digestOf(bytes.getData(), bytes.getSize()) : std::string{};
            juce::MessageManager::callAsync(
                [alive, generation, file, digest, this]
                {
                    if (!alive->load() || generation != generation_)
                        return;
                    if (digest.empty())
                    {
                        setFailure("Cette référence ne se lit pas : " + file.getFileName().toStdString());
                        return;
                    }
                    const auto name = file.getFileName().toStdString();
                    auto started = separation_->start(
                        file,
                        digest,
                        StemSeparation::Model::fast,
                        [this, name, digest](domain::Result<StemSeparation::Separated> result)
                        { separated(name, digest, std::move(result)); });
                    if (!started)
                        setFailure(started.error().message);
                });
        }}
        .detach();
}

void DirectionSession::separated(const std::string& name,
                                 const std::string& digest,
                                 domain::Result<StemSeparation::Separated> result)
{
    if (!result)
    {
        setFailure("La référence n'a pas pu être séparée : " + result.error().message);
        return;
    }
    status_ = "Je mesure la référence « " + name + " »…";
    sendChangeMessage();

    // Read off the message thread: FFTs over minutes of four stems.
    const auto generation = generation_;
    std::thread{[alive = alive_, generation, name, digest, stems = std::move(result).value().stems, this]
                {
                    std::map<std::string, domain::direction::Stereo> audio;
                    double rate = 44100.0;
                    for (const auto& [stem, file] : stems)
                        if (auto read = readStem(file, rate); read.has_value())
                            audio.emplace(stem, std::move(read).value());

                    domain::Result<domain::direction::Reading> reading =
                        audio.empty() ? domain::Result<domain::direction::Reading>{domain::fail(
                                            domain::ErrorCode::notFound, "no stem could be read")}
                                      : domain::Result<domain::direction::Reading>{
                                            domain::direction::read(audio, rate, name, digest)};
                    juce::MessageManager::callAsync(
                        [alive, generation, reading = std::move(reading), this]() mutable
                        {
                            if (!alive->load() || generation != generation_)
                                return;
                            read(std::move(reading));
                        });
                }}
        .detach();
}

void DirectionSession::read(domain::Result<domain::direction::Reading> reading)
{
    if (!reading)
    {
        setFailure("La référence n'a pas pu être mesurée : " + reading.error().message);
        return;
    }

    auto direction = wiring_.state.direction();
    // The same file twice is one reference: its reading is replaced.
    direction.references.erase(std::remove_if(direction.references.begin(),
                                              direction.references.end(),
                                              [&reading](const domain::direction::Reference& reference)
                                              { return reference.reading.digest == reading.value().digest; }),
                               direction.references.end());
    domain::direction::Reference reference;
    reference.reading = std::move(reading).value();
    const auto name = reference.reading.name;
    direction.references.push_back(std::move(reference));

    stage_ = Stage::idle;
    status_ = "Référence « " + name + " » lue.";
    write(std::move(direction), "référence : " + name);
}

void DirectionSession::cancel()
{
    ++generation_;
    separation_->cancel();
    if (stage_ == Stage::reading)
    {
        stage_ = Stage::idle;
        status_ = "Lecture de la référence annulée.";
    }
    sendChangeMessage();
}

void DirectionSession::write(domain::direction::Direction direction, const std::string& label)
{
    domain::GroupOptions group{};
    group.label = "direction : " + label;
    std::vector<std::unique_ptr<domain::Command>> commands;
    commands.push_back(std::make_unique<domain::SetDirection>(std::move(direction)));
    if (auto done = wiring_.bus.executeGroup(std::move(commands), group); !done)
    {
        setFailure("La direction n'a pas pu s'écrire : " + done.error().message);
        return;
    }
    sendChangeMessage();
    if (onDirectionChanged)
        onDirectionChanged();
}

void DirectionSession::removeReference(const std::string& digest)
{
    auto direction = wiring_.state.direction();
    std::string name;
    direction.references.erase(std::remove_if(direction.references.begin(),
                                              direction.references.end(),
                                              [&digest, &name](const domain::direction::Reference& reference)
                                              {
                                                  if (reference.reading.digest != digest)
                                                      return false;
                                                  name = reference.reading.name;
                                                  return true;
                                              }),
                               direction.references.end());
    if (name.empty())
        return;
    write(std::move(direction), "retirer " + name);
}

void DirectionSession::setWeight(const std::string& digest, double weight)
{
    if (!(weight > 0.0))
        return;
    auto direction = wiring_.state.direction();
    for (auto& reference : direction.references)
        if (reference.reading.digest == digest)
            reference.weight = weight;
    write(std::move(direction), "poids");
}

void DirectionSession::setAmount(double amount)
{
    auto direction = wiring_.state.direction();
    direction.amount = std::clamp(amount, 0.0, 1.0);
    write(std::move(direction), "part de direction");
}

void DirectionSession::correctTempo(std::optional<double> bpm)
{
    auto direction = wiring_.state.direction();
    direction.corrections.bpm = bpm;
    write(std::move(direction), bpm ? "tempo corrigé" : "tempo rendu aux références");
}

void DirectionSession::correctKey(std::optional<domain::generation::Key> key)
{
    auto direction = wiring_.state.direction();
    direction.corrections.key = key;
    write(std::move(direction), key ? "tonalité corrigée" : "tonalité rendue aux références");
}

void DirectionSession::clear()
{
    write(domain::direction::Direction{}, "retirée");
}

} // namespace daw::app
