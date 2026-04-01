#pragma once

#include "ai/IVoiceConverter.h"

#include <string>
#include <vector>
#include <map>

// Placeholder for ONNX Runtime integration.
// This will eventually use ONNX Runtime C++ API for model loading/inference.
class ONNXVoiceConverter final : public IVoiceConverter
{
public:
    ONNXVoiceConverter();

    // Try to load an ONNX model for neural inference.
    // Expected IO names: mel_in [1,T,80], mel_out [1,T,80].
    bool LoadNeuralModel(const std::string& modelPath);
    bool HasNeuralModel() const noexcept;
    const std::string& GetLastModelError() const noexcept;

    void SetProfile(const std::string& profile) override;
    void SetBlend(float blend01) override;
    void SetIntensity(float intensity01) override;

    void ProcessInterleaved(float* interleaved, size_t frames, int channels, int sampleRate) noexcept override;

    // Fine-tuning interface
    // referenceAudioPath: path to audio file representing target voice
    // returns: true if fine-tuning succeeded, pweights stored internally
    bool FineTune(const std::string& referenceAudioPath);

    // Get current weights (for persistence)
    std::vector<float> GetWeights() const;

    // Set weights (for loading presets)
    void SetWeights(const std::vector<float>& weights);

private:
    enum class Profile
    {
        Neutral,
        Bright,
        Dark,
        Robot
    };

    static float Clamp01(float v) noexcept;

private:
    Profile m_profile = Profile::Neutral;
    float m_blend = 0.65f;
    float m_intensity = 0.4f;

    // Learnable weights (placeholder)
    // In real implementation, these would be ONNX model parameters
    std::vector<float> m_weights;

    // For now, filter state (similar to LocalVoiceConverter)
    std::vector<float> m_prevIn;
    std::vector<float> m_lpState;
    std::vector<float> m_prevOut;

    bool m_hasNeuralModel = false;
    std::string m_lastModelError;

    // Scratch buffers for optional neural path.
    std::vector<float> m_monoScratch;
    std::vector<float> m_melInScratch;
    std::vector<float> m_melOutScratch;

    // Runtime timbre controls inferred from model output.
    float m_lowGain = 1.0f;
    float m_midGain = 1.0f;
    float m_highGain = 1.0f;
    float m_lowGainTarget = 1.0f;
    float m_midGainTarget = 1.0f;
    float m_highGainTarget = 1.0f;
    int m_inferDecimCounter = 0;

    // Per-channel state for simple 3-band split (LP + HP, MID residual).
    std::vector<float> m_lpBandState;
    std::vector<float> m_hpBandState;

    bool RunNeuralBlock(float* interleaved, size_t frames, int channels) noexcept;

    void EnsureState(int channels) noexcept;
};
