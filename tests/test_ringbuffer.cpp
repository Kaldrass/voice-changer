#include "core/RingBuffer.h"

#include <cmath>
#include <vector>

static bool NearlyEqual(float a, float b)
{
    return std::fabs(a - b) < 1e-6f;
}

bool TestRingBufferBasic()
{
    RingBufferF32 rb(8, 2);

    std::vector<float> in = {
        0.1f, -0.1f,
        0.2f, -0.2f,
        0.3f, -0.3f,
        0.4f, -0.4f,
    };

    const size_t pushed = rb.Push(in.data(), 4);
    if (pushed != 4) return false;
    if (rb.AvailableToRead() != 4) return false;

    std::vector<float> out(8, 0.0f);
    const size_t popped = rb.Pop(out.data(), 4);
    if (popped != 4) return false;

    for (size_t i = 0; i < in.size(); ++i)
    {
        if (!NearlyEqual(in[i], out[i])) return false;
    }

    return rb.AvailableToRead() == 0;
}

bool TestRingBufferOverflowAndDrop()
{
    RingBufferF32 rb(8, 2);

    std::vector<float> in(20 * 2, 0.0f);
    for (size_t i = 0; i < in.size(); ++i) in[i] = static_cast<float>(i);

    const size_t pushed = rb.Push(in.data(), 20);
    if (pushed != 8) return false;

    const size_t dropped = rb.Drop(3);
    if (dropped != 3) return false;
    if (rb.AvailableToRead() != 5) return false;

    std::vector<float> out(5 * 2, 0.0f);
    const size_t popped = rb.Pop(out.data(), 5);
    if (popped != 5) return false;

    const size_t expectedStartFrame = 3;
    for (size_t f = 0; f < 5; ++f)
    {
        const size_t srcFrame = expectedStartFrame + f;
        const float l = static_cast<float>(srcFrame * 2 + 0);
        const float r = static_cast<float>(srcFrame * 2 + 1);
        if (!NearlyEqual(out[f * 2 + 0], l)) return false;
        if (!NearlyEqual(out[f * 2 + 1], r)) return false;
    }

    return true;
}
