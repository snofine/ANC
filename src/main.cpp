#include "Audio/DeviceManager.h"
#include "Audio/WasapiEngine.h"

#include <Windows.h>
#include <fcntl.h>
#include <io.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <conio.h>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

std::atomic<bool> stopRequested{false};

BOOL WINAPI consoleHandler(DWORD type) {
    if (type == CTRL_C_EVENT || type == CTRL_CLOSE_EVENT || type == CTRL_BREAK_EVENT) {
        stopRequested.store(true, std::memory_order_relaxed);
        return TRUE;
    }
    return FALSE;
}

struct Options {
    std::optional<std::size_t> input;
    std::optional<std::size_t> output;
    std::uint32_t buffer{64};
    anc::dsp::ManualAncSettings dsp;
    bool listOnly{};
    bool start{};
    bool experimentalWideband{};
};

void usage() {
    std::wcout
        << L"ANC measurement MVP 0.1\n\n"
        << L"  anc_app --list\n"
        << L"  anc_app --input N --output N --start [options]\n\n"
        << L"Options:\n"
        << L"  --buffer 32|64|128       requested WASAPI period (default 64)\n"
        << L"  --frequency HZ           calibration tone (default 100)\n"
        << L"  --hpf HZ / --lpf HZ      ANC band (defaults 40 / 150)\n"
        << L"  --strength PERCENT       0..30, starts at 10\n"
        << L"  --phase-left DEG          extra phase as a causal delay\n"
        << L"  --phase-right DEG         extra phase as a causal delay\n"
        << L"  --delay-left-ms MS        extra acoustic alignment delay\n"
        << L"  --delay-right-ms MS       extra acoustic alignment delay\n"
        << L"  --left-gain VALUE         0..1\n"
        << L"  --right-gain VALUE        0..1\n\n"
        << L"  --experimental-wideband  20..500 Hz; permits strength up to 100%\n\n"
        << L"Phase 0 already includes polarity inversion (180 degree anti-noise).\n";
}

double numberAfter(int& index, int argc, wchar_t** argv) {
    if (++index >= argc) throw std::invalid_argument("Missing option value");
    return std::stod(argv[index]);
}

Options parseOptions(int argc, wchar_t** argv) {
    Options result;
    bool highPassWasSet = false;
    bool lowPassWasSet = false;
    for (int i = 1; i < argc; ++i) {
        const std::wstring arg = argv[i];
        if (arg == L"--help" || arg == L"-h") { usage(); std::exit(0); }
        else if (arg == L"--list") result.listOnly = true;
        else if (arg == L"--start") result.start = true;
        else if (arg == L"--experimental-wideband") result.experimentalWideband = true;
        else if (arg == L"--input") result.input = static_cast<std::size_t>(numberAfter(i, argc, argv));
        else if (arg == L"--output") result.output = static_cast<std::size_t>(numberAfter(i, argc, argv));
        else if (arg == L"--buffer") result.buffer = static_cast<std::uint32_t>(numberAfter(i, argc, argv));
        else if (arg == L"--frequency") result.dsp.targetToneHz = static_cast<float>(numberAfter(i, argc, argv));
        else if (arg == L"--hpf") { result.dsp.highPassHz = static_cast<float>(numberAfter(i, argc, argv)); highPassWasSet = true; }
        else if (arg == L"--lpf") { result.dsp.lowPassHz = static_cast<float>(numberAfter(i, argc, argv)); lowPassWasSet = true; }
        else if (arg == L"--strength") result.dsp.strength = static_cast<float>(numberAfter(i, argc, argv) / 100.0);
        else if (arg == L"--phase-left") result.dsp.phaseDegreesLeft = static_cast<float>(numberAfter(i, argc, argv));
        else if (arg == L"--phase-right") result.dsp.phaseDegreesRight = static_cast<float>(numberAfter(i, argc, argv));
        else if (arg == L"--delay-left-ms") result.dsp.delayMsLeft = static_cast<float>(numberAfter(i, argc, argv));
        else if (arg == L"--delay-right-ms") result.dsp.delayMsRight = static_cast<float>(numberAfter(i, argc, argv));
        else if (arg == L"--left-gain") result.dsp.gainLeft = static_cast<float>(numberAfter(i, argc, argv));
        else if (arg == L"--right-gain") result.dsp.gainRight = static_cast<float>(numberAfter(i, argc, argv));
        else throw std::invalid_argument("Unknown command line option");
    }
    if (result.experimentalWideband) {
        if (!highPassWasSet) result.dsp.highPassHz = 20.0f;
        if (!lowPassWasSet) result.dsp.lowPassHz = 500.0f;
        result.dsp.maxStrength = 1.0f;
    }
    if (result.buffer != 32 && result.buffer != 64 && result.buffer != 128)
        throw std::invalid_argument("Buffer must be 32, 64, or 128 frames");
    if (result.dsp.highPassHz < 5.0f || result.dsp.highPassHz > 50.0f ||
        result.dsp.lowPassHz < 80.0f || result.dsp.lowPassHz > 500.0f ||
        result.dsp.highPassHz >= result.dsp.lowPassHz)
        throw std::invalid_argument("Unsafe or invalid ANC band");
    if (result.dsp.targetToneHz < result.dsp.highPassHz || result.dsp.targetToneHz > result.dsp.lowPassHz)
        throw std::invalid_argument("Calibration frequency must be inside the ANC band");
    result.dsp.strength = std::clamp(result.dsp.strength, 0.0f, result.dsp.maxStrength);
    result.dsp.gainLeft = std::clamp(result.dsp.gainLeft, 0.0f, 1.0f);
    result.dsp.gainRight = std::clamp(result.dsp.gainRight, 0.0f, 1.0f);
    if (result.dsp.delayMsLeft < 0.0f || result.dsp.delayMsRight < 0.0f ||
        result.dsp.delayMsLeft > 50.0f || result.dsp.delayMsRight > 50.0f)
        throw std::invalid_argument("Delay must be between 0 and 50 ms");
    return result;
}

