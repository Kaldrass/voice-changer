// src/dsp/GainEffect.h
#pragma once
#include "dsp/IEffect.h"
#include <atomic>

class GainEffect final : public IEffect
{
public:
    void SetGain(float g) noexcept { m_gain.store(g, std::memory_order_relaxed); }

    void Process(float* interleaved, size_t frames, int channels) noexcept override;

private:
    std::atomic<float> m_gain{1.0f};
};