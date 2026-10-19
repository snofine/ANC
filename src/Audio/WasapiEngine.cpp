#include "Audio/WasapiEngine.h"
#include "Analysis/SpectrumAnalyzer.h"

#include <avrt.h>
#include <ksmedia.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <utility>

using Microsoft::WRL::ComPtr;

namespace anc::audio {
namespace {

void check(HRESULT hr, const char* operation) {
    if (FAILED(hr)) throw std::runtime_error(std::string(operation) + " failed (HRESULT " + std::to_string(hr) + ")");
}

ComPtr<IMMDevice> openDevice(const std::wstring& id) {
    ComPtr<IMMDeviceEnumerator> enumerator;
    check(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                           IID_PPV_ARGS(&enumerator)), "Create device enumerator");
    ComPtr<IMMDevice> device;
    check(enumerator->GetDevice(id.c_str(), &device), "Open audio endpoint");
    return device;
}

bool isFloatFormat(const WAVEFORMATEX* format) noexcept {
    if (format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) return true;
    if (format->wFormatTag != WAVE_FORMAT_EXTENSIBLE) return false;
    const auto* extended = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format);
    return IsEqualGUID(extended->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
}

bool isPcmFormat(const WAVEFORMATEX* format) noexcept {
    if (format->wFormatTag == WAVE_FORMAT_PCM) return true;
    if (format->wFormatTag != WAVE_FORMAT_EXTENSIBLE) return false;
    const auto* extended = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format);
    return IsEqualGUID(extended->SubFormat, KSDATAFORMAT_SUBTYPE_PCM);
}

float readChannel(const BYTE* frame, const WAVEFORMATEX* format, WORD channel) noexcept {
    const auto bytes = format->wBitsPerSample / 8;
    const BYTE* p = frame + static_cast<std::size_t>(channel) * bytes;
    if (isFloatFormat(format) && format->wBitsPerSample == 32) {
        float value;
        std::memcpy(&value, p, sizeof(value));
        return std::isfinite(value) ? value : 0.0f;
    }
    if (!isPcmFormat(format)) return 0.0f;
    if (format->wBitsPerSample == 16) {
        std::int16_t value;
        std::memcpy(&value, p, sizeof(value));
        return static_cast<float>(value) / 32768.0f;
    }
    if (format->wBitsPerSample == 24) {
        std::int32_t value = static_cast<std::int32_t>(p[0]) |
                             (static_cast<std::int32_t>(p[1]) << 8) |
                             (static_cast<std::int32_t>(p[2]) << 16);
        if (value & 0x00800000) value |= ~0x00FFFFFF;
        return static_cast<float>(value) / 8388608.0f;
    }
    if (format->wBitsPerSample == 32) {
        std::int32_t value;
        std::memcpy(&value, p, sizeof(value));
        return static_cast<float>(value) / 2147483648.0f;
    }
    return 0.0f;
}

void writeChannel(BYTE* frame, const WAVEFORMATEX* format, WORD channel, float sample) noexcept {
    sample = std::clamp(sample, -1.0f, 1.0f);
    const auto bytes = format->wBitsPerSample / 8;
    BYTE* p = frame + static_cast<std::size_t>(channel) * bytes;
    if (isFloatFormat(format) && format->wBitsPerSample == 32) {
        std::memcpy(p, &sample, sizeof(sample));
    } else if (isPcmFormat(format) && format->wBitsPerSample == 16) {
        const auto value = static_cast<std::int16_t>(std::lrint(sample * 32767.0f));
        std::memcpy(p, &value, sizeof(value));
    } else if (isPcmFormat(format) && format->wBitsPerSample == 24) {
        const auto value = static_cast<std::int32_t>(std::lrint(sample * 8388607.0f));
        p[0] = static_cast<BYTE>(value & 0xFF);
        p[1] = static_cast<BYTE>((value >> 8) & 0xFF);
        p[2] = static_cast<BYTE>((value >> 16) & 0xFF);
    } else if (isPcmFormat(format) && format->wBitsPerSample == 32) {
        const auto value = static_cast<std::int32_t>(std::llround(sample * 2147483647.0));
        std::memcpy(p, &value, sizeof(value));
    }
}

