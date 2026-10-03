#include "daw/domain/project/InternalEffects.h"

#include "daw/domain/project/ProjectState.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>

namespace daw::domain
{
namespace
{

std::vector<InternalEffect> build()
{
    using namespace internal;
    std::vector<InternalEffect> effects;

    // The bounds of the 4-band equaliser are Tracktion's own (±20 dB, Q 0.1 to
    // 4, 20 Hz to 20 kHz); the high-pass stops at 1 kHz, past which it is no
    // longer cleaning a track but removing it.
    effects.push_back(
        InternalEffect{equaliser,
                       "Égaliseur",
                       {
                           {highPassFrequency, "Coupe-bas", "Hz", highPassOff, 1000.0, highPassOff},
                           {highFrequency, "Aigus, fréquence", "Hz", 1000.0, 20000.0, 8000.0},
                           {highGain, "Aigus, gain", "dB", -20.0, 20.0, 0.0},
                           {highQ, "Aigus, Q", "Q", 0.1, 4.0, 0.7},
                           {lowFrequency, "Graves, fréquence", "Hz", 20.0, 500.0, 100.0},
                           {lowGain, "Graves, gain", "dB", -20.0, 20.0, 0.0},
                           {lowQ, "Graves, Q", "Q", 0.1, 4.0, 0.7},
                           {mid1Frequency, "Médium 1, fréquence", "Hz", 20.0, 20000.0, 400.0},
                           {mid1Gain, "Médium 1, gain", "dB", -20.0, 20.0, 0.0},
                           {mid1Q, "Médium 1, Q", "Q", 0.1, 4.0, 1.0},
                           {mid2Frequency, "Médium 2, fréquence", "Hz", 20.0, 20000.0, 3000.0},
                           {mid2Gain, "Médium 2, gain", "dB", -20.0, 20.0, 0.0},
                           {mid2Q, "Médium 2, Q", "Q", 0.1, 4.0, 1.0},
                       }});

    // The compressor's bounds are Tracktion's too, said in the units a person
    // reads: its threshold is a gain from 0.01 to 1 (-40 to 0 dB), its ratio
    // the inverse of what is written here, down to 0.05 (20:1).
    effects.push_back(InternalEffect{compressor,
                                     "Compresseur",
                                     {
                                         {threshold, "Seuil", "dB", -40.0, 0.0, -12.0},
                                         {ratio, "Ratio", ":1", 1.0, 20.0, 2.0},
                                         {attack, "Attaque", "ms", 0.3, 200.0, 10.0},
                                         {release, "Relâchement", "ms", 10.0, 300.0, 100.0},
                                         {makeup, "Gain de sortie", "dB", -10.0, 24.0, 0.0},
                                     }});
    return effects;
}

struct Biquad
{
    double b0, b1, b2, a0, a1, a2;

