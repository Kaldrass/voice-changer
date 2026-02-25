// src/dsp/PitchShiftEffect.cpp
#include "dsp/PitchShiftEffect.h"

#include <SoundTouch.h>   // from SoundTouch include dir
#include <algorithm>
#include <cstring>

PitchShiftEffect::PitchShiftEffect(int sampleRate, int channels)
{
    m_st = new soundtouch::SoundTouch();
    EnsureConfig(sampleRate, channels);

    // Good defaults for real-time
    m_st->setTempoChange(0.0f);
    m_st->setRateChange(0.0f);
    m_st->setPitchSemiTones(0.0f);

    // Optional: reduce latency (quality tradeoffs)
    // m_st->setSetting(SETTING_SEQUENCE_MS, 40);
    // m_st->setSetting(SETTING_SEEKWINDOW_MS, 15);
    // m_st->setSetting(SETTING_OVERLAP_MS, 8);
}

PitchShiftEffect::~PitchShiftEffect()
{
    delete m_st;
    m_st = nullptr;
}

void PitchShiftEffect::EnsureConfig(int sampleRate, int channels) noexcept
{
    if (sampleRate <= 0 || channels <= 0) return;

    if (sampleRate == m_sampleRate && channels == m_channels) return;

    m_sampleRate = sampleRate;
    m_channels = channels;

    m_st->setSampleRate(sampleRate);
    m_st->setChannels(channels);
    m_st->clear();
}

void PitchShiftEffect::Process(float* interleaved, size_t frames, int channels) noexcept
{
    if (!m_st || frames == 0 || channels <= 0) return;

    // Safety: if caller channels differ, reconfigure.
    EnsureConfig(m_sampleRate, channels);

    const float st = m_semiTones.load(std::memory_order_relaxed);
    m_st->setPitchSemiTones(st);

    // SoundTouch expects "samples" as frames (per channel) in float.
    m_st->putSamples(interleaved, static_cast<uint32_t>(frames));

    m_out.resize(frames * static_cast<size_t>(channels));

    const uint32_t received = m_st->receiveSamples(m_out.data(), static_cast<uint32_t>(frames));

    if (received == static_cast<uint32_t>(frames))
    {
        std::memcpy(interleaved, m_out.data(), frames * static_cast<size_t>(channels) * sizeof(float));
    }
    else if (received > 0)
    {
        // Partial output: copy what we have and pad rest with zeros.
        const size_t gotSamples = static_cast<size_t>(received) * static_cast<size_t>(channels);
        std::memcpy(interleaved, m_out.data(), gotSamples * sizeof(float));
        std::fill(interleaved + gotSamples, interleaved + frames * static_cast<size_t>(channels), 0.0f);
    }
    else
    {
        // No output yet (internal latency): output silence to keep stream stable.
        std::fill(interleaved, interleaved + frames * static_cast<size_t>(channels), 0.0f);
    }
}