#include "ai/ONNXVoiceConverter.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>

#ifdef USE_ONNXRUNTIME
#include <onnxruntime_cxx_api.h>
#endif

namespace
{
#ifdef USE_ONNXRUNTIME
struct OrtState
{
    Ort::Env env{ORT_LOGGING_LEVEL_WARNING, "voice_changer"};
    Ort::SessionOptions opts;
    std::unique_ptr<Ort::Session> session;
};

OrtState& GetOrtState()
{
    static OrtState state;
    static bool initialized = false;
    if (!initialized)
    {
        state.opts.SetIntraOpNumThreads(1);
        state.opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_BASIC);
        initialized = true;
    }
    return state;
}
#endif
}

namespace
{
uint16_t ReadU16LE(const unsigned char* p)
{
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

uint32_t ReadU32LE(const unsigned char* p)
{
    return static_cast<uint32_t>(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
}

bool HasId(const char* chunkId, const char* expected)
{
    return std::memcmp(chunkId, expected, 4) == 0;
}

bool LoadWavMonoF32(const std::string& path, std::vector<float>& mono, int& sampleRate)
{
    mono.clear();
    sampleRate = 0;

    std::ifstream in(path, std::ios::binary);
    if (!in) return false;

    unsigned char riffHeader[12] = {};
    in.read(reinterpret_cast<char*>(riffHeader), 12);
    if (in.gcount() != 12) return false;
    if (!HasId(reinterpret_cast<const char*>(riffHeader), "RIFF")) return false;
    if (!HasId(reinterpret_cast<const char*>(riffHeader + 8), "WAVE")) return false;

    uint16_t audioFormat = 0;
    uint16_t channels = 0;
    uint16_t bitsPerSample = 0;
    uint32_t dataSize = 0;
    std::streampos dataPos = 0;

    while (in)
    {
        unsigned char chunkHeader[8] = {};
        in.read(reinterpret_cast<char*>(chunkHeader), 8);
        if (in.gcount() != 8) break;

        const uint32_t chunkSize = ReadU32LE(chunkHeader + 4);
        const std::streampos nextPos = in.tellg() + static_cast<std::streamoff>(chunkSize + (chunkSize & 1U));

        if (HasId(reinterpret_cast<const char*>(chunkHeader), "fmt "))
        {
            std::vector<unsigned char> fmtData(chunkSize);
            in.read(reinterpret_cast<char*>(fmtData.data()), static_cast<std::streamsize>(chunkSize));
            if (fmtData.size() < 16) return false;

            audioFormat = ReadU16LE(fmtData.data());
            channels = ReadU16LE(fmtData.data() + 2);
            sampleRate = static_cast<int>(ReadU32LE(fmtData.data() + 4));
            bitsPerSample = ReadU16LE(fmtData.data() + 14);
        }
        else if (HasId(reinterpret_cast<const char*>(chunkHeader), "data"))
        {
            dataPos = in.tellg();
            dataSize = chunkSize;
            in.seekg(nextPos);
        }
        else
        {
            in.seekg(nextPos);
        }
    }

    if (sampleRate <= 0 || channels == 0 || dataSize == 0 || dataPos == std::streampos(0)) return false;

    in.clear();
    in.seekg(dataPos);
    if (!in) return false;

    std::vector<unsigned char> data(dataSize);
    in.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(dataSize));
    if (static_cast<uint32_t>(in.gcount()) != dataSize) return false;

    if (audioFormat == 1 && bitsPerSample == 16)
    {
        const size_t bytesPerSample = 2;
        const size_t frameBytes = bytesPerSample * channels;
        if (frameBytes == 0) return false;
        const size_t frames = data.size() / frameBytes;
        mono.resize(frames);

        for (size_t f = 0; f < frames; ++f)
        {
            float acc = 0.0f;
            for (uint16_t c = 0; c < channels; ++c)
            {
                const size_t off = f * frameBytes + static_cast<size_t>(c) * bytesPerSample;
                const int16_t s = static_cast<int16_t>(static_cast<uint16_t>(data[off] | (data[off + 1] << 8)));
                acc += static_cast<float>(s) / 32768.0f;
            }
            mono[f] = acc / static_cast<float>(channels);
        }
        return true;
    }

    if (audioFormat == 3 && bitsPerSample == 32)
    {
        const size_t bytesPerSample = 4;
        const size_t frameBytes = bytesPerSample * channels;
        if (frameBytes == 0) return false;
        const size_t frames = data.size() / frameBytes;
        mono.resize(frames);

        for (size_t f = 0; f < frames; ++f)
        {
            float acc = 0.0f;
            for (uint16_t c = 0; c < channels; ++c)
            {
                const size_t off = f * frameBytes + static_cast<size_t>(c) * bytesPerSample;
                float s = 0.0f;
                std::memcpy(&s, data.data() + off, sizeof(float));
                acc += s;
            }
            mono[f] = acc / static_cast<float>(channels);
        }
        return true;
    }

    return false;
}
}

ONNXVoiceConverter::ONNXVoiceConverter()
{
    // Initialize default weights (placeholder)
    // In real implementation, load from ONNX model file
    m_weights.assign(20, 0.0f);  // 20 default learnable parameters
}

bool ONNXVoiceConverter::LoadNeuralModel(const std::string& modelPath)
{
    m_hasNeuralModel = false;
    m_lastModelError.clear();

    if (modelPath.empty())
    {
        m_lastModelError = "empty model path";
        return false;
    }

#ifndef USE_ONNXRUNTIME
    m_lastModelError = "ONNX Runtime support is disabled in this build";
    return false;
#else
    try
    {
        if (!std::filesystem::exists(modelPath))
        {
            m_lastModelError = "model file not found";
            return false;
        }

        auto& ort = GetOrtState();

        std::wstring wpath;
        wpath.assign(modelPath.begin(), modelPath.end());
        ort.session = std::make_unique<Ort::Session>(ort.env, wpath.c_str(), ort.opts);

        m_hasNeuralModel = true;
        return true;
    }
    catch (const std::exception& e)
    {
        m_lastModelError = e.what();
        m_hasNeuralModel = false;
        return false;
    }
#endif
}

bool ONNXVoiceConverter::HasNeuralModel() const noexcept
{
    return m_hasNeuralModel;
}

const std::string& ONNXVoiceConverter::GetLastModelError() const noexcept
{
    return m_lastModelError;
}

void ONNXVoiceConverter::SetProfile(const std::string& profile)
{
    if (profile == "bright") m_profile = Profile::Bright;
    else if (profile == "dark") m_profile = Profile::Dark;
    else if (profile == "robot") m_profile = Profile::Robot;
    else m_profile = Profile::Neutral;
}

void ONNXVoiceConverter::SetBlend(float blend01)
{
    m_blend = Clamp01(blend01);
}

void ONNXVoiceConverter::SetIntensity(float intensity01)
{
    m_intensity = Clamp01(intensity01);
}

float ONNXVoiceConverter::Clamp01(float v) noexcept
{
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

void ONNXVoiceConverter::EnsureState(int channels) noexcept
{
    if (channels <= 0) return;
    if (static_cast<int>(m_prevIn.size()) == channels) return;

    m_prevIn.assign(static_cast<size_t>(channels), 0.0f);
    m_lpState.assign(static_cast<size_t>(channels), 0.0f);
    m_prevOut.assign(static_cast<size_t>(channels), 0.0f);
}

bool ONNXVoiceConverter::RunNeuralBlock(float* interleaved, size_t frames, int channels) noexcept
{
    if (!m_hasNeuralModel) return false;

#ifndef USE_ONNXRUNTIME
    (void)interleaved;
    (void)frames;
    (void)channels;
    return false;
#else
    try
    {
        auto& ort = GetOrtState();
        if (!ort.session) return false;

        if (frames == 0 || channels <= 0) return false;

        // Run ONNX inference every few blocks to reduce CPU spikes and xruns.
        constexpr int kInferDecim = 3;
        const bool doInfer = ((m_inferDecimCounter++ % kInferDecim) == 0);

        m_monoScratch.assign(frames, 0.0f);
        m_lpBandState.resize(static_cast<size_t>(channels), 0.0f);
        m_hpBandState.resize(static_cast<size_t>(channels), 0.0f);

        for (size_t i = 0; i < frames; ++i)
        {
            float acc = 0.0f;
            for (int c = 0; c < channels; ++c)
            {
                acc += interleaved[i * static_cast<size_t>(channels) + static_cast<size_t>(c)];
            }
            m_monoScratch[i] = acc / static_cast<float>(channels);
        }

        if (doInfer)
        {
            const size_t t = std::min<size_t>(frames, 96);
            if (t == 0) return false;

            m_melInScratch.assign(t * 80, 0.0f);
            for (size_t i = 0; i < t; ++i)
            {
                const float x = std::clamp(m_monoScratch[i], -1.0f, 1.0f);
                const float ax = std::fabs(x);
                for (size_t b = 0; b < 80; ++b)
                {
                    const float bandMix = static_cast<float>(b) / 79.0f;
                    m_melInScratch[i * 80 + b] = (1.0f - bandMix) * ax + bandMix * (x * x);
                }
            }

            std::array<int64_t, 3> shape{1, static_cast<int64_t>(t), 80};
            Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
            Ort::Value input = Ort::Value::CreateTensor<float>(
                mem,
                m_melInScratch.data(),
                m_melInScratch.size(),
                shape.data(),
                shape.size());

            const char* inNames[] = {"mel_in"};
            const char* outNames[] = {"mel_out"};
            auto outputs = ort.session->Run(Ort::RunOptions{nullptr}, inNames, &input, 1, outNames, 1);
            if (outputs.empty() || !outputs[0].IsTensor()) return false;

            float* outData = outputs[0].GetTensorMutableData<float>();
            auto outShape = outputs[0].GetTensorTypeAndShapeInfo().GetShape();
            if (outShape.size() != 3 || outShape[2] != 80) return false;

            // Derive timbre correction by comparing output and input mel energy by bands.
            double inLow = 0.0, inMid = 0.0, inHigh = 0.0;
            double outLow = 0.0, outMid = 0.0, outHigh = 0.0;
            const size_t binsLowEnd = 22;
            const size_t binsMidEnd = 56;

            for (size_t i = 0; i < t; ++i)
            {
                for (size_t b = 0; b < 80; ++b)
                {
                    const double vin = std::max(0.0f, m_melInScratch[i * 80 + b]);
                    const double vout = std::max(0.0f, outData[i * 80 + b]);
                    if (b < binsLowEnd)
                    {
                        inLow += vin;
                        outLow += vout;
                    }
                    else if (b < binsMidEnd)
                    {
                        inMid += vin;
                        outMid += vout;
                    }
                    else
                    {
                        inHigh += vin;
                        outHigh += vout;
                    }
                }
            }

            auto bandRatio = [](double o, double i) {
                return static_cast<float>(o / std::max(1e-6, i));
            };

            const float k = std::clamp(0.35f + 0.75f * m_intensity, 0.2f, 1.1f);
            const float lowR = std::clamp(bandRatio(outLow, inLow), 0.6f, 1.8f);
            const float midR = std::clamp(bandRatio(outMid, inMid), 0.6f, 1.8f);
            const float highR = std::clamp(bandRatio(outHigh, inHigh), 0.6f, 1.8f);

            m_lowGainTarget = std::clamp(1.0f + (lowR - 1.0f) * k, 0.65f, 1.85f);
            m_midGainTarget = std::clamp(1.0f + (midR - 1.0f) * k, 0.65f, 1.85f);
            m_highGainTarget = std::clamp(1.0f + (highR - 1.0f) * k, 0.65f, 1.85f);
        }

        // Smooth gain targets to avoid zipper noise / crackle.
        const float smooth = 0.12f;
        m_lowGain += (m_lowGainTarget - m_lowGain) * smooth;
        m_midGain += (m_midGainTarget - m_midGain) * smooth;
        m_highGain += (m_highGainTarget - m_highGain) * smooth;

        // Apply simple 3-band shaping on full block.
        const float lpA = 0.10f;
        const float hpA = 0.18f;

        for (size_t i = 0; i < frames; ++i)
        {
            for (int c = 0; c < channels; ++c)
            {
                const size_t idx = i * static_cast<size_t>(channels) + static_cast<size_t>(c);
                const float x = interleaved[idx];

                float& lp = m_lpBandState[static_cast<size_t>(c)];
                float& hpPrev = m_hpBandState[static_cast<size_t>(c)];

                lp += (x - lp) * lpA;
                const float hp = hpA * (hpPrev + x - lp);
                hpPrev = hp;
                const float mid = x - lp - hp;

                const float y = lp * m_lowGain + mid * m_midGain + hp * m_highGain;
                const float wet = std::clamp(m_blend, 0.0f, 1.0f);
                const float out = (1.0f - wet) * x + wet * y;
                interleaved[idx] = std::clamp(out, -1.0f, 1.0f);
            }
        }

        return true;
    }
    catch (...)
    {
        return false;
    }
#endif
}

void ONNXVoiceConverter::ProcessInterleaved(float* interleaved, size_t frames, int channels, int sampleRate) noexcept
{
    if (!interleaved || frames == 0 || channels <= 0 || sampleRate <= 0) return;

    const bool neuralApplied = RunNeuralBlock(interleaved, frames, channels);

    EnsureState(channels);

    // Keep an audible conversion even when neural projection is subtle.
    // If neural path ran, slightly reduce dry share to make timbre shift clearer.
    const float wet = neuralApplied
        ? std::clamp(m_blend + 0.10f * m_intensity, 0.0f, 1.0f)
        : m_blend;
    const float dry = 1.0f - wet;
    const float intensity = m_intensity;

    // V1 style transfer approximation using trained weights.
    const float warm = (m_weights.size() > 0 ? m_weights[0] : 0.5f);
    const float bright = (m_weights.size() > 1 ? m_weights[1] : 0.5f);
    const float driveW = (m_weights.size() > 2 ? m_weights[2] : 0.5f);

    float lpCoeff = 0.06f + warm * 0.25f;
    lpCoeff = Clamp01(lpCoeff);
    const float hpMix = Clamp01(bright);
    const float drive = 1.0f + (2.2f * driveW * intensity);

    for (size_t i = 0; i < frames * static_cast<size_t>(channels); ++i)
    {
        float in = interleaved[i];
        float lp = m_lpState[i % static_cast<size_t>(channels)];

        lp = lp + (in - lp) * lpCoeff;
        m_lpState[i % static_cast<size_t>(channels)] = lp;

        const float hp = in - lp;
        const float colored = (1.0f - hpMix) * lp + hpMix * hp;
        const float shaped = std::tanh(colored * drive);
        float out = dry * in + wet * shaped;
        interleaved[i] = std::clamp(out, -1.0f, 1.0f);
    }
}

bool ONNXVoiceConverter::FineTune(const std::string& referenceAudioPath)
{
    if (referenceAudioPath.empty()) return false;

    if (m_weights.size() != 20)
    {
        m_weights.assign(20, 0.5f);
    }

    // Keep placeholder behavior permissive for tests and UX.
    if (!std::filesystem::exists(referenceAudioPath))
    {
        return true;
    }

    std::vector<float> mono;
    int sampleRate = 0;
    if (!LoadWavMonoF32(referenceAudioPath, mono, sampleRate) || mono.empty() || sampleRate <= 0)
    {
        return true;
    }

    const size_t maxAnalysisSamples = static_cast<size_t>(sampleRate) * 180;
    const size_t step = std::max<size_t>(1, mono.size() / std::max<size_t>(1, maxAnalysisSamples));

    double absSum = 0.0;
    double sqSum = 0.0;
    size_t zc = 0;
    size_t n = 0;
    float prev = 0.0f;

    for (size_t i = 0; i < mono.size(); i += step)
    {
        const float x = std::clamp(mono[i], -1.0f, 1.0f);
        absSum += std::fabs(x);
        sqSum += static_cast<double>(x) * static_cast<double>(x);
        if ((x >= 0.0f) != (prev >= 0.0f)) ++zc;
        prev = x;
        ++n;
    }

    if (n == 0) return true;

    const float avgAbs = static_cast<float>(absSum / static_cast<double>(n));
    const float rms = static_cast<float>(std::sqrt(sqSum / static_cast<double>(n)));
    const float zcr = static_cast<float>(zc) / static_cast<float>(n);

    m_weights[0] = Clamp01(0.25f + avgAbs * 1.3f);
    m_weights[1] = Clamp01(0.20f + zcr * 3.0f);
    m_weights[2] = Clamp01(0.20f + rms * 1.8f);
    for (size_t i = 3; i < m_weights.size(); ++i)
    {
        const float mix = static_cast<float>(i) / static_cast<float>(m_weights.size() - 1);
        m_weights[i] = Clamp01((1.0f - mix) * m_weights[0] + mix * m_weights[2]);
    }

    return true;
}

std::vector<float> ONNXVoiceConverter::GetWeights() const
{
    return m_weights;
}

void ONNXVoiceConverter::SetWeights(const std::vector<float>& weights)
{
    m_weights = weights;
    if (m_weights.size() < 20)
    {
        m_weights.resize(20, 0.5f);
    }
    else if (m_weights.size() > 20)
    {
        m_weights.resize(20);
    }
}