    [[nodiscard]] double gainDbAt(double frequency, double sampleRate) const
    {
        const auto omega = 2.0 * std::numbers::pi * frequency / sampleRate;
        const std::complex<double> z1 = std::polar(1.0, -omega);
        const auto z2 = z1 * z1;
        const auto numerator = b0 + b1 * z1 + b2 * z2;
        const auto denominator = a0 + a1 * z1 + a2 * z2;
        return 20.0 * std::log10(std::abs(numerator / denominator));
    }
};

// The formulas of juce::IIRCoefficients, which is what Tracktion's equaliser
// and high-pass call: the curve drawn is the curve heard.
Biquad shelf(bool low, double sampleRate, double frequency, double q, double gainDb)
{
    const auto a = std::sqrt(std::pow(10.0, gainDb / 20.0));
    const auto minus = a - 1.0;
    const auto plus = a + 1.0;
    const auto omega = 2.0 * std::numbers::pi * std::max(frequency, 2.0) / sampleRate;
    const auto coso = std::cos(omega);
    const auto beta = std::sin(omega) * std::sqrt(a) / q;
    const auto minusCoso = minus * coso;
    if (low)
        return {a * (plus - minusCoso + beta),
                a * 2.0 * (minus - plus * coso),
                a * (plus - minusCoso - beta),
                plus + minusCoso + beta,
                -2.0 * (minus + plus * coso),
                plus + minusCoso - beta};
    return {a * (plus + minusCoso + beta),
            a * -2.0 * (minus + plus * coso),
            a * (plus + minusCoso - beta),
            plus - minusCoso + beta,
            2.0 * (minus - plus * coso),
            plus - minusCoso - beta};
}

Biquad bell(double sampleRate, double frequency, double q, double gainDb)
{
    const auto a = std::sqrt(std::pow(10.0, gainDb / 20.0));
    const auto omega = 2.0 * std::numbers::pi * std::max(frequency, 2.0) / sampleRate;
    const auto alpha = 0.5 * std::sin(omega) / q;
    const auto c2 = -2.0 * std::cos(omega);
    return {1.0 + alpha * a, c2, 1.0 - alpha * a, 1.0 + alpha / a, c2, 1.0 - alpha / a};
}

Biquad highPass(double sampleRate, double frequency)
{
    const auto q = 1.0 / std::sqrt(2.0);
    const auto n = std::tan(std::numbers::pi * frequency / sampleRate);
    const auto squared = n * n;
    const auto c1 = 1.0 / (1.0 + n / q + squared);
    return {c1, c1 * -2.0, c1, 1.0, c1 * 2.0 * (squared - 1.0), c1 * (1.0 - n / q + squared)};
}

} // namespace

const InternalParameter* InternalEffect::find(std::string_view parameterId) const noexcept
{
    for (const auto& parameter : parameters)
    {
        if (parameter.id == parameterId)
            return &parameter;
    }
    return nullptr;
}

const std::vector<InternalEffect>& internalEffects()
{
    static const auto effects = build();
    return effects;
}

const InternalEffect* findInternalEffect(std::string_view identifier) noexcept
{
    for (const auto& effect : internalEffects())
    {
        if (effect.identifier == identifier)
            return &effect;
    }
    return nullptr;
}

double internalValue(const PluginInstance& instance, std::string_view parameterId)
{
    if (const auto* held = instance.findParam(parameterId); held != nullptr)
        return held->value;
    const auto* effect = findInternalEffect(instance.ref.identifier);
    const auto* parameter = effect != nullptr ? effect->find(parameterId) : nullptr;
    return parameter != nullptr ? parameter->defaultValue : 0.0;
}

Result<void>
validateInternalParameter(const InternalEffect& effect, std::string_view parameterId, double value)
{
    const auto* parameter = effect.find(parameterId);
    if (parameter == nullptr)
        return fail(ErrorCode::invalidArgument,
                    std::string{effect.name} + " n'a pas de paramètre " + std::string{parameterId});
    if (!std::isfinite(value) || value < parameter->minimum || value > parameter->maximum)
        return fail(ErrorCode::invalidArgument,
                    std::string{effect.name} + ", " + std::string{parameter->label} + " : " +
                        std::to_string(value) + " hors de [" + std::to_string(parameter->minimum) + ", " +
                        std::to_string(parameter->maximum) + "] " + std::string{parameter->unit});
    return {};
}

double equaliserGainDb(const PluginInstance& instance, double frequency, double sampleRate)
{
    using namespace internal;
    if (instance.bypassed || instance.ref.format != PluginRef::internalFormat ||
        instance.ref.identifier != equaliser)
        return 0.0;

    const auto value = [&instance](std::string_view id) { return internalValue(instance, id); };
    auto total = 0.0;

    // Tracktion skips a band whose gain is zero, and the high-pass is off at
    // its floor: both are left out, exactly as the engine leaves them out.
    if (value(highPassFrequency) > highPassOff)
        total += highPass(sampleRate, value(highPassFrequency)).gainDbAt(frequency, sampleRate);
    if (value(lowGain) != 0.0)
        total += shelf(true, sampleRate, value(lowFrequency), value(lowQ), value(lowGain))
                     .gainDbAt(frequency, sampleRate);
    if (value(mid1Gain) != 0.0)
        total += bell(sampleRate, value(mid1Frequency), value(mid1Q), value(mid1Gain))
                     .gainDbAt(frequency, sampleRate);
    if (value(mid2Gain) != 0.0)
        total += bell(sampleRate, value(mid2Frequency), value(mid2Q), value(mid2Gain))
                     .gainDbAt(frequency, sampleRate);
    if (value(highGain) != 0.0)
        total += shelf(false, sampleRate, value(highFrequency), value(highQ), value(highGain))
                     .gainDbAt(frequency, sampleRate);
    return total;
}

} // namespace daw::domain
