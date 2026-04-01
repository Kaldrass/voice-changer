#pragma once

#include <cstddef>
#include <string>

class IVoiceConverter
{
public:
    virtual ~IVoiceConverter() = default;

    virtual void SetProfile(const std::string& profile) = 0;
    virtual void SetBlend(float blend01) = 0;
    virtual void SetIntensity(float intensity01) = 0;

    virtual void ProcessInterleaved(float* interleaved, size_t frames, int channels, int sampleRate) noexcept = 0;
};
