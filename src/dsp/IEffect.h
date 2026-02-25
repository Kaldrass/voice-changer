// src/dsp/IEffect.h
#pragma once
#include <cstddef>

class IEffect
{
public:
    virtual ~IEffect() = default;
    virtual void Process(float* interleaved, size_t frames, int channels) noexcept = 0;
};