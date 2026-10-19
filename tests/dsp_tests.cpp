#include "DSP/Filters.h"
#include "DSP/Limiter.h"
#include "DSP/ManualAncProcessor.h"
#include "Analysis/SpectrumAnalyzer.h"

#include <cassert>
#include <array>
#include <cmath>
#include <iostream>
#include <numbers>

namespace {

float filteredRms(float frequency) {
    constexpr double sampleRate = 48000.0;
    anc::dsp::Biquad highPass;
    anc::dsp::Biquad lowPass;
    highPass.setHighPass(sampleRate, 40.0);
    lowPass.setLowPass(sampleRate, 150.0);
    double sum = 0.0;
    int count = 0;
    for (int i = 0; i < 96000; ++i) {
        const float x = std::sin(static_cast<float>(2.0 * std::numbers::pi * frequency * i / sampleRate));
        const float y = lowPass.process(highPass.process(x));
        if (i >= 48000) { sum += y * y; ++count; }
    }
    return static_cast<float>(std::sqrt(sum / count));
}

} // namespace

int main() {
    anc::dsp::DCBlocker dc;
    float value = 0.0f;
    for (int i = 0; i < 10000; ++i) value = dc.process(1.0f);
    assert(std::abs(value) < 1.0e-4f);

    const float pass = filteredRms(100.0f);
    assert(pass > filteredRms(10.0f) * 5.0f);
    assert(pass > filteredRms(1000.0f) * 5.0f);

    anc::dsp::PeakLimiter limiter;
    assert(std::abs(limiter.process(100.0f)) <= 0.25119f);

    std::array<float, 2048> spectrumInput{};
    for (std::size_t i = 0; i < spectrumInput.size(); ++i)
        spectrumInput[i] = std::sin(static_cast<float>(2.0 * std::numbers::pi * 100.0 * i / 48000.0));
    anc::analysis::SpectrumAnalyzer<2048> analyzer;
    const float peakHz = analyzer.dominantFrequency(spectrumInput, 48000.0);
    assert(std::abs(peakHz - 100.0f) < 24.0f);

    anc::dsp::ManualAncSettings settings;
    settings.strength = 0.1f;
    anc::dsp::ManualAncProcessor processor(settings);
    for (int i = 0; i < 48000; ++i) processor.process(0.01f);
    assert(processor.currentStrength() > 0.09f);
    processor.emergencyStop();
    for (int i = 0; i < 6000; ++i) processor.process(0.0f);
    assert(processor.currentStrength() == 0.0f);
    assert(processor.emergencyLatched());

    std::cout << "DSP tests passed\n";
}
