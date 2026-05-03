#pragma once

#include "BYTETracker.h"
#include "detector_common.h"
#include "detector_types.h"
#include "../../core/ai/ai_types.h"
#include "../../core/session/stream_config.h"

#include <QString>
#include <QtGlobal>

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <cuda.h>
#include <cuda_runtime.h>

#ifndef ORT_API_MANUAL_INIT
#define ORT_API_MANUAL_INIT
#endif
#include <onnxruntime_cxx_api.h>

struct AVFrame;
struct DetectorSharedModel;
struct SwsContext;
class InferenceScheduler;

struct DetectorResources
{
    DetectorResources();
    ~DetectorResources();

    std::shared_ptr<const DetectorSharedModel> sharedModel;
    std::vector<QString> classNames;

    std::mutex frameMux;
    std::condition_variable frameCV;
    std::vector<uint8_t> pendingCpuRgb;
    std::vector<uint8_t> stagingCpuRgb;
    int blobWriteIdx = 0;
    int blobReadIdx = 0;

    SwsContext* cachedSwsCtx = nullptr;
    int cachedSrcW = 0;
    int cachedSrcH = 0;
    int cachedSrcFmt = -1;
    int cachedDstW = 0;
    int cachedDstH = 0;
    std::vector<uint8_t> resizedCpuBuffer;

    CUcontext cudaContext = nullptr;
    uint8_t* d_rgbFull = nullptr;
    int d_rgbFullPitch = 0;
    uint8_t* d_rgbResized = nullptr;
    int d_rgbResizedPitch = 0;
    float* d_blob[2] = { nullptr, nullptr };
    uint8_t* d_nv12Temp = nullptr;
    int d_nv12TempPitch = 0;
    cudaStream_t preprocessStream = nullptr;
    cudaEvent_t preprocessEvent = nullptr;
    int gpuSrcW = 0;
    int gpuSrcH = 0;
    int gpuNewW = 0;
    int gpuNewH = 0;

    std::mutex trackerMux;
    std::unique_ptr<BYTETracker> tracker;
};

struct DetectorThreadState
{
    std::atomic<bool> isExit{ false };
    std::atomic<bool> modelLoaded{ false };

    std::string pendingModelPath;
    std::string pendingLabelsPath;
    DetectorModelFamily modelFamily = DetectorModelFamily::Unknown;
    int inputWidth = kDefaultInputSize;
    int inputHeight = kDefaultInputSize;
    std::atomic<bool> canUseGpuPreprocess{ true };
    bool yoloUsesObjectness = false;
    int decodedClassCount = 0;

    std::atomic<DetectorBackend> inferenceBackend{ DetectorBackend::Unknown };
    std::atomic<DetectorBackend> preprocessBackend{ DetectorBackend::Unknown };
    std::atomic<DetectorBackend> postprocessBackend{ DetectorBackend::Cpu };
    std::atomic<DetectorBackend> trackerBackend{ DetectorBackend::Cpu };

    int origWidth = 0;
    int origHeight = 0;
    bool hasNewFrame = false;
    bool gpuPathActive = false;
    quint64 pendingFrameGeneration = 0;
    quint64 pendingFrameSerial = 0;
    std::atomic<quint64> pendingFrameToken{ 0 };
    std::atomic<quint64> mediaGeneration{ 1 };
    std::atomic<quint64> mediaSerial{ 1 };
    std::atomic<int> frameCounter{ 0 };
    std::atomic<int> baseSkipFrames{ 2 };
    std::atomic<int> minimumSkipFrames{ 2 };
    std::atomic<StreamPlaybackKind> playbackKind{ StreamPlaybackKind::File };
    std::atomic<bool> liveMode{ false };
    std::atomic<bool> lowLatencyMode{ false };
    std::atomic<int> priorityTier{ static_cast<int>(AiPriorityTier::Candidate) };
    std::atomic<bool> focusRoute{ false };
    std::atomic<bool> alarmRoute{ false };
    std::atomic<bool> fullscreenRoute{ false };
    InferenceScheduler* scheduler = nullptr;

