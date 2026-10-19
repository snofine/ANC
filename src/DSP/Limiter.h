#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace anc::dsp {

class SmoothGain {
public:
    void configure(double sampleRate, double attackSeconds, double releaseSeconds) noexcept {
        attackSamples_ = static_cast<std::uint32_t>(std::max(1.0, sampleRate * attackSeconds));
        releaseSamples_ = static_cast<std::uint32_t>(std::max(1.0, sampleRate * releaseSeconds));
    }

    void setTarget(float target) noexcept {
        target = std::clamp(target, 0.0f, 1.0f);
        if (target == target_) return;
        target_ = target;
        remaining_ = target_ > current_ ? attackSamples_ : releaseSamples_;
        step_ = (target_ - current_) / static_cast<float>(remaining_);
    }
    void force(float value) noexcept {
        current_ = target_ = std::clamp(value, 0.0f, 1.0f);
        remaining_ = 0;
        step_ = 0.0f;
    }
    float current() const noexcept { return current_; }

    float next() noexcept {
        if (remaining_ > 0) {
            current_ += step_;
            if (--remaining_ == 0) current_ = target_;
        }
        return current_;
    }

private:
    float current_{};
    float target_{};
    float step_{};
    std::uint32_t attackSamples_{1};
    std::uint32_t releaseSamples_{1};
    std::uint32_t remaining_{};
};

class PeakLimiter {
public:
    explicit PeakLimiter(float ceiling = 0.2511886f) : ceiling_(ceiling) {}

    float process(float x) noexcept {
        // Smooth saturation precedes the absolute hard ceiling (-12 dBFS by default).
        const float normalized = x / ceiling_;
        const float soft = ceiling_ * std::tanh(normalized);
        return std::clamp(soft, -ceiling_, ceiling_);
    }

private:
    float ceiling_;
};

} // namespace anc::dsp
