// src/app/main.cpp
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <filesystem>
#include <numeric>
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
#include "dsp/VoiceConverterEffect.h"

#include "ai/LocalVoiceConverter.h"
#include "ai/ONNXVoiceConverter.h"
#include "core/PresetManager.h"

#include "app/Preset.h"

static void PrintDevices(const std::vector<AudioDeviceInfo>& devs, const char* title)
{
    std::wcout << L"\n== " << title << L" ==\n";
    for (size_t i = 0; i < devs.size(); ++i)
        std::wcout << L"[" << i << L"] " << devs[i].name << L"\n";
}

static std::wstring ArgToWString(const char* s)
{
    if (!s) return L"";

    const int wideLen = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, nullptr, 0);
    if (wideLen > 0)
    {
        std::wstring out(static_cast<size_t>(wideLen - 1), L'\0');
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, out.data(), wideLen);
        return out;
    }

    // Fallback for non-UTF8 shells/locales.
    const int ansiLen = MultiByteToWideChar(CP_ACP, 0, s, -1, nullptr, 0);
    if (ansiLen <= 0) return L"";

    std::wstring out(static_cast<size_t>(ansiLen - 1), L'\0');
    MultiByteToWideChar(CP_ACP, 0, s, -1, out.data(), ansiLen);
    return out;
}

