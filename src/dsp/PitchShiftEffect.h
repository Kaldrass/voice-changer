// src/dsp/PitchShiftEffect.h
#pragma once

#include "dsp/IEffect.h"
#include <atomic>
#include <vector>
#include <cstddef>

// Forward declaration to avoid including SoundTouch in the header (faster builds).
namespace soundtouch { class SoundTouch; }

class PitchShiftEffect final : public IEffect
{
public:
    PitchShiftEffect(int sampleRate, int channels);
    ~PitchShiftEffect();

    // Typical range: [-12, +12]
    void SetPitchSemiTones(float st) noexcept { m_semiTones.store(st, std::memory_order_relaxed); }

    size_t GetQueuedFrames() const noexcept;

    void Process(float* interleaved, size_t frames, int channels) noexcept override;

    void SetLowLatencyParams(int seqMs, int seekMs, int overlapMs) noexcept;

private:
    void EnsureConfig(int sampleRate, int channels) noexcept;
    void FifoPush(const float* samples, size_t sampleCount);
    size_t FifoPop(float* dst, size_t sampleCount);

private:
    soundtouch::SoundTouch* m_st = nullptr;

    int m_sampleRate = 0;
    int m_channels = 0;

    std::atomic<float> m_semiTones{0.0f};

    std::vector<float> m_tmp; // temp input buffer
    std::vector<float> m_fifo; // FIFO samples
    size_t m_fifoRead = 0;
    size_t m_fifoWrite = 0;
    size_t m_fifoCount = 0;
};