#include "dsp/EQEffect.h"

#include <cmath>
#include <vector>

bool TestEQEffectFiniteAndChangesSignal()
{
    constexpr int sampleRate = 48000;
    constexpr int channels = 2;
    constexpr size_t frames = 256;

    EQEffect eq(static_cast<float>(sampleRate), channels);
    eq.SetHighPass(100.0f, 0.707f);
    eq.SetPresencePeak(3200.0f, 1.0f, 3.0f);
    eq.SetLowPass(12000.0f, 0.707f);

    std::vector<float> in(frames * channels, 0.0f);
    for (size_t i = 0; i < frames; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(sampleRate);
        const float x = 0.25f * std::sinf(2.0f * 3.14159265358979323846f * 1000.0f * t);
        in[i * channels + 0] = x;
        in[i * channels + 1] = x;
    }

    std::vector<float> out = in;
    eq.Process(out.data(), frames, channels);

    bool changed = false;
    for (size_t i = 0; i < out.size(); ++i)
    {
        if (!std::isfinite(out[i])) return false;
        if (std::fabs(out[i]) > 4.0f) return false;
        if (std::fabs(out[i] - in[i]) > 1e-6f) changed = true;
    }

    return changed;
}
