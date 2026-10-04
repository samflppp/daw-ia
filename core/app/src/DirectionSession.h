#pragma once

#include "StemSeparation.h"
#include "daw/domain/command/CommandBus.h"
#include "daw/domain/project/ProjectState.h"
#include "daw/ui/model/DirectionHost.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace daw::app
{

// The application's answer to DirectionHost (S22).
//
// Adding a reference: its bytes digested (its name in the project, never its
// path), separated into stems by the fast model (another process, cached
// under the digest), the stems read by domain::direction::read on a thread of
// its own, and the direction written by one direction.set. The stems of a
// reference serve the reading and never enter the project: only numbers do.
class DirectionSession final : public ui::DirectionHost
{
public:
    struct Wiring
    {
        domain::CommandBus& bus;
        const domain::ProjectState& state;
        juce::File services;
        juce::File cache;
        std::string fake; // "fake": the band filters, for a verification without a model
    };

    explicit DirectionSession(Wiring wiring);
    ~DirectionSession() override;

    DirectionSession(const DirectionSession&) = delete;
    DirectionSession& operator=(const DirectionSession&) = delete;
    DirectionSession(DirectionSession&&) = delete;
    DirectionSession& operator=(DirectionSession&&) = delete;

    [[nodiscard]] Stage stage() const override;
    [[nodiscard]] double progress() const override;
    [[nodiscard]] std::string status() const override;

    void addReference(const std::string& path) override;
    void cancel() override;
    void removeReference(const std::string& digest) override;
    void setWeight(const std::string& digest, double weight) override;
    void setAmount(double amount) override;
    void correctTempo(std::optional<double> bpm) override;
    void correctKey(std::optional<domain::generation::Key> key) override;
    void clear() override;

    // Told after every direction.set this session writes: the mix decides
    // again when a proposal is on screen.
    std::function<void()> onDirectionChanged;

private:
    void write(domain::direction::Direction direction, const std::string& label);
    void separated(const std::string& name,
                   const std::string& digest,
                   domain::Result<StemSeparation::Separated> result);
    void read(domain::Result<domain::direction::Reading> reading);
    void setFailure(std::string message);

    Wiring wiring_;
    std::unique_ptr<StemSeparation> separation_;
    Stage stage_{Stage::idle};
    std::string status_;
    std::uint64_t generation_{0}; // cancel() moves it: a late answer is dropped
    std::shared_ptr<std::atomic<bool>> alive_;

    struct Poll;
    std::unique_ptr<Poll> poll_;
};

} // namespace daw::app
