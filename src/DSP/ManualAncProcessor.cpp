#include "DSP/ManualAncProcessor.h"

#include <algorithm>
#include <cmath>

namespace anc::dsp {

namespace {
float dbToLinear(float db) noexcept { return std::pow(10.0f, db / 20.0f); }

float phaseDelaySamples(float phaseDegrees, float toneHz, double sampleRate) noexcept {
    if (toneHz <= 0.0f) return 0.0f;
    phaseDegrees = std::fmod(phaseDegrees, 360.0f);
    if (phaseDegrees < 0.0f) phaseDegrees += 360.0f;
    return phaseDegrees / 360.0f * static_cast<float>(sampleRate) / toneHz;
}
} // namespace

ManualAncProcessor::ManualAncProcessor(const ManualAncSettings& settings)
    : settings_(settings), leftDelay_(static_cast<std::size_t>(settings.sampleRate * 0.1)),
      rightDelay_(static_cast<std::size_t>(settings.sampleRate * 0.1)),
      leftLimiter_(dbToLinear(settings.peakLimitDbfs)), rightLimiter_(dbToLinear(settings.peakLimitDbfs)) {
    highPass_.setHighPass(settings.sampleRate, settings.highPassHz);
    lowPass_.setLowPass(settings.sampleRate, settings.lowPassHz);
    const float samplesPerMs = static_cast<float>(settings.sampleRate / 1000.0);
    leftDelay_.setDelay(settings.delayMsLeft * samplesPerMs +
                        phaseDelaySamples(settings.phaseDegreesLeft, settings.targetToneHz, settings.sampleRate));
    rightDelay_.setDelay(settings.delayMsRight * samplesPerMs +
                         phaseDelaySamples(settings.phaseDegreesRight, settings.targetToneHz, settings.sampleRate));
    gain_.configure(settings.sampleRate, 1.0, 0.1);
    gain_.force(0.0f);
    const float initialStrength = std::clamp(settings.strength, 0.0f, settings.maxStrength);
    targetStrength_.store(initialStrength, std::memory_order_relaxed);
    gain_.setTarget(initialStrength);
    protectionGain_.configure(settings.sampleRate, 0.20, 0.005);
    protectionGain_.force(1.0f);
    speechAlpha_ = static_cast<float>(1.0 - std::exp(-2.0 * 3.141592653589793 * 400.0 / settings_.sampleRate));
}

void ManualAncProcessor::reset() noexcept {
    dc_.reset();
    highPass_.reset();
    lowPass_.reset();
    leftDelay_.reset();
    rightDelay_.reset();
    gain_.force(0.0f);
    currentStrength_.store(0.0f, std::memory_order_relaxed);
    speechLowPass_ = speechEnvelope_ = previousRaw_ = 0.0f;
    fastReferenceEnergy_ = slowReferenceEnergy_ = 0.0f;
    processedSamples_ = runawayHold_ = 0;
    voiceHold_ = impulseHold_ = 0;
    protectionGain_.force(1.0f);
    voiceDetected_.store(false, std::memory_order_relaxed);
    impulseDetected_.store(false, std::memory_order_relaxed);
}

StereoSample ManualAncProcessor::process(float microphoneSample) noexcept {
    // A deliberately conservative activity detector. It never adapts coefficients (MVP is static),
    // but exposes protection state and ducks abrupt breath/plosive events.
    speechLowPass_ += speechAlpha_ * (microphoneSample - speechLowPass_);
    const float highResidual = std::abs(microphoneSample - speechLowPass_);
    speechEnvelope_ += 0.0025f * (highResidual - speechEnvelope_);
    if (speechEnvelope_ > 0.025f) voiceHold_ = static_cast<std::uint32_t>(settings_.sampleRate * 0.20);
    if (std::abs(microphoneSample - previousRaw_) > 0.35f || std::abs(microphoneSample) > 0.90f)
        impulseHold_ = static_cast<std::uint32_t>(settings_.sampleRate * 0.15);
    previousRaw_ = microphoneSample;
    if (voiceHold_ > 0) --voiceHold_;
    if (impulseHold_ > 0) --impulseHold_;
    voiceDetected_.store(voiceHold_ > 0, std::memory_order_relaxed);
    impulseDetected_.store(impulseHold_ > 0, std::memory_order_relaxed);

    float reference = lowPass_.process(highPass_.process(dc_.process(microphoneSample)));
    const float referencePower = reference * reference;
    fastReferenceEnergy_ += 0.005f * (referencePower - fastReferenceEnergy_);
    slowReferenceEnergy_ += 0.00002f * (referencePower - slowReferenceEnergy_);
    ++processedSamples_;
    const bool warmedUp = processedSamples_ > static_cast<std::uint32_t>(settings_.sampleRate);
    if (warmedUp && fastReferenceEnergy_ > 0.0025f &&
        fastReferenceEnergy_ > std::max(0.000001f, slowReferenceEnergy_) * 16.0f) {
        ++runawayHold_;
        if (runawayHold_ > static_cast<std::uint32_t>(settings_.sampleRate * 0.01)) emergencyStop();
    } else {
        runawayHold_ = 0;
    }
    const bool stopped = emergency_.load(std::memory_order_relaxed);
    gain_.setTarget(stopped ? 0.0f : targetStrength_.load(std::memory_order_relaxed));
    float effectiveGain = gain_.next();
    currentStrength_.store(effectiveGain, std::memory_order_relaxed);
    protectionGain_.setTarget(impulseHold_ > 0 ? 0.1f : 1.0f);
    effectiveGain *= protectionGain_.next();

    StereoSample result;
    result.left = leftLimiter_.process(-leftDelay_.process(reference) * effectiveGain * settings_.gainLeft);
    result.right = rightLimiter_.process(-rightDelay_.process(reference) * effectiveGain * settings_.gainRight);
    return result;
}

void ManualAncProcessor::setStrength(float strength) noexcept {
    targetStrength_.store(std::clamp(strength, 0.0f, settings_.maxStrength), std::memory_order_relaxed);
}

void ManualAncProcessor::emergencyStop() noexcept {
    emergency_.store(true, std::memory_order_relaxed);
}

void ManualAncProcessor::clearEmergency() noexcept {
    emergency_.store(false, std::memory_order_relaxed);
}

} // namespace anc::dsp