static float EstimateTrainingScore(const std::vector<float>& w)
{
    if (w.empty()) return 0.0f;
    float sum = std::accumulate(w.begin(), w.end(), 0.0f);
    float mean = sum / static_cast<float>(w.size());
    float var = 0.0f;
    for (float v : w)
    {
        const float d = v - mean;
        var += d * d;
    }
    var /= static_cast<float>(w.size());

    // Heuristic confidence in [0..100] for V1 approximation.
    const float score01 = std::clamp(0.45f + mean * 0.35f + std::sqrt(var) * 0.35f, 0.0f, 1.0f);
    return score01 * 100.0f;
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

        bool useAiVoice = false;
        std::string aiProfile = "neutral";
        float aiBlend = 0.65f;
        float aiIntensity = 0.4f;

        PresetType preset = PresetType::None;
        std::string presetName;
        std::string finetuneAudioPath;
        std::string loadPresetName;
        std::string aiModelPath;
        bool showPresetsDir = false;
        bool loadedPresetOk = false;
        std::vector<float> loadedPresetWeights;

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
            else if (a == "--mode" && i + 1 < argc)
            {
                const std::string mode = argv[++i];
                if (mode == "ai") useAiVoice = true;
                else if (mode == "dsp") useAiVoice = false;
                else throw std::runtime_error("Invalid --mode value. Use dsp or ai.");
            }
            else if (a == "--ai-profile" && i + 1 < argc) aiProfile = argv[++i];
            else if (a == "--ai-blend" && i + 1 < argc) aiBlend = std::stof(argv[++i]);
            else if (a == "--ai-intensity" && i + 1 < argc) aiIntensity = std::stof(argv[++i]);
            else if (a == "--preset" && i + 1 < argc) preset = ParsePreset(argv[++i]);
            else if (a == "--preset-name" && i + 1 < argc) presetName = argv[++i];
            else if (a == "--fine-tune-audio" && i + 1 < argc) finetuneAudioPath = argv[++i];
            else if (a == "--load-preset" && i + 1 < argc) loadPresetName = argv[++i];
            else if (a == "--ai-model" && i + 1 < argc) aiModelPath = argv[++i];
            else if (a == "--show-presets-dir") showPresetsDir = true;
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
                    << "  --mode <dsp|ai>             Processing mode\n"
                    << "  --ai-profile <name>         AI profile: neutral, bright, dark, robot\n"
                    << "  --ai-blend <float>          AI dry/wet blend [0..1]\n"
                    << "  --ai-intensity <float>      AI coloration amount [0..1]\n"
                    << "  --st-seq-ms <int>           SoundTouch sequence ms\n"
                    << "  --st-seek-ms <int>          SoundTouch seek window ms\n"
                    << "  --st-overlap-ms <int>       SoundTouch overlap ms\n"
                    << "  --preset <name>             girl, demon, robot, radio\n"
                    << "  --fine-tune-audio <path>    Fine-tune IA weights from reference audio\n"
                    << "  --preset-name <name>        Preset name used with --fine-tune-audio\n"
                    << "  --load-preset <name>        Load a previously saved IA preset\n"
                    << "  --ai-model <path>           Optional ONNX model path (mel_in/mel_out)\n"
                    << "  --show-presets-dir          Print presets directory and exit\n";
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

        if (showPresetsDir)
        {
            std::cout << PresetManager::GetPresetsDirectory() << "\n";
            CoUninitialize();
            return 0;
        }

        if (!finetuneAudioPath.empty())
        {
            if (!std::filesystem::exists(finetuneAudioPath))
                throw std::runtime_error("Fine-tune audio file not found.");

            ONNXVoiceConverter converter;
            converter.SetProfile(aiProfile);
            converter.SetBlend(aiBlend);
            converter.SetIntensity(aiIntensity);

            std::cout << "Fine-tuning IA model from: " << finetuneAudioPath << "\n";
            if (!converter.FineTune(finetuneAudioPath))
                throw std::runtime_error("Fine-tuning failed.");

            const std::string resolvedPresetName = presetName.empty() ? "trained_preset" : presetName;

            PresetManager mgr;
            VoicePreset voicePreset;
            voicePreset.name = resolvedPresetName;
            voicePreset.description = "Fine-tuned from: " + finetuneAudioPath;
            voicePreset.weights = converter.GetWeights();
            voicePreset.profile = aiProfile;
            voicePreset.blend = aiBlend;
            voicePreset.intensity = aiIntensity;

            const float score = EstimateTrainingScore(voicePreset.weights);
            std::cout << "Training score: " << score << "/100\n";

            if (!mgr.SavePreset(voicePreset))
                throw std::runtime_error("Unable to save preset.");

            std::cout << "Preset directory: " << PresetManager::GetPresetsDirectory() << "\n";
            std::cout << "Fine-tuning complete. Preset saved: " << resolvedPresetName << "\n";
            CoUninitialize();
            return 0;
        }

        if (!loadPresetName.empty())
        {
            PresetManager presetMgr;
            VoicePreset loadedPreset;
            if (!presetMgr.LoadPreset(loadPresetName, loadedPreset))
                throw std::runtime_error("Failed to load preset: " + loadPresetName);

            aiProfile = loadedPreset.profile;
            aiBlend = loadedPreset.blend;
            aiIntensity = loadedPreset.intensity;
            loadedPresetWeights = loadedPreset.weights;
            loadedPresetOk = !loadedPresetWeights.empty();
            useAiVoice = true;

            std::cout << "Loaded preset: " << loadedPreset.name
                      << " (weights=" << loadedPresetWeights.size() << ")\n";
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

        std::unique_ptr<VoiceConverterEffect> aiFx;
        if (useAiVoice)
        {
            std::unique_ptr<IVoiceConverter> converter;
            if (loadedPresetOk || !aiModelPath.empty())
            {
                auto onnxConverter = std::make_unique<ONNXVoiceConverter>();

                if (loadedPresetOk)
                    onnxConverter->SetWeights(loadedPresetWeights);

                if (!aiModelPath.empty())
                {
                    if (!onnxConverter->LoadNeuralModel(aiModelPath))
                    {
                        std::cout << "[warn] ONNX model not loaded: " << onnxConverter->GetLastModelError() << "\n";
                        std::cout << "[warn] Falling back to heuristic IA path.\n";
                    }
                    else
                    {
                        std::cout << "ONNX model loaded: " << aiModelPath << "\n";
                    }
                }

                converter = std::move(onnxConverter);
            }
            else
            {
                converter = std::make_unique<LocalVoiceConverter>();
            }

            aiFx = std::make_unique<VoiceConverterEffect>(std::move(converter), ci.sampleRate);
            aiFx->SetProfile(aiProfile);
            aiFx->SetBlend(aiBlend);
            aiFx->SetIntensity(aiIntensity);
        }

        // Recommended order for voice:
        // AI(optional) -> Pitch -> EQ -> Gain -> Clip
        if (aiFx) chain.Add(std::move(aiFx));
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