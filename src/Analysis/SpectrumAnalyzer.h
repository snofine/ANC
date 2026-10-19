#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <numbers>

namespace anc::analysis {

template <std::size_t Size>
class SpectrumAnalyzer {
    static_assert((Size & (Size - 1)) == 0, "FFT size must be a power of two");
public:
    float dominantFrequency(const std::array<float, Size>& input, double sampleRate,
                            float minimumHz = 20.0f, float maximumHz = 1000.0f) noexcept {
        for (std::size_t i = 0; i < Size; ++i) {
            const float window = 0.5f - 0.5f * std::cos(static_cast<float>(2.0 * std::numbers::pi * i / (Size - 1)));
            bins_[i] = {input[i] * window, 0.0f};
        }
        fft();
        const auto first = std::max<std::size_t>(1, static_cast<std::size_t>(minimumHz * Size / sampleRate));
        const auto last = std::min<std::size_t>(Size / 2, static_cast<std::size_t>(maximumHz * Size / sampleRate) + 1);
        std::size_t peak = first;
        float peakPower = 0.0f;
        for (std::size_t i = first; i < last; ++i) {
            const float power = std::norm(bins_[i]);
            if (power > peakPower) { peakPower = power; peak = i; }
        }
        return static_cast<float>(peak * sampleRate / Size);
    }

private:
    void fft() noexcept {
        for (std::size_t i = 1, j = 0; i < Size; ++i) {
            std::size_t bit = Size >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) std::swap(bins_[i], bins_[j]);
        }
        for (std::size_t length = 2; length <= Size; length <<= 1) {
            const float angle = static_cast<float>(-2.0 * std::numbers::pi / length);
            const std::complex<float> step(std::cos(angle), std::sin(angle));
            for (std::size_t offset = 0; offset < Size; offset += length) {
                std::complex<float> twiddle(1.0f, 0.0f);
                for (std::size_t i = 0; i < length / 2; ++i) {
                    const auto even = bins_[offset + i];
                    const auto odd = bins_[offset + i + length / 2] * twiddle;
                    bins_[offset + i] = even + odd;
                    bins_[offset + i + length / 2] = even - odd;
                    twiddle *= step;
                }
            }
        }
    }

    std::array<std::complex<float>, Size> bins_{};
};

} // namespace anc::analysis

