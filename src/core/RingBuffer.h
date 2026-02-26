// src/core/RingBuffer.h
#pragma once
#include <vector>
#include <atomic>
#include <cstddef>
#include <cstdint>

class RingBufferF32
{
public:
    explicit RingBufferF32(size_t capacityFrames, int channels);

    size_t CapacityFrames() const noexcept { return m_capacityFrames; }
    int Channels() const noexcept { return m_channels; }

    size_t Push(const float* interleaved, size_t frames) noexcept;
    size_t Pop(float* interleaved, size_t frames) noexcept;
    size_t Drop(size_t frames) noexcept;

    size_t AvailableToRead() const noexcept;
    size_t AvailableToWrite() const noexcept;

private:
    size_t IndexMask() const noexcept { return m_capacityFrames - 1; }

    std::vector<float> m_data;
    size_t m_capacityFrames = 0;
    int m_channels = 0;

    std::atomic<size_t> m_writeIndex{0};
    std::atomic<size_t> m_readIndex{0};
};