#include "dsp/PitchShiftEffect.h"

#include <cmath>
#include <vector>

bool TestPitchShiftEffectFinite()
{
    constexpr int sampleRate = 48000;
    constexpr int channels = 2;
    constexpr size_t frames = 256;

    PitchShiftEffect pitch(sampleRate, channels);
    pitch.SetLowLatencyParams(20, 8, 4);
    pitch.SetPitchSemiTones(4.0f);

    std::vector<float> block(frames * channels, 0.0f);
    for (size_t i = 0; i < frames; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(sampleRate);
        const float x = 0.15f * std::sinf(2.0f * 3.14159265358979323846f * 220.0f * t);
        block[i * channels + 0] = x;
        block[i * channels + 1] = x;
    }

    pitch.Process(block.data(), frames, channels);

    for (float v : block)
    {
        if (!std::isfinite(v)) return false;
        if (std::fabs(v) > 4.0f) return false;
    }

    return true;
}
