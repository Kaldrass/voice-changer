// src/dsp/PitchShiftEffect.h
#pragma once

#include "dsp/IEffect.h"
#include <atomic>
#include <vector>

// Forward declaration to avoid including SoundTouch in the header (faster builds).
namespace soundtouch { class SoundTouch; }

class PitchShiftEffect final : public IEffect
{
public:
    PitchShiftEffect(int sampleRate, int channels);
    ~PitchShiftEffect();

    // Typical range: [-12, +12]
    void SetPitchSemiTones(float st) noexcept { m_semiTones.store(st, std::memory_order_relaxed); }

    void Process(float* interleaved, size_t frames, int channels) noexcept override;

private:
    void EnsureConfig(int sampleRate, int channels) noexcept;

private:
    soundtouch::SoundTouch* m_st = nullptr;

    int m_sampleRate = 0;
    int m_channels = 0;

    std::atomic<float> m_semiTones{0.0f};

    std::vector<float> m_out; // temp output buffer
};