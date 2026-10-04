#include "daw/ui/model/PromptReader.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace daw::ui
{

PromptReader::Reading LocalPromptReader::parse(std::string_view text, const Zone& zone)
{
    Reading out;
    out.interpretation = domain::generation::LocalInterpreter::parse(text);

    // "plus sombre" asks to rework: its words are used, not ignored — when
    // there are notes to rework.
    if (!zone.hasNotes)
        return out;
    if (auto asked = domain::generation::readTransform(text); asked.has_value())
    {
        out.transform = asked->transform;
        auto& ignored = out.interpretation.ignored;
        ignored.erase(std::remove_if(ignored.begin(),
                                     ignored.end(),
                                     [&asked](const std::string& word)
                                     {
                                         std::string lower = word;
                                         for (auto& c : lower)
                                             c = static_cast<char>(
                                                 std::tolower(static_cast<unsigned char>(c)));
                                         return std::find(asked->words.begin(), asked->words.end(), lower) !=
                                                asked->words.end();
                                     }),
                      ignored.end());
    }
    return out;
}

void LocalPromptReader::read(std::string text, Zone zone, Done done)
{
    if (done)
        done(parse(text, zone));
}

RoutedPromptReader::RoutedPromptReader(Remote remote)
    : remote_(std::move(remote))
{
}

void RoutedPromptReader::read(std::string text, Zone zone, Done done)
{
    // A second prompt replaces the first: the window only ever shows the
    // answer to the last question asked.
    pending_ = Pending{nextTicket_++, std::move(text), zone, std::move(done)};

    if (!remote_.available || !remote_.available() || !remote_.ask)
    {
        local(offlineNotice);
        return;
    }

    const auto ticket = pending_->ticket;
    remote_.ask(ticket,
                pending_->text,
                zone,
                [this](std::uint64_t answeredTicket, domain::Result<Reading> result)
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

void RoutedPromptReader::answered(std::uint64_t ticket, domain::Result<Reading> result)
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

    auto out = std::move(result).value();
    out.remote = true;
    out.notice.clear();
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

    auto out = LocalPromptReader::parse(taken.text, taken.zone);
    out.notice = std::string{notice};
    taken.done(std::move(out));
}

} // namespace daw::ui
