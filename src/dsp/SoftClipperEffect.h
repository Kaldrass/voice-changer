// src/dsp/SoftClipperEffect.h
#pragma once

#include "dsp/IEffect.h"
#include <atomic>

class SoftClipperEffect final : public IEffect
{
public:
    // drive > 1.0 increases saturation. Typical: 1.0 to 10.0
    void SetDrive(float d) noexcept { m_drive.store(d, std::memory_order_relaxed); }

    // output gain after clipping. Typical: 1.0
    void SetOutputGain(float g) noexcept { m_outGain.store(g, std::memory_order_relaxed); }

    void Process(float* interleaved, size_t frames, int channels) noexcept override;

private:
    std::atomic<float> m_drive{2.0f};
    std::atomic<float> m_outGain{1.0f};
};