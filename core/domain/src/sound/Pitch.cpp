#include "daw/domain/sound/Pitch.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace daw::domain::sound
{

double fundamentalOf(const float* samples, int count, double sampleRate, double lowest, double highest)
{
    constexpr double threshold = 0.15;

    const auto tauMin = std::max(2, static_cast<int>(sampleRate / highest));
    const auto tauMax = std::min(count / 2, static_cast<int>(sampleRate / lowest));
    const auto width = count - tauMax;
    if (samples == nullptr || tauMax <= tauMin + 2 || width <= 0)
        return 0.0;

    const auto at = [](int index) { return static_cast<std::size_t>(index); };

    std::vector<double> difference(at(tauMax + 1), 0.0);
    for (int tau = 1; tau <= tauMax; ++tau)
    {
        double sum = 0.0;
        for (int j = 0; j < width; ++j)
        {
            const auto delta = static_cast<double>(samples[j]) - static_cast<double>(samples[j + tau]);
            sum += delta * delta;
        }
        difference[at(tau)] = sum;
    }

    // The cumulative mean normalised difference: the first dip under the
    // threshold is the period, not the deepest one, which is often a multiple.
    std::vector<double> normalised(difference.size(), 1.0);
    double running = 0.0;
    for (int tau = 1; tau <= tauMax; ++tau)
    {
        running += difference[at(tau)];
        normalised[at(tau)] = running > 0.0 ? difference[at(tau)] * tau / running : 1.0;
    }

    auto best = -1;
    for (int tau = tauMin; tau < tauMax; ++tau)
    {
        if (normalised[at(tau)] < threshold)
        {
            while (tau + 1 < tauMax && normalised[at(tau + 1)] < normalised[at(tau)])
                ++tau;
            best = tau;
            break;
        }
    }
    if (best < 0)
        return 0.0;

    // A parabola through the dip and its neighbours: a period finer than a
    // sample.
    const auto a = normalised[at(best - 1)];
    const auto b = normalised[at(best)];
    const auto c = normalised[at(best + 1)];
    const auto shift = (a - c) / (2.0 * (a - 2.0 * b + c));
    const auto period = static_cast<double>(best) + (std::isfinite(shift) ? shift : 0.0);
    return sampleRate / period;
}

int midiPitchOf(double hertz)
{
    return static_cast<int>(std::lround(69.0 + 12.0 * std::log2(hertz / 440.0)));
}

} // namespace daw::domain::sound
