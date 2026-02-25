// src/audio/WasapiRender.cpp
#include "audio/WasapiRender.h"

#include <avrt.h>
#include <functiondiscoverykeys_devpkey.h>
#include <stdexcept>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <iomanip>

// Link with Avrt.lib via CMake target_link_libraries(... avrt ...)

static void ThrowIfFailed(HRESULT hr, const char* msg)
{
    if (FAILED(hr))
    {
        std::ostringstream oss;
        oss << msg << " (hr=0x" << std::hex << std::setw(8) << std::setfill('0')
            << static_cast<unsigned long>(hr) << ")";
        throw std::runtime_error(oss.str());
    }
}

static bool IsDeviceFloat32(const WAVEFORMATEX* wf)
{
    const bool isExt = (wf->wFormatTag == WAVE_FORMAT_EXTENSIBLE);

    if (!isExt)
        return (wf->wFormatTag == WAVE_FORMAT_IEEE_FLOAT && wf->wBitsPerSample == 32);

    auto wfe = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(wf);
    return IsEqualGUID(wfe->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) && wf->wBitsPerSample == 32;
}

static bool IsDeviceInt16Pcm(const WAVEFORMATEX* wf)
{
    const bool isExt = (wf->wFormatTag == WAVE_FORMAT_EXTENSIBLE);

    if (!isExt)
        return (wf->wFormatTag == WAVE_FORMAT_PCM && wf->wBitsPerSample == 16);

    auto wfe = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(wf);
    return IsEqualGUID(wfe->SubFormat, KSDATAFORMAT_SUBTYPE_PCM) && wf->wBitsPerSample == 16;
}

WasapiRender::WasapiRender(IMMDevice* device, RingBufferF32& ringBuffer, AudioStats& stats, EffectChain& chain)
    : m_ringBuffer(ringBuffer), m_stats(stats), m_chain(chain)
{
    if (!device) throw std::runtime_error("WasapiRender: device is null");
    m_device = device;
    m_device->AddRef();
}

WasapiRender::~WasapiRender()
{
    Stop();

    if (m_renderClient) { m_renderClient->Release(); m_renderClient = nullptr; }
    if (m_audioClient) { m_audioClient->Release(); m_audioClient = nullptr; }
    if (m_mixFormat) { CoTaskMemFree(m_mixFormat); m_mixFormat = nullptr; }
    if (m_event) { CloseHandle(m_event); m_event = nullptr; }
    if (m_device) { m_device->Release(); m_device = nullptr; }
}

void WasapiRender::Initialize()
{
    if (m_audioClient) return;

    HRESULT hr = m_device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&m_audioClient);
    ThrowIfFailed(hr, "WasapiRender: IMMDevice::Activate(IAudioClient) failed");

    hr = m_audioClient->GetMixFormat(&m_mixFormat);
    ThrowIfFailed(hr, "WasapiRender: IAudioClient::GetMixFormat failed");
    
    if (m_mixFormat->nChannels <= 0 || m_mixFormat->nSamplesPerSec <= 0)
        throw std::runtime_error("WasapiRender: invalid mix format");

    m_format.channels = static_cast<int>(m_mixFormat->nChannels);
    m_format.sampleRate = static_cast<int>(m_mixFormat->nSamplesPerSec);

    m_deviceIsFloat32 = IsDeviceFloat32(m_mixFormat);

    if (!m_deviceIsFloat32 && !IsDeviceInt16Pcm(m_mixFormat))
        throw std::runtime_error("WasapiRender: unsupported render format (only float32 or int16 PCM)");

    // Event-driven shared mode render.
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
    ThrowIfFailed(hr, "WasapiRender: IAudioClient::Initialize failed");

    m_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!m_event) throw std::runtime_error("WasapiRender: CreateEvent failed");

    hr = m_audioClient->SetEventHandle(m_event);
    ThrowIfFailed(hr, "WasapiRender: IAudioClient::SetEventHandle failed");

    hr = m_audioClient->GetService(__uuidof(IAudioRenderClient), (void**)&m_renderClient);
    ThrowIfFailed(hr, "WasapiRender: IAudioClient::GetService(IAudioRenderClient) failed");

    // Pre-allocate a temp buffer for one callback worth of audio.
    m_tmp.resize(4096u * static_cast<size_t>(m_format.channels), 0.0f);
}

