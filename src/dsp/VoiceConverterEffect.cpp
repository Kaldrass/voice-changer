#include "dsp/VoiceConverterEffect.h"

VoiceConverterEffect::VoiceConverterEffect(std::unique_ptr<IVoiceConverter> converter, int sampleRate)
    : m_converter(std::move(converter)), m_sampleRate(sampleRate > 0 ? sampleRate : 48000)
{
}

void VoiceConverterEffect::SetProfile(const std::string& profile)
{
    if (m_converter) m_converter->SetProfile(profile);
}

void VoiceConverterEffect::SetBlend(float blend01) noexcept
{
    if (m_converter) m_converter->SetBlend(blend01);
}

void VoiceConverterEffect::SetIntensity(float intensity01) noexcept
{
    if (m_converter) m_converter->SetIntensity(intensity01);
}

void VoiceConverterEffect::Process(float* interleaved, size_t frames, int channels) noexcept
{
    if (!m_converter) return;
    m_converter->ProcessInterleaved(interleaved, frames, channels, m_sampleRate);
}
