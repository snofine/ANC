#pragma once

#include <string>
#include <vector>

namespace anc::audio {

struct AudioDeviceInfo {
    std::wstring id;
    std::wstring name;
    bool isDefault{};
};

class DeviceManager {
public:
    static std::vector<AudioDeviceInfo> captureDevices();
    static std::vector<AudioDeviceInfo> renderDevices();
};

} // namespace anc::audio

