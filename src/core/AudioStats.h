// src/core/AudioStats.h
#pragma once
#include <atomic>
#include <cstdint>

struct AudioStats
{
    std::atomic<uint64_t> captureFrames{0};
    std::atomic<uint64_t> renderFrames{0};
    std::atomic<uint32_t> xruns{0};
};