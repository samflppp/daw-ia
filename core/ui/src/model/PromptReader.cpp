#include "daw/ui/model/PromptReader.h"

#include <utility>

namespace daw::ui
{

void LocalPromptReader::read(std::string text, Zone zone, Done done)
{
    static_cast<void>(zone);
    if (!done)
        return;

    Reading out;
    out.interpretation = domain::generation::LocalInterpreter::parse(text);
    done(std::move(out));
}

RoutedPromptReader::RoutedPromptReader(Remote remote)
    : remote_(std::move(remote))
{
}

void RoutedPromptReader::read(std::string text, Zone zone, Done done)
{
    // A second prompt replaces the first: the window only ever shows the
    // answer to the last question asked.
    pending_ = Pending{nextTicket_++, std::move(text), std::move(done)};

    if (!remote_.available || !remote_.available() || !remote_.ask)
    {
        local(offlineNotice);
        return;
    }

    const auto ticket = pending_->ticket;
    remote_.ask(
        ticket,
        pending_->text,
        zone,
        [this](std::uint64_t answeredTicket, domain::Result<domain::generation::Interpretation> result)
        { answered(answeredTicket, std::move(result)); });
}

void RoutedPromptReader::cancel()
{
    pending_.reset();
}

void RoutedPromptReader::expire()
{
    if (pending_.has_value())
        local(slowNotice);
}

void RoutedPromptReader::answered(std::uint64_t ticket,
                                  domain::Result<domain::generation::Interpretation> result)
{
    // Late, cancelled, or replaced by another prompt: not the question on
    // screen any more.
    if (!pending_.has_value() || pending_->ticket != ticket)
        return;

    if (!result)
    {
        local(failedNotice);
        return;
    }

    auto done = std::move(pending_->done);
    pending_.reset();
    if (!done)
        return;

    Reading out;
    out.interpretation = std::move(result).value();
    out.remote = true;
    done(std::move(out));
}

void RoutedPromptReader::local(std::string_view notice)
{
    if (!pending_.has_value())
        return;

    auto taken = std::move(*pending_);
    pending_.reset();
    if (!taken.done)
        return;

    Reading out;
    out.interpretation = domain::generation::LocalInterpreter::parse(taken.text);
    out.notice = std::string{notice};
    taken.done(std::move(out));
}

} // namespace daw::ui
