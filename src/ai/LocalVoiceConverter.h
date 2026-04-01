#pragma once

#include "ai/IVoiceConverter.h"

#include <vector>

class LocalVoiceConverter final : public IVoiceConverter
{
public:
    void SetProfile(const std::string& profile) override;
    void SetBlend(float blend01) override;
    void SetIntensity(float intensity01) override;

    void ProcessInterleaved(float* interleaved, size_t frames, int channels, int sampleRate) noexcept override;

private:
    enum class Profile
    {
        Neutral,
        Bright,
        Dark,
        Robot
    };

    static float Clamp01(float v) noexcept;
    static float FastTanh(float x) noexcept;
    void EnsureState(int channels) noexcept;

private:
    Profile m_profile = Profile::Neutral;
    float m_blend = 0.65f;
    float m_intensity = 0.4f;

    std::vector<float> m_prevIn;
    std::vector<float> m_lpState;
    std::vector<float> m_prevOut;
    std::vector<float> m_phase;
};
