#include "daw/domain/flux/Picture.h"

#include <cmath>
#include <cstddef>
#include <numbers>
#include <vector>

#include <doctest/doctest.h>

using namespace daw::domain;

// What a node of the flux shows (S24): the waveform drawn by columns, the
// level, the spectrum — on signals whose answer is known.

namespace
{

std::vector<float> sine(double hz, double amplitude, std::size_t count, double rate = 48000.0)
{
    std::vector<float> samples(count);
    for (std::size_t index = 0; index < count; ++index)
        samples[index] = static_cast<float>(
            amplitude * std::sin(2.0 * std::numbers::pi * hz * static_cast<double>(index) / rate));
    return samples;
}

// The band whose range holds `hz`.
std::size_t bandOf(double hz, std::size_t bands, double lowHz = 30.0, double highHz = 16000.0)
{
    const auto position = std::log(hz / lowHz) / std::log(highHz / lowHz) * static_cast<double>(bands);
    return static_cast<std::size_t>(position);
}

} // namespace

TEST_CASE("A waveform drawn in columns: each column the lowest and highest sample that falls in it")
{
    std::vector<float> ramp(100);
    for (std::size_t index = 0; index < ramp.size(); ++index)
        ramp[index] = static_cast<float>(index);

    const auto spans = flux::envelopeOf(ramp.data(), ramp.size(), 10);
    REQUIRE(spans.size() == 10);
    for (std::size_t column = 0; column < spans.size(); ++column)
    {
        CHECK(spans[column].low == doctest::Approx(10.0 * static_cast<double>(column)));
        CHECK(spans[column].high == doctest::Approx(10.0 * static_cast<double>(column) + 9.0));
    }

    // More columns than samples: each column shows the sample under it.
    const float three[] = {-0.5f, 0.25f, 1.0f};
    const auto wide = flux::envelopeOf(three, 3, 6);
    REQUIRE(wide.size() == 6);
    CHECK(wide.front().low == -0.5f);
    CHECK(wide.back().high == 1.0f);
    CHECK(wide[3].low == 0.25f);

    CHECK(flux::envelopeOf(ramp.data(), 0, 10).empty());
}

TEST_CASE("The level of a node is its peak in dBFS, silence at the floor")
{
    const auto half = sine(440.0, 0.5, 4800);
    CHECK(flux::peakDbOf(half.data(), half.size()) == doctest::Approx(-6.02).epsilon(0.01));
    const std::vector<float> silence(480, 0.0f);
    CHECK(flux::peakDbOf(silence.data(), silence.size()) == -100.0);
}

TEST_CASE("A sine's spectrum peaks in its band at its level, and nowhere else")
{
    constexpr std::size_t bands = 48;
    const auto full = sine(100.0, 1.0, 8192);
    const auto spectrum = flux::spectrumOf(full.data(), full.size(), 48000.0, bands);
    REQUIRE(spectrum.size() == bands);
    const auto at = bandOf(100.0, bands);
    CHECK(std::abs(spectrum[at]) < 1.5); // Hann: at most 1.42 dB lost between two bins
    CHECK(spectrum[bandOf(1000.0, bands)] < -60.0);
    CHECK(spectrum[bandOf(30.0 * 1.01, bands)] < -30.0);

    // Half the amplitude, 6 dB lower; in another band.
    const auto quiet = sine(2000.0, 0.5, 8192);
    const auto other = flux::spectrumOf(quiet.data(), quiet.size(), 48000.0, bands);
    CHECK(std::abs(other[bandOf(2000.0, bands)] + 6.02) < 1.5);
    CHECK(other[at] < -60.0);

    // The middle of a band is inside it.
    CHECK(bandOf(flux::bandHz(at, bands), bands) == at);
}
