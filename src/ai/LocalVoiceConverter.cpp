#include "ai/LocalVoiceConverter.h"

#include <algorithm>
#include <cmath>

void LocalVoiceConverter::SetProfile(const std::string& profile)
{
    if (profile == "bright") m_profile = Profile::Bright;
    else if (profile == "dark") m_profile = Profile::Dark;
    else if (profile == "robot") m_profile = Profile::Robot;
    else m_profile = Profile::Neutral;
}

void LocalVoiceConverter::SetBlend(float blend01)
{
    m_blend = Clamp01(blend01);
}

void LocalVoiceConverter::SetIntensity(float intensity01)
{
    m_intensity = Clamp01(intensity01);
}

float LocalVoiceConverter::Clamp01(float v) noexcept
{
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

float LocalVoiceConverter::FastTanh(float x) noexcept
{
    // Lightweight saturation suitable for realtime per-sample processing.
    return std::tanh(x);
}

void LocalVoiceConverter::EnsureState(int channels) noexcept
{
    if (channels <= 0) return;
    if (static_cast<int>(m_prevIn.size()) == channels) return;

    m_prevIn.assign(static_cast<size_t>(channels), 0.0f);
    m_lpState.assign(static_cast<size_t>(channels), 0.0f);
    m_prevOut.assign(static_cast<size_t>(channels), 0.0f);
    m_phase.assign(static_cast<size_t>(channels), 0.0f);
}

void LocalVoiceConverter::ProcessInterleaved(float* interleaved, size_t frames, int channels, int sampleRate) noexcept
{
    if (!interleaved || frames == 0 || channels <= 0 || sampleRate <= 0) return;

    EnsureState(channels);

    const float wet = m_blend;
    const float dry = 1.0f - wet;
    const float intensity = m_intensity;

    float hpCoeff = 0.96f;
    float lpCoeff = 0.12f;
    float drive = 1.0f + 1.8f * intensity;
    float modHz = 70.0f;

    switch (m_profile)
    {
    case Profile::Bright:
        hpCoeff = 0.975f;
        lpCoeff = 0.08f;
        drive = 1.2f + 2.0f * intensity;
        modHz = 90.0f;
        break;
    case Profile::Dark:
        hpCoeff = 0.93f;
        lpCoeff = 0.2f;
        drive = 1.0f + 1.3f * intensity;
        modHz = 55.0f;
        break;
    case Profile::Robot:
        hpCoeff = 0.97f;
        lpCoeff = 0.09f;
        drive = 1.5f + 2.5f * intensity;
        modHz = 120.0f;
        break;
    case Profile::Neutral:
    default:
        break;
    }

    const float phaseStep = 2.0f * 3.14159265358979323846f * (modHz / static_cast<float>(sampleRate));

    for (size_t f = 0; f < frames; ++f)
    {
        float* frame = interleaved + f * static_cast<size_t>(channels);

        for (int c = 0; c < channels; ++c)
        {
            const float x = frame[c];

            const float hp = x - hpCoeff * m_prevIn[static_cast<size_t>(c)];
            m_prevIn[static_cast<size_t>(c)] = x;

            m_lpState[static_cast<size_t>(c)] += lpCoeff * (hp - m_lpState[static_cast<size_t>(c)]);
            float y = m_lpState[static_cast<size_t>(c)];

            y = FastTanh(y * drive);

            if (m_profile == Profile::Robot)
            {
                const float m = std::sinf(m_phase[static_cast<size_t>(c)]);
                y = y * (0.7f + 0.3f * m);
                m_phase[static_cast<size_t>(c)] += phaseStep;
                if (m_phase[static_cast<size_t>(c)] > 2.0f * 3.14159265358979323846f)
                    m_phase[static_cast<size_t>(c)] -= 2.0f * 3.14159265358979323846f;
            }

            // Gentle temporal smoothing to reduce metallic artifacts.
            y = 0.85f * y + 0.15f * m_prevOut[static_cast<size_t>(c)];
            m_prevOut[static_cast<size_t>(c)] = y;

            frame[c] = dry * x + wet * y;
        }
    }
}
