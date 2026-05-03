

#include "vad_segmenter.h"

#include "../../common/diagnostics/logger.h"
#include "../../core/ai/shared_ai_runtimes.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <vector>

#ifndef ORT_API_MANUAL_INIT
#define ORT_API_MANUAL_INIT
#endif
#include <onnxruntime_cxx_api.h>

namespace
{
constexpr float kMinRmsThreshold = 0.0090f;
constexpr float kNoiseMultiplier = 3.4f;
constexpr float kPeakGate = 0.060f;
constexpr float kSpeechProbThreshold = 0.40f;
}

class VadModelSession::Private
{
public:
    void Shutdown()
    {
        sharedModel.reset();
        state.assign(2 * 1 * 128, 0.0f);
    }

    std::shared_ptr<const SharedVadModel> sharedModel;
    std::vector<float> state;
    int64_t sampleRateValue = 16000;
};

VadModelSession::VadModelSession()
    : d_(std::make_unique<Private>())
{
}

VadModelSession::~VadModelSession() = default;

bool VadModelSession::LoadModel(const std::wstring& modelPath)
{
    d_->Shutdown();
    d_->sharedModel = SharedVadRuntime::Instance().AcquireModel(modelPath);
    if (!d_->sharedModel || !d_->sharedModel->ortSession)
        return false;

    d_->state.assign(2 * 1 * 128, 0.0f);
    Logger::Instance().Log(
        LogLevel::Info,
        "vad",
        "model.loaded",
        "Silero VAD model loaded",
        {
            { "shared_model", "true" },
            { "backend", d_->sharedModel->usingGpu ? "GPU" : "CPU" },
        });
    return true;
}

void VadModelSession::Reset()
{
    d_->state.assign(2 * 1 * 128, 0.0f);
}

bool VadModelSession::IsLoaded() const
{
    return d_ && d_->sharedModel && d_->sharedModel->ortSession != nullptr;
}

bool VadModelSession::IsUsingGpu() const
{
    return d_ && d_->sharedModel && d_->sharedModel->usingGpu;
}

bool VadModelSession::Infer(const float* samples, size_t sampleCount, float& probability) const
{
    if (!d_ || !d_->sharedModel || !d_->sharedModel->ortSession || !samples || sampleCount == 0)
        return false;

    const SharedVadModel& sharedModel = *d_->sharedModel;

    try
    {
        Ort::MemoryInfo memInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        const int64_t inputShape[2] = { 1, static_cast<int64_t>(sampleCount) };
        const int64_t stateShape[3] = { 2, 1, 128 };

        Ort::Value inputTensor = Ort::Value::CreateTensor<float>(
            memInfo, const_cast<float*>(samples), sampleCount, inputShape, 2);
        Ort::Value stateTensor = Ort::Value::CreateTensor<float>(
            memInfo, d_->state.data(), d_->state.size(), stateShape, 3);
        Ort::Value srTensor = Ort::Value::CreateTensor<int64_t>(
            memInfo, &d_->sampleRateValue, 1, nullptr, 0);

        const char* inputNamesRaw[3] = {
            sharedModel.inputName.c_str(),
            sharedModel.stateName.c_str(),
            sharedModel.srName.c_str()
        };
        const char* outputNamesRaw[2] = {
            sharedModel.outputName.c_str(),
            sharedModel.stateOutName.c_str()
        };
        Ort::Value inputValues[3] = { std::move(inputTensor), std::move(stateTensor), std::move(srTensor) };
        auto outputValues = sharedModel.ortSession->Run(
            Ort::RunOptions{ nullptr },
            inputNamesRaw,
            inputValues,
            3,
            outputNamesRaw,
            2);

        float* outputData = outputValues[0].GetTensorMutableData<float>();
        float* stateOutData = outputValues[1].GetTensorMutableData<float>();

        probability = outputData ? outputData[0] : 0.0f;
        if (stateOutData)
            std::memcpy(d_->state.data(), stateOutData, d_->state.size() * sizeof(float));
        return true;
    }
    catch (const Ort::Exception& ex)
    {
        Logger::Instance().Log(
            LogLevel::Error,
            "vad",
            "ort.fail",
            "VAD ONNX Runtime call failed",
            { { "error", ex.what() } });
        return false;
    }
}

VadSegmenter::VadSegmenter() = default;

VadSegmenter::~VadSegmenter() = default;

bool VadSegmenter::LoadModel(const std::wstring& modelPath)
{
    const bool ok = modelSession_.LoadModel(modelPath);
    Reset();
    return ok;
}

void VadSegmenter::Reset()
{
    inSpeech_ = false;
    preRollPrimed_ = false;
    pendingPrimed_ = false;
    preRollStartSample_ = 0;
    pendingStartSample_ = 0;
    speechRunSamples_ = 0;
    silenceRunSamples_ = 0;
    noiseFloorRms_ = 0.0025f;
    preRollSamples_.clear();
    pendingSamples_.clear();
    modelSession_.Reset();
}

bool VadSegmenter::HasModel() const
{
    return modelSession_.IsLoaded();
}

bool VadSegmenter::IsUsingGpu() const
{
    return modelSession_.IsUsingGpu();
}

