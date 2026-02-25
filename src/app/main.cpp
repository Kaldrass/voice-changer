// src/app/main.cpp
#include <iostream>
#include <string>
#include <vector>
#include <windows.h>

#include "audio/DeviceUtils.h"
#include "audio/WasapiCapture.h"
#include "audio/WasapiRender.h"
#include "core/AudioStats.h"
#include "core/RingBuffer.h"
#include "dsp/EffectChain.h"
#include "dsp/GainEffect.h"
#include "dsp/PitchShiftEffect.h"
#include "dsp/SoftClipperEffect.h"

static void PrintDevices(const std::vector<AudioDeviceInfo>& devs, const char* title)
{
    std::wcout << L"\n== " << title << L" ==\n";
    for (size_t i = 0; i < devs.size(); ++i)
    {
        std::wcout << L"[" << i << L"] " << devs[i].name << L"\n";
    }
}

static std::wstring ArgToWString(const char* s)
{
    // Minimal ASCII/ANSI conversion; upgrade later to CommandLineToArgvW for UTF-8 safety.
    return std::wstring(s, s + strlen(s));
}

int main(int argc, char** argv)
{
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr))
    {
        std::cerr << "CoInitializeEx failed\n";
        return 1;
    }

    try
    {
        bool list = false;
        std::wstring inSub, outSub;
        int inIndex = -1, outIndex = -1;
        float gainValue = 1.0f;
        float clipDrive = 2.0f;
        float clipOutGain = 1.0f;
        float pitchSemis = 0.0f;
        std::cout << "argc=" << argc << "\n";
        for (int i = 1; i < argc; ++i)
        {
            // std::cout << "arg[" << i << "]=" << argv[i] << "\n";
            std::string a = argv[i];
            if (a == "--list-devices") list = true;
            else if (a == "--in" && i + 1 < argc) inSub = ArgToWString(argv[++i]);
            else if (a == "--out" && i + 1 < argc) outSub = ArgToWString(argv[++i]);
            else if (a == "--in-index" && i + 1 < argc) inIndex = std::stoi(argv[++i]);
            else if (a == "--out-index" && i + 1 < argc) outIndex = std::stoi(argv[++i]);
            else if (a == "--gain" && i + 1 < argc) gainValue = std::stof(argv[++i]);
            else if (a == "--clip-drive" && i + 1 < argc) clipDrive = std::stof(argv[++i]);
            else if (a == "--clip-out" && i + 1 < argc) clipOutGain = std::stof(argv[++i]);
            else if (a == "--pitch-semitones" && i + 1 < argc) pitchSemis = std::stof(argv[++i]);
            else if (a == "--help")
            {
                std::cout << "Usage: voice_changer [options]\n"
                          << "Options:\n"
                          << "  --list-devices           List audio devices and exit\n"
                          << "  --in <substring>         Select input device by substring match\n"
                          << "  --out <substring>        Select output device by substring match\n"
                          << "  --in-index <index>       Select input device by index (from --list-devices)\n"
                          << "  --out-index <index>      Select output device by index (from --list-devices)\n"
                          << "  --gain <float>          Set gain effect value (default 1.0)\n"
                          << "  --clip-drive <float>    Set soft clipper drive (default 2.0)\n"
                          << "  --clip-out <float>      Set soft clipper output gain (default 1.0)\n"
                          << "  --pitch-semitones <float> Set pitch shift in semitones (default 0.0)\n";
                CoUninitialize();
                return 0;
            }
            else
            {
                std::cerr << "Unknown argument: " << a << "\n";
                return 1;
            }
        }

        auto ins = EnumerateDevices(AudioFlow::Capture);
        auto outs = EnumerateDevices(AudioFlow::Render);

        if (list)
        {
            PrintDevices(ins, "CAPTURE DEVICES");
            PrintDevices(outs, "RENDER DEVICES");
            CoUninitialize();
            return 0;
        }

        if (inIndex < 0 && !inSub.empty()) inIndex = FindDeviceIndexBySubstring(ins, inSub);
        if (outIndex < 0 && !outSub.empty()) outIndex = FindDeviceIndexBySubstring(outs, outSub);

        if (inIndex < 0 || inIndex >= static_cast<int>(ins.size()))
            throw std::runtime_error("Invalid input device. Use --list-devices then --in/--in-index.");
        if (outIndex < 0 || outIndex >= static_cast<int>(outs.size()))
            throw std::runtime_error("Invalid output device. Use --list-devices then --out/--out-index.");

        std::wcout << L"Selected input : [" << inIndex << L"] " << ins[inIndex].name << L"\n";
        std::wcout << L"Selected output: [" << outIndex << L"] " << outs[outIndex].name << L"\n";

        IMMDevice* inDev = GetDeviceById(ins[inIndex].id);
        IMMDevice* outDev = GetDeviceById(outs[outIndex].id);

        AudioStats stats;
        
        // Internal format: stereo ring buffer
        RingBufferF32 rb(16384, 2);
        
        WasapiCapture cap(inDev, rb, stats);
        cap.Initialize();
        
        auto ci = cap.GetDeviceFormatInfo();
        std::cout << "CAPTURE: " << ci.sampleRate << " Hz, " << ci.channels
                  << " ch, " << (ci.isFloat32 ? "float32" : "int16") << "\n";
        
        EffectChain chain;
        
        auto gain = std::make_unique<GainEffect>();
        gain->SetGain(gainValue);
        chain.Add(std::move(gain));
        
        auto pitch = std::make_unique<PitchShiftEffect>(ci.sampleRate, 2);
        pitch->SetPitchSemiTones(pitchSemis);
        chain.Add(std::move(pitch));
        
        auto clip = std::make_unique<SoftClipperEffect>();
        clip->SetDrive(clipDrive);
        clip->SetOutputGain(clipOutGain);
        chain.Add(std::move(clip));
        
        WasapiRender ren(outDev, rb, stats, chain);
        ren.Initialize();
        
        auto ri = ren.GetRenderInfo();
        std::cout << "RENDER : " << ri.sampleRate << " Hz, " << ri.channels
                  << " ch, " << (ri.isFloat32 ? "float32" : "int16")
                  << ", bufferFrames=" << ri.bufferFrames << "\n";
        
        if (ci.channels != ri.channels)
            std::cout << "WARNING: channel mismatch (capture " << ci.channels << " vs render " << ri.channels << ")\n";
        if (ci.sampleRate != ri.sampleRate)
            std::cout << "WARNING: sample rate mismatch (capture " << ci.sampleRate << " vs render " << ri.sampleRate << ")\n";
        
        cap.Start();
        
        const uint32_t warmupTargetFrames = 1024;
        for (int i = 0; i < 200; ++i)
        {
            if (rb.AvailableToRead() >= warmupTargetFrames) break;
            Sleep(1);
        }
        
        ren.Start();
        
        auto ri2 = ren.GetRenderInfo();
        std::cout << "RENDER (after start): bufferFrames=" << ri2.bufferFrames << "\n";

        std::cout << "Running. Press Ctrl+C to exit.\n";

        while (true)
        {
            Sleep(500);
            const auto capFrames = stats.captureFrames.load();
            const auto renFrames = stats.renderFrames.load();
            const auto xr = stats.xruns.load();
            std::cout << "captureFrames=" << capFrames << " renderFrames=" << renFrames << " xruns=" << xr << "\n";

            const auto rbFrames = static_cast<uint32_t>(rb.AvailableToRead());
            const auto padding = ren.GetLastPaddingFrames();
            const int sr = ci.sampleRate; // capture sample rate (tu as ci)
            
            double latMs = 0.0;
            if (sr > 0)
                latMs = (static_cast<double>(rbFrames + padding) * 1000.0) / static_cast<double>(sr);
            
            std::cout << "rbFrames=" << rbFrames
                      << " padding=" << padding
                      << " approxOutLatencyMs=" << latMs
                      << "\n";
        }

        ren.Stop();
        cap.Stop();

        inDev->Release();
        outDev->Release();
    }
    catch (const std::exception& e)
    {
        std::cerr << "Fatal error: " << e.what() << "\n";
        CoUninitialize();
        return 1;
    }

    CoUninitialize();
    return 0;
}