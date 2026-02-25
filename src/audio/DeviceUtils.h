// src/audio/DeviceUtils.h
#pragma once
#include <string>
#include <vector>
#include <windows.h>
#include <mmdeviceapi.h>

enum class AudioFlow
{
    Capture,
    Render
};

struct AudioDeviceInfo
{
    std::wstring id;
    std::wstring name;
};

std::vector<AudioDeviceInfo> EnumerateDevices(AudioFlow flow);

IMMDevice* GetDeviceById(const std::wstring& id);

int FindDeviceIndexBySubstring(const std::vector<AudioDeviceInfo>& devices, const std::wstring& needle);