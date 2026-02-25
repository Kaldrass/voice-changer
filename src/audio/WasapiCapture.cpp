// src/audio/WasapiCapture.cpp
#include "audio/WasapiCapture.h"

#include <mmreg.h>
#include <ks.h>
#include <ksmedia.h>
#include <avrt.h>
#include <stdexcept>
#include <algorithm>
#include <cmath>

// Link with Avrt.lib via CMake target_link_libraries(... avrt ...)

static void ThrowIfFailed(HRESULT hr, const char* msg)
{
    if (FAILED(hr)) throw std::runtime_error(msg);
}

static bool IsDeviceFloat32(const WAVEFORMATEX* wf)
{
    const bool isExt = (wf->wFormatTag == WAVE_FORMAT_EXTENSIBLE);

    if (!isExt)
        return (wf->wFormatTag == WAVE_FORMAT_IEEE_FLOAT && wf->wBitsPerSample == 32);

    auto wfe = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(wf);
    return IsEqualGUID(wfe->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) && wf->wBitsPerSample == 32;
}

WasapiCapture::WasapiCapture(IMMDevice* device, RingBufferF32& ringBuffer, AudioStats& stats)
    : m_ringBuffer(ringBuffer), m_stats(stats)
{
    if (!device) throw std::runtime_error("WasapiCapture: device is null");
    m_device = device;
    m_device->AddRef();
}

WasapiCapture::~WasapiCapture()
{
    Stop();

    if (m_captureClient) { m_captureClient->Release(); m_captureClient = nullptr; }
    if (m_audioClient) { m_audioClient->Release(); m_audioClient = nullptr; }
    if (m_mixFormat) { CoTaskMemFree(m_mixFormat); m_mixFormat = nullptr; }
    if (m_event) { CloseHandle(m_event); m_event = nullptr; }
    if (m_device) { m_device->Release(); m_device = nullptr; }
}

void WasapiCapture::Initialize()
{
    if (m_audioClient) return;

    HRESULT hr = m_device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&m_audioClient);
    ThrowIfFailed(hr, "WasapiCapture: IMMDevice::Activate(IAudioClient) failed");

    hr = m_audioClient->GetMixFormat(&m_mixFormat);
    ThrowIfFailed(hr, "WasapiCapture: IAudioClient::GetMixFormat failed");

    if (m_mixFormat->nChannels <= 0 || m_mixFormat->nSamplesPerSec <= 0)
        throw std::runtime_error("WasapiCapture: invalid mix format");

    m_format.channels = static_cast<int>(m_mixFormat->nChannels);
    m_format.sampleRate = static_cast<int>(m_mixFormat->nSamplesPerSec);

    m_deviceChannels = m_format.channels;

    // Event-driven shared mode capture.
    // Let Windows choose the buffer duration; use 0 for periodicity in shared mode.
    const REFERENCE_TIME bufferDuration100ns = 0;

    DWORD streamFlags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK;

    hr = m_audioClient->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        streamFlags,
        bufferDuration100ns,
        0,
        m_mixFormat,
        nullptr
    );
    ThrowIfFailed(hr, "WasapiCapture: IAudioClient::Initialize failed");

    m_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!m_event) throw std::runtime_error("WasapiCapture: CreateEvent failed");

    hr = m_audioClient->SetEventHandle(m_event);
    ThrowIfFailed(hr, "WasapiCapture: IAudioClient::SetEventHandle failed");

    hr = m_audioClient->GetService(__uuidof(IAudioCaptureClient), (void**)&m_captureClient);
    ThrowIfFailed(hr, "WasapiCapture: IAudioClient::GetService(IAudioCaptureClient) failed");

    m_deviceIsFloat32 = IsDeviceFloat32(m_mixFormat);

    // Pre-allocate a temp buffer for one packet.
    // Packet sizes can vary; this will grow dynamically if needed.
    m_tmp.resize(4096u * static_cast<size_t>(m_format.channels), 0.0f);
}

void WasapiCapture::Start()
{
    if (!m_audioClient) throw std::runtime_error("WasapiCapture: Initialize() must be called before Start()");
    if (m_running.load(std::memory_order_acquire)) return;

    HRESULT hr = m_audioClient->Start();
    ThrowIfFailed(hr, "WasapiCapture: IAudioClient::Start failed");

    m_running.store(true, std::memory_order_release);
    m_thread = std::thread(&WasapiCapture::ThreadMain, this);
}

void WasapiCapture::Stop()
{
    if (!m_running.exchange(false, std::memory_order_acq_rel)) return;

    if (m_event) SetEvent(m_event);
    if (m_thread.joinable()) m_thread.join();

    if (m_audioClient) m_audioClient->Stop();
}

static void UpmixToStereo(const float* in, UINT32 frames, int inCh, float* out)
{
    // out is always 2 channels interleaved
    if (inCh <= 0) return;

    if (inCh == 1)
    {
        for (UINT32 i = 0; i < frames; ++i)
        {
            float x = in[i];
            out[2u * i + 0] = x;
            out[2u * i + 1] = x;
        }
        return;
    }

    // inCh >= 2 : take first two channels
    for (UINT32 i = 0; i < frames; ++i)
    {
        out[2u * i + 0] = in[static_cast<size_t>(i) * inCh + 0];
        out[2u * i + 1] = in[static_cast<size_t>(i) * inCh + 1];
    }
}

