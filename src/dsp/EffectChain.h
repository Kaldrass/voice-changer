// src/dsp/EffectChain.h
#pragma once
#include "dsp/IEffect.h"
#include <vector>
#include <memory>

class EffectChain
{
public:
    void Add(std::unique_ptr<IEffect> fx) { m_fx.emplace_back(std::move(fx)); }

    void Process(float* interleaved, size_t frames, int channels) noexcept
    {
        for (auto& fx : m_fx) fx->Process(interleaved, frames, channels);
    }

private:
    std::vector<std::unique_ptr<IEffect>> m_fx;
};