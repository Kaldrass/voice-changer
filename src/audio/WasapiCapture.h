// src/audio/WasapiCapture.h
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

struct WasapiFormatInfo
{
    int sampleRate = 0;
    int channels = 0;
    bool isFloat32 = false; // else int16 PCM
};

class WasapiCapture
{
    public:
        WasapiCapture(IMMDevice* device, RingBufferF32& ringBuffer, AudioStats& stats);
        ~WasapiCapture();

        WasapiCapture(const WasapiCapture&) = delete;
        WasapiCapture& operator=(const WasapiCapture&) = delete;

        // Initializes the WASAPI capture client. Must be called before Start().
        void Initialize();

        void Start();
        void Stop();

        WasapiFormatInfo GetDeviceFormatInfo() const noexcept;

        AudioFormat GetFormat() const noexcept { return m_format; }

    private:
        void ThreadMain();

        size_t ConvertToFloatInterleaved(const BYTE* src, UINT32 frames, float* dst);

    private:
        IMMDevice* m_device = nullptr;
        IAudioClient* m_audioClient = nullptr;
        IAudioCaptureClient* m_captureClient = nullptr;

        WAVEFORMATEX* m_mixFormat = nullptr;
        AudioFormat m_format{};

        HANDLE m_event = nullptr;

        RingBufferF32& m_ringBuffer;
        AudioStats& m_stats;

        std::atomic<bool> m_running{false};
        std::thread m_thread;

        std::vector<float> m_deviceTmp; // float buffer in device channel count
        std::vector<float> m_tmp; // interleaved float buffer
        
        bool m_deviceIsFloat32 = false;

        int m_deviceChannels = 0;     // actual device channels
        int m_outChannels = 2;        // internal channels (fixed to 2)

};