    bool loggedModelInfo = false;
    bool loggedFirstResult = false;
    bool loggedRawDet = false;
    bool loggedGpuPreprocessPath = false;
    bool loggedCpuPreprocessPath = false;
};

class DetectorModelSession
{
public:
    DetectorModelSession(DetectorThreadState& state, DetectorResources& resources);

    bool LoadModel(const std::string& modelPath, const std::string& labelsPath);
    void SetModelPath(const std::string& path);
    void SetModelConfig(const std::string& modelPath, const std::string& labelsPath);

    bool IsModelLoaded() const;
    bool IsUsingGpuInference() const;
    bool HasGpuPreprocessCapability() const;
    QString GetBackendSummary() const;
    QString GetPipelineSummary() const;
    void SetMinimumSkipFrames(int skip);
    int GetActiveSkipFrames() const;
    int GetBaseSkipFrames() const;
    bool SupportsGpuPreprocess() const;

private:
    void LoadClassNames(const std::string& modelPath, const std::string& labelsPath);

    DetectorThreadState& state_;
    DetectorResources& resources_;
};

class DetectorPreprocessPipeline
{
public:
    DetectorPreprocessPipeline(
        DetectorThreadState& state,
        DetectorResources& resources,
        const DetectorModelSession& modelSession);

    ~DetectorPreprocessPipeline();

    void PushFrame(AVFrame* frame);
    void Clear();
    void NormalizeToBlob(const uint8_t* rgbData, std::vector<float>& blob) const;
    void FreeGpuBuffers();

private:
    void AllocateGpuBuffers(int srcW, int srcH, int newW, int newH);

    DetectorThreadState& state_;
    DetectorResources& resources_;
    const DetectorModelSession& modelSession_;
};

class DetectorOutputDecoder
{
public:
    DetectorOutputDecoder(DetectorThreadState& state, DetectorResources& resources, const float& confidenceThreshold);

    DetectionResult DecodeOutputs(const std::vector<Ort::Value>& outputs, int w, int h);
    QString ResolveClassName(int classId) const;
    void FillClassNames(DetectionResult& result) const;
    std::vector<DetectionBox> ApplyNms(const std::vector<DetectionBox>& boxes, float iouThreshold) const;
    void LimitBoxesByConfidence(std::vector<DetectionBox>& boxes, size_t maxBoxes) const;

private:
    DetectionResult DecodeRtDetrOutput(const Ort::Value& output, int w, int h);
    DetectionResult DecodeRtDetrOutputs(const std::vector<Ort::Value>& outputs, int w, int h);
    DetectionResult DecodeYoloOutput(const Ort::Value& output, int w, int h);

    DetectorThreadState& state_;
    DetectorResources& resources_;
    const float& confidenceThreshold_;
};

class DetectorTrackingStage
{
public:
    DetectorTrackingStage(DetectorThreadState& state, DetectorResources& resources);

    void Reset();
    DetectionResult Track(const DetectionResult& input) const;

private:
    QString ResolveClassName(int classId) const;

    DetectorThreadState& state_;
    DetectorResources& resources_;
};

class DetectorWorkerLoop
{
public:
    DetectorWorkerLoop(
        DetectorThreadState& state,
        DetectorResources& resources,
        DetectorModelSession& modelSession,
        DetectorPreprocessPipeline& preprocessPipeline,
        DetectorOutputDecoder& outputDecoder,
        DetectorTrackingStage& trackingStage);

    void Run(const std::function<void(bool)>& modelReadyCallback,
        const std::function<void(DetectionResult)>& detectionsReadyCallback);

private:
    DetectorThreadState& state_;
    DetectorResources& resources_;
    DetectorModelSession& modelSession_;
    DetectorPreprocessPipeline& preprocessPipeline_;
    DetectorOutputDecoder& outputDecoder_;
    DetectorTrackingStage& trackingStage_;
};
