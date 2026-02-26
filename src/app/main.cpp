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
#include "dsp/EQEffect.h"

#include "app/Preset.h"

static void PrintDevices(const std::vector<AudioDeviceInfo>& devs, const char* title)
{
    std::wcout << L"\n== " << title << L" ==\n";
    for (size_t i = 0; i < devs.size(); ++i)
        std::wcout << L"[" << i << L"] " << devs[i].name << L"\n";
}

static std::wstring ArgToWString(const char* s)
{
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

        int hpHz = 90;
        float hpQ = 0.707f;
        int pkHz = 3000;
        float pkQ = 1.0f;
        float pkDB = 2.0f;
        int lpHz = 12000;
        float lpQ = 0.707f;

        uint8_t stSeqMs = 20;
        uint8_t stSeekMs = 8;
        uint8_t stOverlapMs = 4;

        PresetType preset = PresetType::None;

        std::cout << "argc=" << argc << "\n";
        for (int i = 1; i < argc; ++i)
        {
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
            else if (a == "--st-seq-ms" && i + 1 < argc) stSeqMs = static_cast<uint8_t>(std::stoi(argv[++i]));
            else if (a == "--st-seek-ms" && i + 1 < argc) stSeekMs = static_cast<uint8_t>(std::stoi(argv[++i]));
            else if (a == "--st-overlap-ms" && i + 1 < argc) stOverlapMs = static_cast<uint8_t>(std::stoi(argv[++i]));
            else if (a == "--preset" && i + 1 < argc) preset = ParsePreset(argv[++i]);
            else if (a == "--help")
            {
                std::cout
                    << "Usage: voice_changer [options]\n"
                    << "Options:\n"
                    << "  --list-devices              List audio devices and exit\n"
                    << "  --in <substring>            Select input device by substring match\n"
                    << "  --out <substring>           Select output device by substring match\n"
                    << "  --in-index <index>          Select input device by index\n"
                    << "  --out-index <index>         Select output device by index\n"
                    << "  --gain <float>              Gain value\n"
                    << "  --clip-drive <float>        Soft clip drive\n"
                    << "  --clip-out <float>          Soft clip output gain\n"
                    << "  --pitch-semitones <float>   Pitch shift in semitones\n"
                    << "  --st-seq-ms <int>           SoundTouch sequence ms\n"
                    << "  --st-seek-ms <int>          SoundTouch seek window ms\n"
                    << "  --st-overlap-ms <int>       SoundTouch overlap ms\n"
                    << "  --preset <name>             girl, demon, robot, radio\n";
                CoUninitialize();
                return 0;
            }
            else
            {
                std::cerr << "Unknown argument: " << a << "\n";
                CoUninitialize();
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

        // Stereo internal format
        RingBufferF32 rb(2048, 2);

        WasapiCapture cap(inDev, rb, stats);
        cap.Initialize();

        auto ci = cap.GetDeviceFormatInfo();
        std::cout << "CAPTURE: " << ci.sampleRate << " Hz, " << ci.channels
                  << " ch, " << (ci.isFloat32 ? "float32" : "int16") << "\n";

        // Presets v2 (EQ + pitch + gain)
        switch (preset)
        {
        case PresetType::Girl:
            pitchSemis = 5.0f;
            gainValue = 1.5f;
            hpHz = 100; hpQ = 0.707f;
            pkHz = 3200; pkQ = 1.0f; pkDB = 3.0f;
            lpHz = 12000; lpQ = 0.707f;
            break;

        case PresetType::Demon:
            pitchSemis = -6.0f;
            gainValue = 2.5f;
            hpHz = 70; hpQ = 0.707f;
            pkHz = 2500; pkQ = 1.0f; pkDB = 2.0f;
            lpHz = 10000; lpQ = 0.707f;
            break;

        case PresetType::Robot:
            pitchSemis = 2.0f;
            gainValue = 2.2f;
            hpHz = 180; hpQ = 0.707f;
            pkHz = 3800; pkQ = 1.2f; pkDB = 4.0f;
            lpHz = 14000; lpQ = 0.707f;
            break;

        case PresetType::Radio:
            pitchSemis = 0.0f;
            gainValue = 2.0f;
            hpHz = 150; hpQ = 0.707f;
            pkHz = 3000; pkQ = 1.0f; pkDB = 4.0f;
            lpHz = 4500; lpQ = 0.707f;
            break;

        default:
            break;
        }

        EffectChain chain;

        auto pitch = std::make_unique<PitchShiftEffect>(ci.sampleRate, 2);
        pitch->SetLowLatencyParams(stSeqMs, stSeekMs, stOverlapMs);
        pitch->SetPitchSemiTones(pitchSemis);
        auto* pitchPtr = pitch.get();

        auto eq = std::make_unique<EQEffect>(static_cast<float>(ci.sampleRate), 2);
        eq->SetHighPass(static_cast<float>(hpHz), hpQ);
        eq->SetPresencePeak(static_cast<float>(pkHz), pkQ, pkDB);
        eq->SetLowPass(static_cast<float>(lpHz), lpQ);

        auto gain = std::make_unique<GainEffect>();
        gain->SetGain(gainValue);

        auto clip = std::make_unique<SoftClipperEffect>();
        clip->SetDrive(clipDrive);
        clip->SetOutputGain(clipOutGain);

        // Recommended order for voice:
        // Pitch -> EQ -> Gain -> Clip
        chain.Add(std::move(pitch));
        chain.Add(std::move(eq));
        chain.Add(std::move(gain));
        chain.Add(std::move(clip));

        WasapiRender ren(outDev, rb, stats, chain);
        ren.Initialize();

        auto ri = ren.GetRenderInfo();
        std::cout << "RENDER : " << ri.sampleRate << " Hz, " << ri.channels
                  << " ch, " << (ri.isFloat32 ? "float32" : "int16")
                  << ", bufferFrames=" << ri.bufferFrames << "\n";

        cap.Start();

        const uint32_t warmupTargetFrames = 256;
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

            const auto rbFrames = static_cast<uint32_t>(rb.AvailableToRead());
            const auto padding = ren.GetLastPaddingFrames();
            const auto stQueued = static_cast<uint32_t>(pitchPtr ? pitchPtr->GetQueuedFrames() : 0);

            double latMsTotal = 0.0;
            if (ci.sampleRate > 0)
                latMsTotal = (static_cast<double>(rbFrames + padding + stQueued) * 1000.0) / static_cast<double>(ci.sampleRate);

            std::cout << "captureFrames=" << capFrames << " renderFrames=" << renFrames << " xruns=" << xr << "\n";
            std::cout << "rbFrames=" << rbFrames
                      << " padding=" << padding
                      << " stQueued=" << stQueued
                      << " approxTotalLatencyMs=" << latMsTotal
                      << "\n";
        }

        // Unreachable (Ctrl+C), kept for completeness
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