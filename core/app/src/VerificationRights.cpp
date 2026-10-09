#include "BusSession.h"
#include "CopilotBridge.h"
#include "DirectionSession.h"
#include "KitSession.h"
#include "MixSession.h"
#include "StemSession.h"
#include "Verification.h"
#include "VoiceInput.h"
#include "daw/domain/rights/Rights.h"
#include "daw/domain/serialization/Json.h"
#include "daw/domain/voice/PushToTalk.h"

#include <memory>
#include <string>

// --verify-droits (S26): « ai-je le droit ? », the one place a feature asks
// it (domain::rights), answered no to everything for once. Each feature, at
// the door a person uses, says it and changes nothing: the project is the
// same, to the byte. The generation is refused the same way in --verify,
// where its window is built. The copilot needs no key here: it refuses
// before it would send anything.

namespace daw::app
{

void Verification::buildRights()
{
    auto before = std::make_shared<std::string>();
    const auto refused = [](domain::rights::Feature feature) { return domain::rights::refusal(feature); };

    add("toutes les réponses à non",
        [this, before]
        {
            *before = domain::json::write(state_.toValue());
            domain::rights::refuseAllForCheck(true);
            check(!domain::rights::allows(domain::rights::Feature::copilot), "le droit répond non");
        });

    add("le copilote",
        [this, refused]
        {
            copilot_.ask("Ajoute une piste de basse.");
            const auto& lines = copilot_.transcript();
            check(!lines.empty() && lines.back().text == refused(domain::rights::Feature::copilot),
                  "« " + (lines.empty() ? std::string{} : lines.back().text) + " »");
        });

    add("le mixage par l'IA",
        [this, refused]
        {
            if (mix_ == nullptr)
                return check(false, "le mixage est branché");
            mix_->start();
            check(mix_->stage() == ui::MixHost::Stage::failed &&
                      mix_->status() == refused(domain::rights::Feature::mix),
                  "« " + mix_->status() + " »");
        });

    add("la séparation en stems",
        [this, refused]
        {
            if (stems_ == nullptr)
                return check(false, "le séparateur est branché");
            stems_->separate(domain::AudioClipId::generate(), ui::StemHost::Quality::fast);
            check(stems_->status() == refused(domain::rights::Feature::stems),
                  "« " + stems_->status() + " »");
        });

    add("le kit",
        [this, refused]
        {
            if (kitSession_ == nullptr)
                return check(false, "le kit est branché");
            kitSession_->choose(domain::kit::Axes{});
            check(kitSession_->status() == refused(domain::rights::Feature::kit),
                  "« " + kitSession_->status() + " »");
        });

    add("les bus intelligents",
        [this, refused]
        {
            if (busSession_ == nullptr)
                return check(false, "les bus sont branchés");
            busSession_->propose();
            check(busSession_->status() == refused(domain::rights::Feature::buses),
                  "« " + busSession_->status() + " »");
        });

    add("la direction par références",
        [this, refused]
        {
            if (direction_ == nullptr)
                return check(false, "la direction est branchée");
            direction_->addReference(folder_.getChildFile("reference.wav").getFullPathName().toStdString());
            check(direction_->status() == refused(domain::rights::Feature::direction),
                  "« " + direction_->status() + " »");
        });

    add(
        "la voix : Ctrl droit tenu",
        [this]
        {
            if (voice_ == nullptr)
                return check(false, "la voix est branchée");
            voice_->keyForTest(domain::voice::PushToTalk::keyScanCode, true, true);
        },
        [this] { return voice_ == nullptr || voice_->stage() == ui::VoiceHost::Stage::failed; },
        3000.0);
    add("la voix : la touche dit le refus, le micro reste fermé",
        [this, refused]
        {
            if (voice_ == nullptr)
                return;
            check(voice_->message() == refused(domain::rights::Feature::voice),
                  "« " + voice_->message() + " »");
            check(!voice_->microphoneDevice().isOpen(), "le micro fermé");
            voice_->keyForTest(domain::voice::PushToTalk::keyScanCode, true, false);
        });

    add("le projet n'a pas bougé, et les réponses reviennent à oui",
        [this, before]
        {
            domain::rights::refuseAllForCheck(false);
            check(domain::json::write(state_.toValue()) == *before, "le projet à l'octet");
            check(domain::rights::allows(domain::rights::Feature::copilot), "le droit répond oui");
        });
}

} // namespace daw::app
