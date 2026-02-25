// src/audio/WasapiRender.h
#pragma once

#include <atomic>
#include <thread>
#include <vector>
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>

#include "core/AudioTypes.h"
#include "core/AudioStats.h"
#include "core/RingBuffer.h"
#include "dsp/EffectChain.h"

struct WasapiRenderInfo
{
    int sampleRate = 0;
    int channels = 0;
    bool isFloat32 = false;
    UINT32 bufferFrames = 0;
};

class WasapiRender
{
public:
    WasapiRender(IMMDevice* device, RingBufferF32& ringBuffer, AudioStats& stats, EffectChain& chain);
    ~WasapiRender();
    
    
    WasapiRender(const WasapiRender&) = delete;
    WasapiRender& operator=(const WasapiRender&) = delete;
    
    // Initializes the WASAPI render client. Must be called before Start().
    void Initialize();
    
    void Start();
    void Stop();
    
    WasapiRenderInfo GetRenderInfo() const noexcept;
    AudioFormat GetFormat() const noexcept { return m_format; }

    UINT32 GetLastPaddingFrames() const noexcept { return m_lastPaddingFrames.load(std::memory_order_relaxed); }
    UINT32 GetBufferFrames() const noexcept { return m_bufferFrames; }

private:
    void ThreadMain();

    // Writes interleaved float32 samples into the device buffer (float32 or int16 PCM).
    void WriteFromFloatInterleaved(const float* src, UINT32 frames, BYTE* dst, bool deviceIsFloat32);

private:
    IMMDevice* m_device = nullptr;
    IAudioClient* m_audioClient = nullptr;
    IAudioRenderClient* m_renderClient = nullptr;

    WAVEFORMATEX* m_mixFormat = nullptr;
    AudioFormat m_format{};

    HANDLE m_event = nullptr;

    RingBufferF32& m_ringBuffer;
    AudioStats& m_stats;
    EffectChain& m_chain;

    std::atomic<bool> m_running{false};
    std::thread m_thread;

    std::vector<float> m_tmp; // interleaved float buffer
    bool m_deviceIsFloat32 = false;

    UINT32 m_bufferFrames = 0;
    
    std::atomic<UINT32> m_lastPaddingFrames{0};
};