std::uint32_t choosePeriod(std::uint32_t requested, UINT32 minimum, UINT32 maximum, UINT32 fundamental) noexcept {
    requested = std::clamp(requested, minimum, maximum);
    if (fundamental == 0 || requested <= minimum) return requested;
    const auto steps = (requested - minimum + fundamental / 2) / fundamental;
    return std::clamp(minimum + steps * fundamental, minimum, maximum);
}

} // namespace

struct WasapiEngine::Stream {
    ComPtr<IMMDevice> device;
    ComPtr<IAudioClient> client;
    ComPtr<IAudioCaptureClient> captureClient;
    ComPtr<IAudioRenderClient> renderClient;
    WAVEFORMATEX* format{};
    HANDLE event{};
    UINT32 bufferFrames{};
    UINT32 periodFrames{};
    REFERENCE_TIME latency{};

    ~Stream() {
        if (event) CloseHandle(event);
        if (format) CoTaskMemFree(format);
    }
};

WasapiEngine::WasapiEngine(std::wstring inputId, std::wstring outputId,
                           dsp::ManualAncSettings settings, std::uint32_t requestedPeriodFrames)
    : inputId_(std::move(inputId)), outputId_(std::move(outputId)), settings_(settings),
      requestedPeriodFrames_(requestedPeriodFrames) {
    initialize();
}

WasapiEngine::~WasapiEngine() { stop(); }

void WasapiEngine::initialize() {
    capture_ = std::make_unique<Stream>();
    render_ = std::make_unique<Stream>();
    capture_->device = openDevice(inputId_);
    render_->device = openDevice(outputId_);

    check(capture_->device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                     reinterpret_cast<void**>(capture_->client.GetAddressOf())), "Activate capture client");
    check(render_->device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                    reinterpret_cast<void**>(render_->client.GetAddressOf())), "Activate render client");
    check(capture_->client->GetMixFormat(&capture_->format), "Read capture mix format");
    check(render_->client->GetMixFormat(&render_->format), "Read render mix format");

    if (capture_->format->nSamplesPerSec != render_->format->nSamplesPerSec)
        throw std::runtime_error("Input/output sample-rate mismatch. Set both Windows devices to the same rate.");
    if ((!isFloatFormat(capture_->format) && !isPcmFormat(capture_->format)) ||
        (!isFloatFormat(render_->format) && !isPcmFormat(render_->format)))
        throw std::runtime_error("The device mix format is neither PCM nor IEEE float.");
    if (render_->format->nChannels < 2)
        throw std::runtime_error("The selected output is not stereo.");

    sampleRate_ = capture_->format->nSamplesPerSec;
    settings_.sampleRate = sampleRate_;
    const DWORD flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK;

    auto initializeStream = [&](Stream& stream) {
        ComPtr<IAudioClient3> client3;
        if (SUCCEEDED(stream.client.As(&client3))) {
            UINT32 defaultPeriod = 0, fundamental = 0, minimum = 0, maximum = 0;
            check(client3->GetSharedModeEnginePeriod(stream.format, &defaultPeriod, &fundamental,
                                                     &minimum, &maximum), "Query low-latency periods");
            stream.periodFrames = choosePeriod(requestedPeriodFrames_, minimum, maximum, fundamental);
            check(client3->InitializeSharedAudioStream(flags, stream.periodFrames, stream.format, nullptr),
                  "Initialize low-latency shared stream");
        } else {
            check(stream.client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, 0, 0, stream.format, nullptr),
                  "Initialize shared stream");
        }
        stream.event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!stream.event) throw std::runtime_error("Create audio event failed");
        check(stream.client->SetEventHandle(stream.event), "Attach audio event");
        check(stream.client->GetBufferSize(&stream.bufferFrames), "Read audio buffer size");
        check(stream.client->GetStreamLatency(&stream.latency), "Read stream latency");
        if (stream.periodFrames == 0) stream.periodFrames = stream.bufferFrames;
    };

    initializeStream(*capture_);
    initializeStream(*render_);
    check(capture_->client->GetService(IID_PPV_ARGS(&capture_->captureClient)), "Open capture service");
    check(render_->client->GetService(IID_PPV_ARGS(&render_->renderClient)), "Open render service");
    processor_ = std::make_unique<dsp::ManualAncProcessor>(settings_);
}

