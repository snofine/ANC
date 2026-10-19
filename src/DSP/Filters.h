#pragma once

#include <algorithm>
#include <cmath>
#include <numbers>

namespace anc::dsp {

class DCBlocker {
public:
    void reset() noexcept { x1_ = y1_ = 0.0f; }

    float process(float x) noexcept {
        const float y = x - x1_ + 0.995f * y1_;
        x1_ = x;
        y1_ = y;
        return y;
    }

private:
    float x1_{};
    float y1_{};
};

class Biquad {
public:
    void setHighPass(double sampleRate, double frequency, double q = 0.7071067811865476) noexcept {
        configure(sampleRate, frequency, q, true);
    }

    void setLowPass(double sampleRate, double frequency, double q = 0.7071067811865476) noexcept {
        configure(sampleRate, frequency, q, false);
    }

    void reset() noexcept { z1_ = z2_ = 0.0f; }

    float process(float x) noexcept {
        const float y = b0_ * x + z1_;
        z1_ = b1_ * x - a1_ * y + z2_;
        z2_ = b2_ * x - a2_ * y;
        return y;
    }

private:
    void configure(double sampleRate, double frequency, double q, bool highPass) noexcept {
        frequency = std::clamp(frequency, 1.0, sampleRate * 0.49);
        const double omega = 2.0 * std::numbers::pi * frequency / sampleRate;
        const double c = std::cos(omega);
        const double alpha = std::sin(omega) / (2.0 * q);
        const double a0 = 1.0 + alpha;
        if (highPass) {
            b0_ = static_cast<float>((1.0 + c) / (2.0 * a0));
            b1_ = static_cast<float>(-(1.0 + c) / a0);
            b2_ = b0_;
        } else {
            b0_ = static_cast<float>((1.0 - c) / (2.0 * a0));
            b1_ = static_cast<float>((1.0 - c) / a0);
            b2_ = b0_;
        }
        a1_ = static_cast<float>(-2.0 * c / a0);
        a2_ = static_cast<float>((1.0 - alpha) / a0);
    }

    float b0_{};
    float b1_{};
    float b2_{};
    float a1_{};
    float a2_{};
    float z1_{};
    float z2_{};
};

} // namespace anc::dsp
