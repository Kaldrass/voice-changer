// src/core/RingBuffer.cpp
#include "core/RingBuffer.h"
#include <algorithm>
#include <stdexcept>

static bool IsPowerOfTwo(size_t x) { return x && ((x & (x - 1)) == 0); }

RingBufferF32::RingBufferF32(size_t capacityFrames, int channels)
    : m_capacityFrames(capacityFrames), m_channels(channels)
{
    // Capacity must be power of two for mask indexing.
    if (!IsPowerOfTwo(capacityFrames))
        throw std::runtime_error("RingBuffer capacityFrames must be power of two");

    m_data.resize(m_capacityFrames * static_cast<size_t>(m_channels), 0.0f);
}

size_t RingBufferF32::AvailableToRead() const noexcept
{
    const size_t w = m_writeIndex.load(std::memory_order_acquire);
    const size_t r = m_readIndex.load(std::memory_order_acquire);
    return w - r;
}

size_t RingBufferF32::AvailableToWrite() const noexcept
{
    return m_capacityFrames - AvailableToRead();
}

size_t RingBufferF32::Push(const float* in, size_t frames) noexcept
{
    size_t canWrite = AvailableToWrite();
    size_t toWrite = std::min(frames, canWrite);

    size_t w = m_writeIndex.load(std::memory_order_relaxed);
    for (size_t i = 0; i < toWrite; ++i)
    {
        size_t frameIndex = (w + i) & IndexMask();
        float* dst = &m_data[frameIndex * static_cast<size_t>(m_channels)];
        const float* src = &in[i * static_cast<size_t>(m_channels)];
        for (int c = 0; c < m_channels; ++c) dst[c] = src[c];
    }

    m_writeIndex.store(w + toWrite, std::memory_order_release);
    return toWrite;
}

size_t RingBufferF32::Pop(float* out, size_t frames) noexcept
{
    size_t canRead = AvailableToRead();
    size_t toRead = std::min(frames, canRead);

    size_t r = m_readIndex.load(std::memory_order_relaxed);
    for (size_t i = 0; i < toRead; ++i)
    {
        size_t frameIndex = (r + i) & IndexMask();
        const float* src = &m_data[frameIndex * static_cast<size_t>(m_channels)];
        float* dst = &out[i * static_cast<size_t>(m_channels)];
        for (int c = 0; c < m_channels; ++c) dst[c] = src[c];
    }

    m_readIndex.store(r + toRead, std::memory_order_release);
    return toRead;
}

size_t RingBufferF32::Drop(size_t frames) noexcept
{
    const size_t canRead = AvailableToRead();
    const size_t toDrop = std::min(frames, canRead);

    const size_t r = m_readIndex.load(std::memory_order_relaxed);
    m_readIndex.store(r + toDrop, std::memory_order_release);
    return toDrop;
}