void WasapiEngine::start() {
    if (running_.exchange(true)) return;
    processor_->reset();
    microphoneOnline_.store(true, std::memory_order_relaxed);
    BYTE* initialData = nullptr;
    check(render_->renderClient->GetBuffer(render_->bufferFrames, &initialData), "Prime render buffer");
    std::memset(initialData, 0, static_cast<std::size_t>(render_->bufferFrames) * render_->format->nBlockAlign);
    check(render_->renderClient->ReleaseBuffer(render_->bufferFrames, AUDCLNT_BUFFERFLAGS_SILENT),
          "Release primed render buffer");
    check(capture_->client->Start(), "Start capture");
    try {
        check(render_->client->Start(), "Start render");
    } catch (...) {
        capture_->client->Stop();
        running_.store(false);
        throw;
    }
    captureThread_ = std::thread(&WasapiEngine::captureLoop, this);
    renderThread_ = std::thread(&WasapiEngine::renderLoop, this);
    analysisThread_ = std::thread(&WasapiEngine::analysisLoop, this);
}

void WasapiEngine::stop() noexcept {
    running_.store(false, std::memory_order_relaxed);
    if (capture_ && capture_->event) SetEvent(capture_->event);
    if (render_ && render_->event) SetEvent(render_->event);
    if (captureThread_.joinable()) captureThread_.join();
    if (renderThread_.joinable()) renderThread_.join();
    if (analysisThread_.joinable()) analysisThread_.join();
    if (capture_ && capture_->client) capture_->client->Stop();
    if (render_ && render_->client) render_->client->Stop();
    microphoneOnline_.store(false, std::memory_order_relaxed);
}

void WasapiEngine::captureLoop() noexcept {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    DWORD taskIndex = 0;
    HANDLE mmcss = AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex);
    float rms = 0.0f;
    std::uint64_t silentFrames = 0;
    while (running_.load(std::memory_order_relaxed)) {
        if (WaitForSingleObject(capture_->event, 2000) != WAIT_OBJECT_0) {
            microphoneOnline_.store(false, std::memory_order_relaxed);
            processor_->emergencyStop();
            break;
        }
        UINT32 packetFrames = 0;
        HRESULT hr = capture_->captureClient->GetNextPacketSize(&packetFrames);
        while (SUCCEEDED(hr) && packetFrames > 0) {
            BYTE* data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            hr = capture_->captureClient->GetBuffer(&data, &frames, &flags, nullptr, nullptr);
            if (FAILED(hr)) break;
            for (UINT32 frameIndex = 0; frameIndex < frames; ++frameIndex) {
                float mono = 0.0f;
                if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) == 0) {
                    const BYTE* frame = data + static_cast<std::size_t>(frameIndex) * capture_->format->nBlockAlign;
                    for (WORD channel = 0; channel < capture_->format->nChannels; ++channel)
                        mono += readChannel(frame, capture_->format, channel);
                    mono /= capture_->format->nChannels;
                    silentFrames = 0;
                } else {
                    ++silentFrames;
                }
                if (!microphoneRing_.push(mono)) captureOverruns_.fetch_add(1, std::memory_order_relaxed);
                analysisRing_.push(mono);
                rms += 0.001f * (mono * mono - rms);
            }
            capture_->captureClient->ReleaseBuffer(frames);
            if (silentFrames > static_cast<std::uint64_t>(sampleRate_)) {
                microphoneOnline_.store(false, std::memory_order_relaxed);
                processor_->emergencyStop();
                break;
            }
            hr = capture_->captureClient->GetNextPacketSize(&packetFrames);
        }
        if (FAILED(hr)) {
            microphoneOnline_.store(false, std::memory_order_relaxed);
            processor_->emergencyStop();
            break;
        }
        microphoneRms_.store(std::sqrt(std::max(0.0f, rms)), std::memory_order_relaxed);
    }
    if (mmcss) AvRevertMmThreadCharacteristics(mmcss);
    CoUninitialize();
}

