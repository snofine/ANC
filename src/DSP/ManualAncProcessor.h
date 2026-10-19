#pragma once

#include "DSP/DelayLine.h"
#include "DSP/Filters.h"
#include "DSP/Limiter.h"

#include <atomic>
#include <cstdint>

namespace anc::dsp {

struct ManualAncSettings {
    double sampleRate{48000.0};
    float highPassHz{40.0f};
    float lowPassHz{150.0f};
    float targetToneHz{100.0f};
    float phaseDegreesLeft{0.0f};
    float phaseDegreesRight{0.0f};
    float delayMsLeft{0.0f};
    float delayMsRight{0.0f};
    float gainLeft{1.0f};
    float gainRight{1.0f};
    float strength{0.10f};
    float maxStrength{0.30f};
    float peakLimitDbfs{-12.0f};
};

struct StereoSample { float left{}; float right{}; };

class ManualAncProcessor {
public:
    explicit ManualAncProcessor(const ManualAncSettings& settings);

    void reset() noexcept;
    StereoSample process(float microphoneSample) noexcept;
    void setStrength(float strength) noexcept;
    void emergencyStop() noexcept;
    void clearEmergency() noexcept;
    bool emergencyLatched() const noexcept { return emergency_.load(std::memory_order_relaxed); }
    bool voiceDetected() const noexcept { return voiceDetected_.load(std::memory_order_relaxed); }
    bool impulseDetected() const noexcept { return impulseDetected_.load(std::memory_order_relaxed); }
    float currentStrength() const noexcept { return currentStrength_.load(std::memory_order_relaxed); }

private:
    ManualAncSettings settings_;
    DCBlocker dc_;
    Biquad highPass_;
    Biquad lowPass_;
    DelayLine leftDelay_;
    DelayLine rightDelay_;
    SmoothGain gain_;
    SmoothGain protectionGain_;
    PeakLimiter leftLimiter_;
    PeakLimiter rightLimiter_;
    std::atomic<bool> emergency_{false};
    std::atomic<bool> voiceDetected_{false};
    std::atomic<bool> impulseDetected_{false};
    std::atomic<float> targetStrength_{0.10f};
    std::atomic<float> currentStrength_{0.0f};
    float speechLowPass_{};
    float speechEnvelope_{};
    float previousRaw_{};
    float speechAlpha_{};
    float fastReferenceEnergy_{};
    float slowReferenceEnergy_{};
    std::uint32_t processedSamples_{};
    std::uint32_t runawayHold_{};
    std::uint32_t voiceHold_{};
    std::uint32_t impulseHold_{};
};

} // namespace anc::dsp
