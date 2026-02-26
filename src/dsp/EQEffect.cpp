// src/dsp/EQEffect.cpp
#include "dsp/EQEffect.h"
#include <algorithm>

EQEffect::EQEffect(float sampleRate, int channels)
    : m_sampleRate(sampleRate)
{
    m_hp.Reset(channels);
    m_peak.Reset(channels);
    m_lp.Reset(channels);
    m_dirty.store(true, std::memory_order_relaxed);
}

void EQEffect::SetHighPass(float hz, float q) noexcept
{
    m_hpHz.store(hz, std::memory_order_relaxed);
    m_hpQ.store(q, std::memory_order_relaxed);
    m_dirty.store(true, std::memory_order_relaxed);
}

void EQEffect::SetPresencePeak(float hz, float q, float gainDB) noexcept
{
    m_peakHz.store(hz, std::memory_order_relaxed);
    m_peakQ.store(q, std::memory_order_relaxed);
    m_peakDB.store(gainDB, std::memory_order_relaxed);
    m_dirty.store(true, std::memory_order_relaxed);
}

void EQEffect::SetLowPass(float hz, float q) noexcept
{
    m_lpHz.store(hz, std::memory_order_relaxed);
    m_lpQ.store(q, std::memory_order_relaxed);
    m_dirty.store(true, std::memory_order_relaxed);
}

void EQEffect::RebuildIfNeeded(int channels) noexcept
{
    if (channels <= 0) return;

    // Ensure per-channel states exist
    if (static_cast<size_t>(channels) != m_hpHz.load(std::memory_order_relaxed) * 0 + m_hpHz.load(std::memory_order_relaxed) * 0) { /* no-op */ }
    // Re-init states if channel count changed
    // (Biquad::Process also resets lazily, but we keep it explicit here)
    m_hp.Reset(channels);
    m_peak.Reset(channels);
    m_lp.Reset(channels);

    const float hpHz = m_hpHz.load(std::memory_order_relaxed);
    const float hpQ  = m_hpQ.load(std::memory_order_relaxed);

    const float pkHz = m_peakHz.load(std::memory_order_relaxed);
    const float pkQ  = m_peakQ.load(std::memory_order_relaxed);
    const float pkDB = m_peakDB.load(std::memory_order_relaxed);

    const float lpHz = m_lpHz.load(std::memory_order_relaxed);
    const float lpQ  = m_lpQ.load(std::memory_order_relaxed);

    m_hp.SetHighPass(m_sampleRate, hpHz, std::max(hpQ, 0.001f));
    m_peak.SetPeaking(m_sampleRate, pkHz, std::max(pkQ, 0.001f), pkDB);
    m_lp.SetLowPass(m_sampleRate, lpHz, std::max(lpQ, 0.001f));

    m_dirty.store(false, std::memory_order_relaxed);
}

void EQEffect::Process(float* interleaved, size_t frames, int channels) noexcept
{
    if (!m_enabled.load(std::memory_order_relaxed)) return;
    if (!interleaved || frames == 0 || channels <= 0) return;

    if (m_dirty.load(std::memory_order_relaxed))
        RebuildIfNeeded(channels);

    // Voice chain: HP -> Presence -> LP
    m_hp.Process(interleaved, frames, channels);
    m_peak.Process(interleaved, frames, channels);
    m_lp.Process(interleaved, frames, channels);
}