// src/dsp/Biquad.h
#pragma once

#include <vector>
#include <cstddef>

class Biquad
{
public:
    void Reset(int channels);

    void SetIdentity() noexcept;

    // All frequency params in Hz, sampleRate in Hz.
    void SetHighPass(float sampleRate, float freqHz, float q) noexcept;
    void SetLowPass(float sampleRate, float freqHz, float q) noexcept;

    // Peaking EQ: gainDB can be positive or negative.
    void SetPeaking(float sampleRate, float centerHz, float q, float gainDB) noexcept;

    void Process(float* interleaved, size_t frames, int channels) noexcept;

private:
    // Direct Form II Transposed state per channel
    std::vector<float> m_z1;
    std::vector<float> m_z2;

    // Normalized coefficients (a0 assumed 1)
    float m_b0 = 1.0f;
    float m_b1 = 0.0f;
    float m_b2 = 0.0f;
    float m_a1 = 0.0f;
    float m_a2 = 0.0f;
};