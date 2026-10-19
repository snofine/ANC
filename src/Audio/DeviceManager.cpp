#include "Audio/DeviceManager.h"

#include <Windows.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <wrl/client.h>

#include <stdexcept>

using Microsoft::WRL::ComPtr;

namespace anc::audio {
namespace {

void check(HRESULT hr, const char* operation) {
    if (FAILED(hr)) throw std::runtime_error(std::string(operation) + " failed (HRESULT " + std::to_string(hr) + ")");
}

std::vector<AudioDeviceInfo> enumerate(EDataFlow flow) {
    ComPtr<IMMDeviceEnumerator> enumerator;
    check(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                           IID_PPV_ARGS(&enumerator)), "Create device enumerator");

    ComPtr<IMMDevice> defaultDevice;
    std::wstring defaultId;
    if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(flow, eConsole, &defaultDevice))) {
        LPWSTR id = nullptr;
        if (SUCCEEDED(defaultDevice->GetId(&id))) {
            defaultId = id;
            CoTaskMemFree(id);
        }
    }

    ComPtr<IMMDeviceCollection> collection;
    check(enumerator->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, &collection), "Enumerate audio endpoints");
    UINT count = 0;
    check(collection->GetCount(&count), "Read endpoint count");

    std::vector<AudioDeviceInfo> result;
    result.reserve(count);
    for (UINT i = 0; i < count; ++i) {
        ComPtr<IMMDevice> device;
        check(collection->Item(i, &device), "Read endpoint");
        LPWSTR id = nullptr;
        check(device->GetId(&id), "Read endpoint id");
        std::wstring ownedId(id);
        CoTaskMemFree(id);

        ComPtr<IPropertyStore> properties;
        check(device->OpenPropertyStore(STGM_READ, &properties), "Open endpoint properties");
        PROPVARIANT value;
        PropVariantInit(&value);
        check(properties->GetValue(PKEY_Device_FriendlyName, &value), "Read endpoint name");
        const std::wstring name = value.vt == VT_LPWSTR ? value.pwszVal : L"Unnamed device";
        PropVariantClear(&value);
        result.push_back({ownedId, name, ownedId == defaultId});
    }
    return result;
}

} // namespace

std::vector<AudioDeviceInfo> DeviceManager::captureDevices() { return enumerate(eCapture); }
std::vector<AudioDeviceInfo> DeviceManager::renderDevices() { return enumerate(eRender); }

} // namespace anc::audio
