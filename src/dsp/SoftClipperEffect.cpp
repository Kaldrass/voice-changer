// src/dsp/SoftClipperEffect.cpp

#include "dsp/SoftClipperEffect.h"
#include <cmath>

static inline float SoftClipTanh(float x) noexcept
{
    // tanh is smooth and avoids harsh clipping.
    return std::tanh(x);
}

void SoftClipperEffect::Process(float* interleaved, size_t frames, int channels) noexcept
{
    const float drive = m_drive.load(std::memory_order_relaxed);
    const float outGain = m_outGain.load(std::memory_order_relaxed);

    const size_t n = frames * static_cast<size_t>(channels);
    for (size_t i = 0; i < n; ++i)
    {
        const float x = interleaved[i] * drive;
        interleaved[i] = SoftClipTanh(x) * outGain;
    }
}