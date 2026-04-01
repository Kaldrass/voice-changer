#include "ai/LocalVoiceConverter.h"
#include "dsp/VoiceConverterEffect.h"

#include <cmath>
#include <memory>
#include <vector>

bool TestVoiceConverterEffectFinite()
{
    auto converter = std::make_unique<LocalVoiceConverter>();
    VoiceConverterEffect fx(std::move(converter), 48000);
    fx.SetProfile("bright");
    fx.SetBlend(0.7f);
    fx.SetIntensity(0.6f);

    constexpr size_t frames = 256;
    constexpr int channels = 2;

    std::vector<float> in(frames * channels, 0.0f);
    for (size_t i = 0; i < frames; ++i)
    {
        const float t = static_cast<float>(i) / 48000.0f;
        const float x = 0.2f * std::sinf(2.0f * 3.14159265358979323846f * 440.0f * t);
        in[i * channels + 0] = x;
        in[i * channels + 1] = x;
    }

    std::vector<float> out = in;
    fx.Process(out.data(), frames, channels);

    bool changed = false;
    for (size_t i = 0; i < out.size(); ++i)
    {
        if (!std::isfinite(out[i])) return false;
        if (std::fabs(out[i]) > 4.0f) return false;
        if (std::fabs(out[i] - in[i]) > 1e-6f) changed = true;
    }

    return changed;
}