void WasapiRender::Start()
{
    if (!m_audioClient) throw std::runtime_error("WasapiRender: Initialize() must be called before Start()");
    if (m_running.load(std::memory_order_acquire)) return;

    // Pre-roll: fill initial device buffer with silence to avoid garbage.
    UINT32 bufferFrames = 0;
    HRESULT hr = m_audioClient->GetBufferSize(&bufferFrames);
    ThrowIfFailed(hr, "WasapiRender: IAudioClient::GetBufferSize failed");
    
    m_bufferFrames = bufferFrames;

    BYTE* dst = nullptr;
    hr = m_renderClient->GetBuffer(bufferFrames, &dst);
    ThrowIfFailed(hr, "WasapiRender: IAudioRenderClient::GetBuffer (pre-roll) failed");

    std::fill(m_tmp.begin(), m_tmp.begin() + (static_cast<size_t>(bufferFrames) * static_cast<size_t>(m_format.channels)), 0.0f);
    WriteFromFloatInterleaved(m_tmp.data(), bufferFrames, dst, m_deviceIsFloat32);

    hr = m_renderClient->ReleaseBuffer(bufferFrames, 0);
    ThrowIfFailed(hr, "WasapiRender: IAudioRenderClient::ReleaseBuffer (pre-roll) failed");

    hr = m_audioClient->Start();
    ThrowIfFailed(hr, "WasapiRender: IAudioClient::Start failed");

    m_running.store(true, std::memory_order_release);
    m_thread = std::thread(&WasapiRender::ThreadMain, this);
}

void WasapiRender::Stop()
{
    if (!m_running.exchange(false, std::memory_order_acq_rel)) return;

    if (m_event) SetEvent(m_event);
    if (m_thread.joinable()) m_thread.join();

    if (m_audioClient) m_audioClient->Stop();
}

WasapiRenderInfo WasapiRender::GetRenderInfo() const noexcept
{
    WasapiRenderInfo info;
    info.sampleRate = m_format.sampleRate;
    info.channels = m_format.channels;
    info.isFloat32 = m_deviceIsFloat32;
    info.bufferFrames = m_bufferFrames;
    return info;
}

void WasapiRender::WriteFromFloatInterleaved(const float* src, UINT32 frames, BYTE* dst, bool deviceIsFloat32)
{
    const int ch = m_format.channels;
    const size_t n = static_cast<size_t>(frames) * static_cast<size_t>(ch);

    if (deviceIsFloat32)
    {
        float* out = reinterpret_cast<float*>(dst);
        std::copy(src, src + n, out);
        return;
    }

    // int16 PCM
    int16_t* out = reinterpret_cast<int16_t*>(dst);
    for (size_t i = 0; i < n; ++i)
    {
        float x = src[i];
        if (x > 1.0f) x = 1.0f;
        if (x < -1.0f) x = -1.0f;

        // Scale to int16. Using 32767 for positive range.
        const int v = static_cast<int>(std::lrintf(x * 32767.0f));
        out[i] = static_cast<int16_t>(v);
    }
}

void WasapiRender::ThreadMain()
{
    DWORD avrtTaskIndex = 0;
    HANDLE avrtHandle = AvSetMmThreadCharacteristicsW(L"Pro Audio", &avrtTaskIndex);

    UINT32 bufferFrames = 0;
    HRESULT hr = m_audioClient->GetBufferSize(&bufferFrames);
    if (FAILED(hr))
    {
        m_stats.xruns.fetch_add(1, std::memory_order_relaxed);
        if (avrtHandle) AvRevertMmThreadCharacteristics(avrtHandle);
        return;
    }

    while (m_running.load(std::memory_order_acquire))
    {
        DWORD waitRes = WaitForSingleObject(m_event, 2000);
        if (!m_running.load(std::memory_order_acquire)) break;
        if (waitRes != WAIT_OBJECT_0) continue;

        UINT32 paddingFrames = 0;
        hr = m_audioClient->GetCurrentPadding(&paddingFrames);
        if (FAILED(hr))
        {
            m_stats.xruns.fetch_add(1, std::memory_order_relaxed);
            continue;
        }

        UINT32 availFrames = bufferFrames - paddingFrames;
        if (availFrames == 0) continue;

        const size_t needed = static_cast<size_t>(availFrames) * static_cast<size_t>(m_format.channels);
        if (m_tmp.size() < needed) m_tmp.resize(needed);

        // Pop from ring buffer; if insufficient, fill remainder with silence.
        size_t gotFrames = m_ringBuffer.Pop(m_tmp.data(), static_cast<size_t>(availFrames));
        if (gotFrames < static_cast<size_t>(availFrames))
        {
            const size_t gotSamples = gotFrames * static_cast<size_t>(m_format.channels);
            std::fill(m_tmp.begin() + gotSamples, m_tmp.begin() + needed, 0.0f);
            m_stats.xruns.fetch_add(1, std::memory_order_relaxed);
        }

        // Apply effect chain in-place (float32).
        m_chain.Process(m_tmp.data(), static_cast<size_t>(availFrames), m_format.channels);

        BYTE* dst = nullptr;
        hr = m_renderClient->GetBuffer(availFrames, &dst);
        if (FAILED(hr))
        {
            m_stats.xruns.fetch_add(1, std::memory_order_relaxed);
            continue;
        }

        WriteFromFloatInterleaved(m_tmp.data(), availFrames, dst, m_deviceIsFloat32);

        hr = m_renderClient->ReleaseBuffer(availFrames, 0);
        if (FAILED(hr))
        {
            m_stats.xruns.fetch_add(1, std::memory_order_relaxed);
            continue;
        }

        m_stats.renderFrames.fetch_add(availFrames, std::memory_order_relaxed);
    }

    if (avrtHandle) AvRevertMmThreadCharacteristics(avrtHandle);
}