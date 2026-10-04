#pragma once

#include <complex>
#include <cstddef>
#include <numbers>
#include <utility>
#include <vector>

namespace daw::domain::dsp
{

// An iterative radix-2 FFT of 2^order complex points, in place. Shared by the
// mix's measure (S20) and the reading of a reference (S22); private to the
// domain.
class Fft
{
public:
    explicit Fft(std::size_t order)
        : order_{order}
        , size_{std::size_t{1} << order}
        , twiddles_(size_ / 2)
        , reversed_(size_)
    {
        for (std::size_t index = 0; index < size_ / 2; ++index)
            twiddles_[index] = std::polar(1.0, -2.0 * std::numbers::pi * static_cast<double>(index) / size_);
        for (std::size_t index = 0; index < size_; ++index)
        {
            std::size_t value = 0;
            for (std::size_t bit = 0; bit < order_; ++bit)
                value |= ((index >> bit) & 1U) << (order_ - 1 - bit);
            reversed_[index] = value;
        }
    }

    [[nodiscard]] std::size_t size() const noexcept { return size_; }

    void operator()(std::vector<std::complex<double>>& data) const
    {
        for (std::size_t index = 0; index < size_; ++index)
        {
            if (index < reversed_[index])
                std::swap(data[index], data[reversed_[index]]);
        }
        for (std::size_t span = 2; span <= size_; span *= 2)
        {
            const auto half = span / 2;
            const auto stride = size_ / span;
            for (std::size_t start = 0; start < size_; start += span)
            {
                // By hand, on the real and imaginary parts: the complex
                // product of the standard library checks for infinities
                // and costs a third of the whole analysis under MSVC.
                for (std::size_t offset = 0; offset < half; ++offset)
                {
                    auto& low = data[start + offset];
                    auto& high = data[start + offset + half];
                    const auto& twiddle = twiddles_[offset * stride];
                    const auto re = high.real() * twiddle.real() - high.imag() * twiddle.imag();
                    const auto im = high.real() * twiddle.imag() + high.imag() * twiddle.real();
                    high = {low.real() - re, low.imag() - im};
                    low = {low.real() + re, low.imag() + im};
                }
            }
        }
    }

private:
    std::size_t order_;
    std::size_t size_;
    std::vector<std::complex<double>> twiddles_;
    std::vector<std::size_t> reversed_;
};

} // namespace daw::domain::dsp
