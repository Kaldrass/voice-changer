// src/dsp/GainEffect.cpp
#include "dsp/GainEffect.h"

void GainEffect::Process(float* interleaved, size_t frames, int channels) noexcept
{
    const float g = m_gain.load(std::memory_order_relaxed);
    const size_t n = frames * static_cast<size_t>(channels);
    for (size_t i = 0; i < n; ++i) interleaved[i] *= g;
}