void VadSegmenter::Process(const std::vector<float>& mono16k, long long startMs,
    std::vector<VadChunk>& outChunks)
{
    if (mono16k.empty())
        return;

    if (!vadEnabled_)
    {
        outChunks.push_back({ startMs, mono16k });
        return;
    }

    const long long startSample = MsToSamples(std::max(0LL, startMs));
    if (!pendingPrimed_)
    {
        pendingPrimed_ = true;
        pendingStartSample_ = startSample;
    }

    pendingSamples_.insert(pendingSamples_.end(), mono16k.begin(), mono16k.end());

    while (static_cast<int>(pendingSamples_.size()) >= FRAME_SAMPLES)
    {
        std::vector<float> frame(pendingSamples_.begin(), pendingSamples_.begin() + FRAME_SAMPLES);
        const long long frameStartSample = pendingStartSample_;
        pendingSamples_.erase(pendingSamples_.begin(), pendingSamples_.begin() + FRAME_SAMPLES);
        pendingStartSample_ += FRAME_SAMPLES;

        float score = 0.0f;
        const bool speechLike = EvaluateFrame(frame, score);

        if (!inSpeech_)
        {
            AppendPreRoll(frame, frameStartSample);

            if (speechLike)
                speechRunSamples_ += FRAME_SAMPLES;
            else
                speechRunSamples_ = 0;

            if (speechRunSamples_ >= MsToSamples(MIN_SPEECH_MS) && !preRollSamples_.empty())
            {
                inSpeech_ = true;
                silenceRunSamples_ = 0;

                VadChunk chunk;
                chunk.startMs = SamplesToMs(preRollStartSample_);
                chunk.samples = preRollSamples_;
                outChunks.push_back(std::move(chunk));

                std::cout << "[vad] speech-open start=" << SamplesToMs(preRollStartSample_)
                          << " score=" << score
                          << (modelSession_.IsLoaded() ? " mode=silero" : " mode=heuristic")
                          << std::endl;

                preRollSamples_.clear();
                preRollPrimed_ = false;
                speechRunSamples_ = 0;
            }

            continue;
        }

        outChunks.push_back({ SamplesToMs(frameStartSample), frame, false });

        if (speechLike)
        {
            silenceRunSamples_ = 0;
        }
        else
        {
            silenceRunSamples_ += FRAME_SAMPLES;
            if (silenceRunSamples_ >= MsToSamples(MIN_SILENCE_MS))
            {
                std::cout << "[vad] speech-close end=" << SamplesToMs(frameStartSample + FRAME_SAMPLES)
                          << " score=" << score
                          << (modelSession_.IsLoaded() ? " mode=silero" : " mode=heuristic")
                          << std::endl;
                if (!outChunks.empty())
                    outChunks.back().flushAfter = true;
                inSpeech_ = false;
                speechRunSamples_ = 0;
                silenceRunSamples_ = 0;
                preRollSamples_.clear();
                preRollPrimed_ = false;
            }
        }
    }
}

long long VadSegmenter::MsToSamples(long long ms)
{
    return ms * TARGET_SAMPLE_RATE / 1000LL;
}

long long VadSegmenter::SamplesToMs(long long samples)
{
    return samples * 1000LL / TARGET_SAMPLE_RATE;
}

float VadSegmenter::MeasureRms(const std::vector<float>& samples)
{
    if (samples.empty())
        return 0.0f;

    double sumSquares = 0.0;
    for (float sample : samples)
        sumSquares += static_cast<double>(sample) * static_cast<double>(sample);

    return static_cast<float>(std::sqrt(sumSquares / static_cast<double>(samples.size())));
}

float VadSegmenter::MeasurePeak(const std::vector<float>& samples)
{
    float peak = 0.0f;
    for (float sample : samples)
        peak = std::max(peak, std::abs(sample));
    return peak;
}

bool VadSegmenter::EvaluateFrame(const std::vector<float>& frame, float& score)
{
    const float rms = MeasureRms(frame);
    const float peak = MeasurePeak(frame);
    const float dynamicThreshold = std::max(kMinRmsThreshold, noiseFloorRms_ * kNoiseMultiplier);
    const bool heuristicSpeech = (rms >= dynamicThreshold) || (peak >= kPeakGate);
    noiseFloorRms_ = 0.98f * noiseFloorRms_ + 0.02f * std::min(rms, dynamicThreshold);

    if (modelSession_.IsLoaded() && modelSession_.Infer(frame.data(), frame.size(), score))
        return score >= kSpeechProbThreshold || heuristicSpeech;

    score = heuristicSpeech ? std::max(rms, peak) : rms;
    return heuristicSpeech;
}

void VadSegmenter::AppendPreRoll(const std::vector<float>& samples, long long startSample)
{
    if (samples.empty())
        return;

    if (!preRollPrimed_)
    {
        preRollPrimed_ = true;
        preRollStartSample_ = startSample;
    }

    preRollSamples_.insert(preRollSamples_.end(), samples.begin(), samples.end());
    TrimPreRoll();
}

void VadSegmenter::TrimPreRoll()
{
    const long long maxSamples = MsToSamples(PRE_ROLL_MS);
    if (maxSamples <= 0)
        return;

    if (static_cast<long long>(preRollSamples_.size()) <= maxSamples)
        return;

    const long long trimSamples = static_cast<long long>(preRollSamples_.size()) - maxSamples;
    preRollSamples_.erase(preRollSamples_.begin(), preRollSamples_.begin() + trimSamples);
    preRollStartSample_ += trimSamples;
}