void WasapiEngine::analysisLoop() noexcept {
    constexpr std::size_t fftSize = 2048;
    analysis::SpectrumAnalyzer<fftSize> analyzer;
    std::array<float, fftSize> samples{};
    std::size_t count = 0;
    while (running_.load(std::memory_order_relaxed)) {
        float sample = 0.0f;
        bool received = false;
        while (count < fftSize && analysisRing_.pop(sample)) {
            samples[count++] = sample;
            received = true;
        }
        if (count == fftSize) {
            dominantFrequencyHz_.store(analyzer.dominantFrequency(samples, sampleRate_), std::memory_order_relaxed);
            count = 0;
        } else if (!received) {
            Sleep(2);
        }
    }
}

void WasapiEngine::renderLoop() noexcept {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    DWORD taskIndex = 0;
    HANDLE mmcss = AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex);
    float rms = 0.0f;
    while (running_.load(std::memory_order_relaxed)) {
        if (WaitForSingleObject(render_->event, 2000) != WAIT_OBJECT_0) break;
        UINT32 padding = 0;
        HRESULT hr = render_->client->GetCurrentPadding(&padding);
        if (FAILED(hr)) break;
        const UINT32 frames = render_->bufferFrames - padding;
        if (frames == 0) continue;
        BYTE* data = nullptr;
        hr = render_->renderClient->GetBuffer(frames, &data);
        if (FAILED(hr)) break;
        std::memset(data, 0, static_cast<std::size_t>(frames) * render_->format->nBlockAlign);
        for (UINT32 frameIndex = 0; frameIndex < frames; ++frameIndex) {
            float mic = 0.0f;
            if (!microphoneRing_.pop(mic)) renderUnderruns_.fetch_add(1, std::memory_order_relaxed);
            const auto anc = processor_->process(mic);
            BYTE* frame = data + static_cast<std::size_t>(frameIndex) * render_->format->nBlockAlign;
            writeChannel(frame, render_->format, 0, anc.left);
            writeChannel(frame, render_->format, 1, anc.right);
            rms += 0.001f * (0.5f * (anc.left * anc.left + anc.right * anc.right) - rms);
        }
        hr = render_->renderClient->ReleaseBuffer(frames, 0);
        if (FAILED(hr)) break;
        ancRms_.store(std::sqrt(std::max(0.0f, rms)), std::memory_order_relaxed);
    }
    if (running_.load(std::memory_order_relaxed)) {
        processor_->emergencyStop();
        running_.store(false, std::memory_order_relaxed);
    }
    if (mmcss) AvRevertMmThreadCharacteristics(mmcss);
    CoUninitialize();
}

EngineStats WasapiEngine::stats() const noexcept {
    EngineStats result;
    result.captureOverruns = captureOverruns_.load(std::memory_order_relaxed);
    result.renderUnderruns = renderUnderruns_.load(std::memory_order_relaxed);
    result.microphoneRms = microphoneRms_.load(std::memory_order_relaxed);
    result.ancRms = ancRms_.load(std::memory_order_relaxed);
    result.dominantFrequencyHz = dominantFrequencyHz_.load(std::memory_order_relaxed);
    result.inputLatencyMs = capture_->latency / 10000.0;
    result.outputLatencyMs = render_->latency / 10000.0;
    result.inputPeriodFrames = capture_->periodFrames;
    result.outputPeriodFrames = render_->periodFrames;
    result.microphoneOnline = microphoneOnline_.load(std::memory_order_relaxed);
    result.running = running_.load(std::memory_order_relaxed);
    return result;
}

} // namespace anc::audio