size_t WasapiCapture::ConvertToFloatInterleaved(const BYTE* src, UINT32 frames, float* dst)
{
    // dst is device-channels interleaved float
    const int ch = m_deviceChannels;
    const size_t n = static_cast<size_t>(frames) * static_cast<size_t>(ch);

    const WAVEFORMATEX* wf = m_mixFormat;
    const bool isExt = (wf->wFormatTag == WAVE_FORMAT_EXTENSIBLE);

    WORD formatTag = wf->wFormatTag;
    WORD bitsPerSample = wf->wBitsPerSample;

    if (isExt)
    {
        auto wfe = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(wf);
        if (IsEqualGUID(wfe->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT))
        {
            formatTag = WAVE_FORMAT_IEEE_FLOAT;
            bitsPerSample = wf->wBitsPerSample;
        }
        else if (IsEqualGUID(wfe->SubFormat, KSDATAFORMAT_SUBTYPE_PCM))
        {
            formatTag = WAVE_FORMAT_PCM;
            bitsPerSample = wf->wBitsPerSample;
        }
    }

    if (formatTag == WAVE_FORMAT_IEEE_FLOAT && bitsPerSample == 32)
    {
        const float* f = reinterpret_cast<const float*>(src);
        std::copy(f, f + n, dst);
        return frames;
    }

    if (formatTag == WAVE_FORMAT_PCM && bitsPerSample == 16)
    {
        const int16_t* s = reinterpret_cast<const int16_t*>(src);
        for (size_t i = 0; i < n; ++i) dst[i] = static_cast<float>(s[i]) / 32768.0f;
        return frames;
    }

    throw std::runtime_error("WasapiCapture: unsupported capture format (only float32 or int16 PCM)");
}

WasapiFormatInfo WasapiCapture::GetDeviceFormatInfo() const noexcept
{
    WasapiFormatInfo info;
    info.sampleRate = m_format.sampleRate;
    info.channels = m_format.channels;
    info.isFloat32 = m_deviceIsFloat32;
    return info;
}

void WasapiCapture::ThreadMain()
{
    DWORD avrtTaskIndex = 0;
    HANDLE avrtHandle = AvSetMmThreadCharacteristicsW(L"Pro Audio", &avrtTaskIndex);

    while (m_running.load(std::memory_order_acquire))
    {
        DWORD waitRes = WaitForSingleObject(m_event, 2000);
        if (!m_running.load(std::memory_order_acquire)) break;
        if (waitRes != WAIT_OBJECT_0) continue;

        UINT32 packetFrames = 0;
        HRESULT hr = m_captureClient->GetNextPacketSize(&packetFrames);
        if (FAILED(hr))
        {
            m_stats.xruns.fetch_add(1, std::memory_order_relaxed);
            continue;
        }

        while (packetFrames > 0 && m_running.load(std::memory_order_acquire))
        {
            BYTE* data = nullptr;
            UINT32 numFrames = 0;
            DWORD flags = 0;

            hr = m_captureClient->GetBuffer(&data, &numFrames, &flags, nullptr, nullptr);
            if (FAILED(hr))
            {
                m_stats.xruns.fetch_add(1, std::memory_order_relaxed);
                break;
            }

            const size_t deviceNeeded = static_cast<size_t>(numFrames) * static_cast<size_t>(m_deviceChannels);
            if (m_deviceTmp.size() < deviceNeeded) m_deviceTmp.resize(deviceNeeded);
            
            const size_t stereoNeeded = static_cast<size_t>(numFrames) * 2u;
            if (m_tmp.size() < stereoNeeded) m_tmp.resize(stereoNeeded);
            
            if (flags & AUDCLNT_BUFFERFLAGS_SILENT)
            {
                std::fill(m_tmp.begin(), m_tmp.begin() + stereoNeeded, 0.0f);
            }
            else
            {
                try
                {
                    ConvertToFloatInterleaved(data, numFrames, m_deviceTmp.data()); // device channels
                    UpmixToStereo(m_deviceTmp.data(), numFrames, m_deviceChannels, m_tmp.data()); // stereo
                }
                catch (...)
                {
                    std::fill(m_tmp.begin(), m_tmp.begin() + stereoNeeded, 0.0f);
                    m_stats.xruns.fetch_add(1, std::memory_order_relaxed);
                }
            }
            
            // Push stereo frames into ring buffer (which is 2ch)
            size_t pushed = m_ringBuffer.Push(m_tmp.data(), static_cast<size_t>(numFrames));
            if (pushed < static_cast<size_t>(numFrames))
            {
                m_stats.xruns.fetch_add(1, std::memory_order_relaxed);
            }

            m_stats.captureFrames.fetch_add(numFrames, std::memory_order_relaxed);

            hr = m_captureClient->ReleaseBuffer(numFrames);
            if (FAILED(hr))
            {
                m_stats.xruns.fetch_add(1, std::memory_order_relaxed);
                break;
            }

            hr = m_captureClient->GetNextPacketSize(&packetFrames);
            if (FAILED(hr))
            {
                m_stats.xruns.fetch_add(1, std::memory_order_relaxed);
                break;
            }
        }
    }

    if (avrtHandle) AvRevertMmThreadCharacteristics(avrtHandle);
}