void printDevices(const wchar_t* heading, const std::vector<anc::audio::AudioDeviceInfo>& devices) {
    std::wcout << L"\n" << heading << L":\n";
    for (std::size_t i = 0; i < devices.size(); ++i)
        std::wcout << L"  [" << i << L"] " << devices[i].name
                   << (devices[i].isDefault ? L"  (default)" : L"") << L"\n";
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    _setmode(_fileno(stdout), _O_U16TEXT);
    _setmode(_fileno(stderr), _O_U16TEXT);
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(comResult)) {
        std::wcerr << L"Cannot initialize COM.\n";
        return 1;
    }

    int exitCode = 0;
    try {
        const auto options = parseOptions(argc, argv);
        const auto inputs = anc::audio::DeviceManager::captureDevices();
        const auto outputs = anc::audio::DeviceManager::renderDevices();
        printDevices(L"Capture devices", inputs);
        printDevices(L"Render devices", outputs);

        if (options.listOnly || !options.start) {
            if (!options.listOnly)
                std::wcout << L"\nSafe idle: add --input N --output N --start to enable audio.\n";
            CoUninitialize();
            return 0;
        }
        if (!options.input || !options.output || *options.input >= inputs.size() || *options.output >= outputs.size())
            throw std::invalid_argument("Select valid --input and --output indices");

        std::wcout
            << L"\nWARNING: this is a measurement prototype, not hearing protection.\n"
            << L"Remove the headset first, start at low volume, then measure with the microphone near the ear.\n"
            << L"Controls: X = emergency stop, R = re-arm, +/- = strength, Q = quit.\n"
            << L"Global emergency hotkey: Ctrl+Shift+F12.\n\n";
        if (options.experimentalWideband)
            std::wcout << L"EXPERIMENTAL WIDEBAND ACTIVE: 20-500 Hz, strength limit 100%, -12 dBFS limiter remains ON.\n\n";

        SetConsoleCtrlHandler(consoleHandler, TRUE);
        anc::audio::WasapiEngine engine(inputs[*options.input].id, outputs[*options.output].id,
                                        options.dsp, options.buffer);
        engine.start();
        std::wcout << L"Audio sample rate: " << std::fixed << std::setprecision(0) << engine.sampleRate() << L" Hz\n";
        const bool hotkeyRegistered = RegisterHotKey(nullptr, 1, MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, VK_F12) != FALSE;
        if (!hotkeyRegistered) std::wcout << L"Could not register the global hotkey; use X in this console.\n";
        float strength = options.dsp.strength;
        auto nextStatus = std::chrono::steady_clock::now();
        while (!stopRequested.load(std::memory_order_relaxed)) {
            MSG message;
            while (PeekMessageW(&message, nullptr, WM_HOTKEY, WM_HOTKEY, PM_REMOVE))
                if (message.wParam == 1) engine.processor().emergencyStop();
            if (_kbhit()) {
                const wchar_t key = static_cast<wchar_t>(_getwch());
                if (key == L'q' || key == L'Q') break;
                if (key == L'x' || key == L'X') engine.processor().emergencyStop();
                if (key == L'r' || key == L'R') engine.processor().clearEmergency();
                if (key == L'+') { strength = std::min(options.dsp.maxStrength, strength + 0.01f); engine.processor().setStrength(strength); }
                if (key == L'-') { strength = std::max(0.0f, strength - 0.01f); engine.processor().setStrength(strength); }
            }
            const auto now = std::chrono::steady_clock::now();
            if (now >= nextStatus) {
                const auto stats = engine.stats();
                std::wcout << L"\rMic " << std::fixed << std::setprecision(4) << stats.microphoneRms
                           << L" | ANC " << stats.ancRms
                           << L" | peak " << std::setprecision(0) << stats.dominantFrequencyHz << L" Hz"
                           << L" | gain " << std::setprecision(0) << engine.processor().currentStrength() * 100.0f << L"%"
                           << L" | periods " << stats.inputPeriodFrames << L"/" << stats.outputPeriodFrames
                           << L" | latency " << std::setprecision(2) << stats.inputLatencyMs + stats.outputLatencyMs << L" ms"
                           << L" | xrun " << stats.captureOverruns << L"/" << stats.renderUnderruns
                           << (engine.processor().voiceDetected() ? L" | VOICE" : L"        ")
                           << (engine.processor().impulseDetected() ? L" | IMPULSE" : L"          ")
                           << (engine.processor().emergencyLatched() ? L" | STOPPED" : L"          ") << L"    " << std::flush;
                if (!stats.microphoneOnline) {
                    std::wcout << L"\nMicrophone disconnected; ANC was stopped.\n";
                    break;
                }
                if (!stats.running) {
                    std::wcout << L"\nAudio rendering stopped because of a device error.\n";
                    break;
                }
                nextStatus = now + std::chrono::milliseconds(250);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        engine.processor().emergencyStop();
        std::this_thread::sleep_for(std::chrono::milliseconds(120));
        engine.stop();
        if (hotkeyRegistered) UnregisterHotKey(nullptr, 1);
        std::wcout << L"\nANC stopped.\n";
    } catch (const std::exception& error) {
        std::wcerr << L"\nError: " << error.what() << L"\n";
        exitCode = 1;
    }
    CoUninitialize();
    return exitCode;
}
