// src/dsp/EQEffect.h
#pragma once

#include "dsp/IEffect.h"
#include "dsp/Biquad.h"
#include <atomic>

class EQEffect final : public IEffect
{
public:
    // Typical voice defaults: HP 90 Hz, presence +2 dB at 3 kHz, LP 12000 Hz
    EQEffect(float sampleRate, int channels);

    void SetEnabled(bool e) noexcept { m_enabled.store(e, std::memory_order_relaxed); }

    void SetHighPass(float hz, float q = 0.707f) noexcept;
    void SetPresencePeak(float hz, float q, float gainDB) noexcept;
    void SetLowPass(float hz, float q = 0.707f) noexcept;

    void Process(float* interleaved, size_t frames, int channels) noexcept override;

private:
    void RebuildIfNeeded(int channels) noexcept;

private:
    float m_sampleRate = 48000.0f;

    std::atomic<bool> m_enabled{true};
    std::atomic<bool> m_dirty{true};

    std::atomic<float> m_hpHz{90.0f};
    std::atomic<float> m_hpQ{0.707f};

    std::atomic<float> m_peakHz{3000.0f};
    std::atomic<float> m_peakQ{1.0f};
    std::atomic<float> m_peakDB{2.0f};

    std::atomic<float> m_lpHz{12000.0f};
    std::atomic<float> m_lpQ{0.707f};

    Biquad m_hp;
    Biquad m_peak;
    Biquad m_lp;

    int m_lastChannels = 0;
};