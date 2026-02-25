// src/audio/DeviceUtils.cpp
#include "audio/DeviceUtils.h"
#include <functiondiscoverykeys_devpkey.h>
#include <comdef.h>
#include <stdexcept>

static EDataFlow ToEDataFlow(AudioFlow flow)
{
    return (flow == AudioFlow::Capture) ? eCapture : eRender;
}

static std::wstring ReadDeviceName(IMMDevice* dev)
{
    IPropertyStore* props = nullptr;
    HRESULT hr = dev->OpenPropertyStore(STGM_READ, &props);
    if (FAILED(hr)) throw std::runtime_error("OpenPropertyStore failed");

    PROPVARIANT varName;
    PropVariantInit(&varName);

    hr = props->GetValue(PKEY_Device_FriendlyName, &varName);
    props->Release();

    if (FAILED(hr)) throw std::runtime_error("GetValue(PKEY_Device_FriendlyName) failed");

    std::wstring name = (varName.vt == VT_LPWSTR && varName.pwszVal) ? varName.pwszVal : L"";
    PropVariantClear(&varName);
    return name;
}

std::vector<AudioDeviceInfo> EnumerateDevices(AudioFlow flow)
{
    IMMDeviceEnumerator* enumerator = nullptr;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                  __uuidof(IMMDeviceEnumerator),
                                  reinterpret_cast<void**>(&enumerator));
    if (FAILED(hr)) throw std::runtime_error("CoCreateInstance(MMDeviceEnumerator) failed");

    IMMDeviceCollection* collection = nullptr;
    hr = enumerator->EnumAudioEndpoints(ToEDataFlow(flow), DEVICE_STATE_ACTIVE, &collection);
    enumerator->Release();
    if (FAILED(hr)) throw std::runtime_error("EnumAudioEndpoints failed");

    UINT count = 0;
    hr = collection->GetCount(&count);
    if (FAILED(hr))
    {
        collection->Release();
        throw std::runtime_error("IMMDeviceCollection::GetCount failed");
    }

    std::vector<AudioDeviceInfo> out;
    out.reserve(count);

    for (UINT i = 0; i < count; ++i)
    {
        IMMDevice* dev = nullptr;
        hr = collection->Item(i, &dev);
        if (FAILED(hr)) continue;

        LPWSTR devId = nullptr;
        hr = dev->GetId(&devId);
        if (FAILED(hr))
        {
            dev->Release();
            continue;
        }

        AudioDeviceInfo info;
        info.id = devId;
        CoTaskMemFree(devId);

        try
        {
            info.name = ReadDeviceName(dev);
        }
        catch (...)
        {
            info.name = L"(unknown)";
        }

        dev->Release();
        out.emplace_back(std::move(info));
    }

    collection->Release();
    return out;
}

IMMDevice* GetDeviceById(const std::wstring& id)
{
    IMMDeviceEnumerator* enumerator = nullptr;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                  __uuidof(IMMDeviceEnumerator),
                                  reinterpret_cast<void**>(&enumerator));
    if (FAILED(hr)) throw std::runtime_error("CoCreateInstance(MMDeviceEnumerator) failed");

    IMMDevice* dev = nullptr;
    hr = enumerator->GetDevice(id.c_str(), &dev);
    enumerator->Release();

    if (FAILED(hr)) throw std::runtime_error("IMMDeviceEnumerator::GetDevice failed");
    return dev; // Caller must Release()
}

static std::wstring ToLower(std::wstring s)
{
    for (auto& ch : s) ch = static_cast<wchar_t>(towlower(ch));
    return s;
}

int FindDeviceIndexBySubstring(const std::vector<AudioDeviceInfo>& devices, const std::wstring& needle)
{
    if (needle.empty()) return -1;

    const std::wstring n = ToLower(needle);
    for (int i = 0; i < static_cast<int>(devices.size()); ++i)
    {
        const std::wstring name = ToLower(devices[i].name);
        if (name.find(n) != std::wstring::npos) return i;
    }
    return -1;
}