// src/dsp/Biquad.cpp
#include "dsp/Biquad.h"

#include <algorithm>
#include <cmath>

static inline float ClampFreq(float f, float sr) noexcept
{
    const float nyq = 0.5f * sr;
    if (f < 1.0f) return 1.0f;
    if (f > nyq * 0.99f) return nyq * 0.99f;
    return f;
}

void Biquad::Reset(int channels)
{
    if (channels < 1) channels = 1;
    m_z1.assign(static_cast<size_t>(channels), 0.0f);
    m_z2.assign(static_cast<size_t>(channels), 0.0f);
}

void Biquad::SetIdentity() noexcept
{
    m_b0 = 1.0f; m_b1 = 0.0f; m_b2 = 0.0f;
    m_a1 = 0.0f; m_a2 = 0.0f;
}

void Biquad::SetHighPass(float sampleRate, float freqHz, float q) noexcept
{
    const float sr = sampleRate;
    const float f = ClampFreq(freqHz, sr);
    const float Q = std::max(q, 0.001f);

    const float w0 = 2.0f * 3.14159265358979323846f * (f / sr);
    const float cosw0 = std::cos(w0);
    const float sinw0 = std::sin(w0);
    const float alpha = sinw0 / (2.0f * Q);

    // RBJ cookbook high-pass
    float b0 =  (1.0f + cosw0) * 0.5f;
    float b1 = -(1.0f + cosw0);
    float b2 =  (1.0f + cosw0) * 0.5f;
    float a0 =  1.0f + alpha;
    float a1 = -2.0f * cosw0;
    float a2 =  1.0f - alpha;

    // Normalize
    m_b0 = b0 / a0;
    m_b1 = b1 / a0;
    m_b2 = b2 / a0;
    m_a1 = a1 / a0;
    m_a2 = a2 / a0;
}

void Biquad::SetLowPass(float sampleRate, float freqHz, float q) noexcept
{
    const float sr = sampleRate;
    const float f = ClampFreq(freqHz, sr);
    const float Q = std::max(q, 0.001f);

    const float w0 = 2.0f * 3.14159265358979323846f * (f / sr);
    const float cosw0 = std::cos(w0);
    const float sinw0 = std::sin(w0);
    const float alpha = sinw0 / (2.0f * Q);

    // RBJ cookbook low-pass
    float b0 = (1.0f - cosw0) * 0.5f;
    float b1 = (1.0f - cosw0);
    float b2 = (1.0f - cosw0) * 0.5f;
    float a0 = 1.0f + alpha;
    float a1 = -2.0f * cosw0;
    float a2 = 1.0f - alpha;

    // Normalize
    m_b0 = b0 / a0;
    m_b1 = b1 / a0;
    m_b2 = b2 / a0;
    m_a1 = a1 / a0;
    m_a2 = a2 / a0;
}

void Biquad::SetPeaking(float sampleRate, float centerHz, float q, float gainDB) noexcept
{
    const float sr = sampleRate;
    const float f = ClampFreq(centerHz, sr);
    const float Q = std::max(q, 0.001f);

    const float A = std::pow(10.0f, gainDB / 40.0f); // sqrt(10^(dB/20))
    const float w0 = 2.0f * 3.14159265358979323846f * (f / sr);
    const float cosw0 = std::cos(w0);
    const float sinw0 = std::sin(w0);
    const float alpha = sinw0 / (2.0f * Q);

    // RBJ cookbook peaking EQ
    float b0 = 1.0f + alpha * A;
    float b1 = -2.0f * cosw0;
    float b2 = 1.0f - alpha * A;
    float a0 = 1.0f + alpha / A;
    float a1 = -2.0f * cosw0;
    float a2 = 1.0f - alpha / A;

    // Normalize
    m_b0 = b0 / a0;
    m_b1 = b1 / a0;
    m_b2 = b2 / a0;
    m_a1 = a1 / a0;
    m_a2 = a2 / a0;
}

void Biquad::Process(float* interleaved, size_t frames, int channels) noexcept
{
    if (!interleaved || frames == 0 || channels <= 0) return;
    if (static_cast<size_t>(channels) != m_z1.size()) Reset(channels);

    const size_t ch = static_cast<size_t>(channels);
    for (size_t f = 0; f < frames; ++f)
    {
        float* frame = interleaved + f * ch;
        for (size_t c = 0; c < ch; ++c)
        {
            const float x = frame[c];
            const float y = m_b0 * x + m_z1[c];
            m_z1[c] = m_b1 * x - m_a1 * y + m_z2[c];
            m_z2[c] = m_b2 * x - m_a2 * y;
            frame[c] = y;
        }
    }
}