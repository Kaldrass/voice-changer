#include <cassert>
#include <iostream>

#include "ai/ONNXVoiceConverter.h"
#include "core/PresetManager.h"

int test_onnx_voice_converter()
{
    std::cout << "Testing ONNXVoiceConverter...\n";

    ONNXVoiceConverter converter;

    // Test set profile
    converter.SetProfile("bright");
    converter.SetBlend(0.75f);
    converter.SetIntensity(0.5f);

    // Test process with dummy audio
    float audioSamples[256];
    for (int i = 0; i < 256; ++i)
    {
        audioSamples[i] = std::sin(i * 0.01f);
    }

    converter.ProcessInterleaved(audioSamples, 128, 1, 48000);

    // Verify output is finite
    for (int i = 0; i < 256; ++i)
    {
        assert(std::isfinite(audioSamples[i]));
        assert(audioSamples[i] >= -1.1f && audioSamples[i] <= 1.1f);
    }

    // Test fine-tuning (placeholder)
    bool finetunedOk = converter.FineTune("dummy_audio.wav");
    assert(finetunedOk);

    // Test weight get/set
    auto weights = converter.GetWeights();
    assert(weights.size() == 20);
    converter.SetWeights(weights);

    std::cout << "  ONNXVoiceConverter: OK\n";
    return 0;
}

int test_preset_manager()
{
    std::cout << "Testing PresetManager...\n";

    PresetManager mgr;

    // Create and save preset
    VoicePreset preset;
    preset.name = "TestPreset";
    preset.description = "Test fine-tuned voice";
    preset.profile = "dark";
    preset.blend = 0.7f;
    preset.intensity = 0.5f;
    preset.weights = std::vector<float>(20, 0.5f);

    bool saveOk = mgr.SavePreset(preset);
    assert(saveOk);

    // Load preset
    VoicePreset loaded;
    bool loadOk = mgr.LoadPreset("TestPreset", loaded);
    assert(loadOk);
    assert(loaded.name == "TestPreset");
    assert(loaded.profile == "dark");
    assert(loaded.blend == 0.7f);

    // List presets
    auto presets = mgr.ListPresets();
    assert(presets.size() >= 1);

    std::cout << "  PresetManager: OK\n";
    return 0;
}

int main()
{
    std::cout << "\n=== ONNX & Preset Tests ===\n";
    int result = 0;
    result += test_onnx_voice_converter();
    result += test_preset_manager();
    std::cout << "=== All tests completed ===\n\n";
    return result;
}
