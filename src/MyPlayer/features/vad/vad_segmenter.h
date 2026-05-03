#pragma once

#include <memory>
#include <string>
#include <vector>

#include "vad_types.h"

class VadModelSession
{
public:
    VadModelSession();
    ~VadModelSession();

    bool LoadModel(const std::wstring& modelPath);
    void Reset();
    bool IsLoaded() const;
    bool IsUsingGpu() const;
    bool Infer(const float* samples, size_t sampleCount, float& probability) const;

private:
    class Private;
    std::unique_ptr<Private> d_;
};

class VadSegmenter
{
public:
    VadSegmenter();
    ~VadSegmenter();

    bool LoadModel(const std::wstring& modelPath);
    void Reset();
    void SetEnabled(bool enabled) { vadEnabled_ = enabled; }
    bool IsEnabled() const { return vadEnabled_; }
    bool HasModel() const;
    bool IsUsingGpu() const;

    void Process(const std::vector<float>& mono16k, long long startMs, std::vector<VadChunk>& outChunks);

private:
    static constexpr int TARGET_SAMPLE_RATE = 16000;
    static constexpr int FRAME_SAMPLES = 512;
    static constexpr long long PRE_ROLL_MS = 160;
    static constexpr long long MIN_SPEECH_MS = 120;
    static constexpr long long MIN_SILENCE_MS = 280;

    static long long MsToSamples(long long ms);
    static long long SamplesToMs(long long samples);
    static float MeasureRms(const std::vector<float>& samples);
    static float MeasurePeak(const std::vector<float>& samples);
    bool EvaluateFrame(const std::vector<float>& frame, float& score);
    void AppendPreRoll(const std::vector<float>& samples, long long startSample);
    void TrimPreRoll();

    bool vadEnabled_ = true;
    VadModelSession modelSession_;
    bool inSpeech_ = false;
    bool preRollPrimed_ = false;
    bool pendingPrimed_ = false;
    long long preRollStartSample_ = 0;
    long long pendingStartSample_ = 0;
    long long speechRunSamples_ = 0;
    long long silenceRunSamples_ = 0;
    float noiseFloorRms_ = 0.0025f;
    std::vector<float> preRollSamples_;
    std::vector<float> pendingSamples_;
};
