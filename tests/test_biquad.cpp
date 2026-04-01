#include "dsp/Biquad.h"

#include <cmath>
#include <vector>

static bool IsFinite(float v)
{
    return std::isfinite(v);
}

bool TestBiquadIdentity()
{
    Biquad bq;
    bq.Reset(2);
    bq.SetIdentity();

    std::vector<float> in = {
        0.1f, -0.1f,
        0.5f, -0.5f,
        -0.25f, 0.25f,
        0.0f, 0.0f,
    };
    std::vector<float> x = in;

    bq.Process(x.data(), 4, 2);

    for (size_t i = 0; i < in.size(); ++i)
    {
        if (std::fabs(in[i] - x[i]) > 1e-6f) return false;
    }

    return true;
}

bool TestBiquadStability()
{
    Biquad bq;
    bq.Reset(2);
    bq.SetHighPass(48000.0f, 90.0f, 0.707f);

    std::vector<float> x(512 * 2, 0.0f);
    for (size_t i = 0; i < x.size(); i += 2)
    {
        x[i] = 0.2f;
        x[i + 1] = -0.2f;
    }

    bq.Process(x.data(), 512, 2);

    for (float v : x)
    {
        if (!IsFinite(v)) return false;
        if (std::fabs(v) > 2.0f) return false;
    }

    return true;
}
