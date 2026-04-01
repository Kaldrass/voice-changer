#pragma once

#include "ai/IVoiceConverter.h"
#include "dsp/IEffect.h"

#include <memory>
#include <string>

class VoiceConverterEffect final : public IEffect
{
public:
    VoiceConverterEffect(std::unique_ptr<IVoiceConverter> converter, int sampleRate);

    void SetProfile(const std::string& profile);
    void SetBlend(float blend01) noexcept;
    void SetIntensity(float intensity01) noexcept;

    void Process(float* interleaved, size_t frames, int channels) noexcept override;

private:
    std::unique_ptr<IVoiceConverter> m_converter;
    int m_sampleRate = 48000;
};
