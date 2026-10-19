#pragma once

#include "Audio/SpscRingBuffer.h"
#include "DSP/ManualAncProcessor.h"

#include <Windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>

namespace anc::audio {

struct EngineStats {
    std::uint64_t captureOverruns{};
    std::uint64_t renderUnderruns{};
    float microphoneRms{};
    float ancRms{};
    float dominantFrequencyHz{};
    double inputLatencyMs{};
    double outputLatencyMs{};
    std::uint32_t inputPeriodFrames{};
    std::uint32_t outputPeriodFrames{};
    bool microphoneOnline{};
    bool running{};
};

class WasapiEngine {
public:
    WasapiEngine(std::wstring inputId, std::wstring outputId, dsp::ManualAncSettings settings,
                 std::uint32_t requestedPeriodFrames);
    ~WasapiEngine();

    WasapiEngine(const WasapiEngine&) = delete;
    WasapiEngine& operator=(const WasapiEngine&) = delete;

    void start();
    void stop() noexcept;
    EngineStats stats() const noexcept;
    dsp::ManualAncProcessor& processor() noexcept { return *processor_; }
    double sampleRate() const noexcept { return sampleRate_; }

private:
    struct Stream;
    void initialize();
    void captureLoop() noexcept;
    void renderLoop() noexcept;
    void analysisLoop() noexcept;

    std::wstring inputId_;
    std::wstring outputId_;
    dsp::ManualAncSettings settings_;
    std::uint32_t requestedPeriodFrames_{};
    double sampleRate_{};
    std::unique_ptr<Stream> capture_;
    std::unique_ptr<Stream> render_;
    std::unique_ptr<dsp::ManualAncProcessor> processor_;
    SpscRingBuffer<float, 65536> microphoneRing_;
    SpscRingBuffer<float, 65536> analysisRing_;
    std::thread captureThread_;
    std::thread renderThread_;
    std::thread analysisThread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> microphoneOnline_{false};
    std::atomic<std::uint64_t> captureOverruns_{};
    std::atomic<std::uint64_t> renderUnderruns_{};
    std::atomic<float> microphoneRms_{};
    std::atomic<float> ancRms_{};
    std::atomic<float> dominantFrequencyHz_{};
};

} // namespace anc::audio
