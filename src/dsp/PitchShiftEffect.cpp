// src/dsp/PitchShiftEffect.cpp
#include "dsp/PitchShiftEffect.h"

#include <SoundTouch.h>
#include <algorithm>
#include <cstring>

PitchShiftEffect::PitchShiftEffect(int sampleRate, int channels)
{
    m_st = new soundtouch::SoundTouch();
    EnsureConfig(sampleRate, channels);

    m_st->setTempoChange(0.0f);
    m_st->setRateChange(0.0f);
    m_st->setPitchSemiTones(0.0f);

    // m_st->setSetting(SETTING_SEQUENCE_MS, 20);
    // m_st->setSetting(SETTING_SEEKWINDOW_MS, 8);
    // m_st->setSetting(SETTING_OVERLAP_MS, 4);

    // FIFO capacity: a few blocks
    const size_t fifoSamples = 8192u * static_cast<size_t>(channels);
    m_fifo.assign(fifoSamples, 0.0f);
    m_fifoRead = m_fifoWrite = m_fifoCount = 0;
}

PitchShiftEffect::~PitchShiftEffect()
{
    delete m_st;
    m_st = nullptr;
}

void PitchShiftEffect::EnsureConfig(int sampleRate, int channels) noexcept
{
    if (!m_st || sampleRate <= 0 || channels <= 0) return;
    if (sampleRate == m_sampleRate && channels == m_channels) return;

    m_sampleRate = sampleRate;
    m_channels = channels;
    m_st->setSampleRate(sampleRate);
    m_st->setChannels(channels);
    m_st->clear();

    m_fifoRead = m_fifoWrite = m_fifoCount = 0;
}

void PitchShiftEffect::FifoPush(const float* samples, size_t sampleCount)
{
    const size_t cap = m_fifo.size();
    if (cap == 0) return;

    // Drop oldest if overflow (keeps realtime stable)
    if (sampleCount > cap)
    {
        samples += (sampleCount - cap);
        sampleCount = cap;
    }
    if (m_fifoCount + sampleCount > cap)
    {
        const size_t overflow = (m_fifoCount + sampleCount) - cap;
        m_fifoRead = (m_fifoRead + overflow) % cap;
        m_fifoCount -= overflow;
    }

    size_t first = std::min(sampleCount, cap - m_fifoWrite);
    std::memcpy(&m_fifo[m_fifoWrite], samples, first * sizeof(float));
    size_t remain = sampleCount - first;
    if (remain > 0)
        std::memcpy(&m_fifo[0], samples + first, remain * sizeof(float));

    m_fifoWrite = (m_fifoWrite + sampleCount) % cap;
    m_fifoCount += sampleCount;
}

size_t PitchShiftEffect::FifoPop(float* dst, size_t sampleCount)
{
    const size_t cap = m_fifo.size();
    const size_t n = std::min(sampleCount, m_fifoCount);

    size_t first = std::min(n, cap - m_fifoRead);
    std::memcpy(dst, &m_fifo[m_fifoRead], first * sizeof(float));
    size_t remain = n - first;
    if (remain > 0)
        std::memcpy(dst + first, &m_fifo[0], remain * sizeof(float));

    m_fifoRead = (m_fifoRead + n) % cap;
    m_fifoCount -= n;
    return n;
}

size_t PitchShiftEffect::GetQueuedFrames() const noexcept
{
    if (m_channels <= 0) return 0;
    return m_fifoCount / static_cast<size_t>(m_channels);
}

void PitchShiftEffect::Process(float* interleaved, size_t frames, int channels) noexcept
{
    if (!m_st || frames == 0 || channels <= 0) return;

    EnsureConfig(m_sampleRate, channels);

    const float st = m_semiTones.load(std::memory_order_relaxed);
    m_st->setPitchSemiTones(st);

    // Always feed input
    m_st->putSamples(interleaved, static_cast<uint32_t>(frames));

    // Pull as much as available (bounded)
    const size_t maxPullFrames = frames * 2; // allow filling FIFO
    m_tmp.resize(maxPullFrames * static_cast<size_t>(channels));
    uint32_t pulled = m_st->receiveSamples(m_tmp.data(), static_cast<uint32_t>(maxPullFrames));

    if (pulled > 0)
        FifoPush(m_tmp.data(), static_cast<size_t>(pulled) * static_cast<size_t>(channels));

    // Now output exactly 'frames' frames
    const size_t needSamples = frames * static_cast<size_t>(channels);
    size_t gotSamples = FifoPop(interleaved, needSamples);

    if (gotSamples < needSamples)
    {
        // No silence: bypass remaining with original input already fed.
        // Here we can't recover the original input (it was overwritten by partial pop),
        // so we fill with last sample value to avoid clicks.
        float last = (gotSamples > 0) ? interleaved[gotSamples - 1] : 0.0f;
        std::fill(interleaved + gotSamples, interleaved + needSamples, last);
    }
}

void PitchShiftEffect::SetLowLatencyParams(int seqMs, int seekMs, int overlapMs) noexcept
{
    if (!m_st) return;
    m_st->setSetting(SETTING_SEQUENCE_MS, seqMs);
    m_st->setSetting(SETTING_SEEKWINDOW_MS, seekMs);
    m_st->setSetting(SETTING_OVERLAP_MS, overlapMs);
}