#include "daw/domain/rights/Rights.h"

#include <atomic>

namespace daw::domain::rights
{
namespace
{

std::atomic<bool> refusingForCheck{false};

const char* nameOf(Feature feature) noexcept
{
    switch (feature)
    {
    case Feature::copilot:
        return "le copilote";
    case Feature::mixByModel:
        return "le mixage par le modèle";
    case Feature::mix:
        return "le mixage par l'IA";
    case Feature::generation:
        return "la génération";
    case Feature::stems:
        return "la séparation en stems";
    case Feature::voice:
        return "la voix";
    case Feature::kit:
        return "le kit";
    case Feature::buses:
        return "les bus intelligents";
    case Feature::direction:
        return "la direction par références";
    }
    return "cette fonction";
}

} // namespace

Route routeOf(Feature feature) noexcept
{
    return feature == Feature::copilot || feature == Feature::mixByModel ? Route::api : Route::local;
}

bool allows(Feature) noexcept
{
    // The prototype: everything, for everyone. A licence will answer here,
    // by routeOf(feature), and nowhere else.
    return !refusingForCheck.load();
}

std::string refusal(Feature feature)
{
    return std::string{"Ta licence ne comprend pas "} + nameOf(feature) + " : rien n'a changé.";
}

void refuseAllForCheck(bool refuse) noexcept
{
    refusingForCheck.store(refuse);
}

} // namespace daw::domain::rights
