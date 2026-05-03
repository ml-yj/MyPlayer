#pragma once

#include <QObject>

#include <thread>

#include "detector_pipeline.h"
#include "detector_types.h"
#include "../../core/ai/ai_types.h"
#include "../../core/media/demux_types.h"

struct AVFrame;

class DetectorThread : public QObject
{
    Q_OBJECT

public:
    DetectorThread();
    ~DetectorThread();

    void SetModelPath(const std::string& path);
    void SetModelConfig(const std::string& modelPath, const std::string& labelsPath);
    void SetInferenceScheduler(InferenceScheduler* scheduler);
    void SetSessionContext(const AiSessionContext& context);
    void start();
    void PushFrame(AVFrame* frame);
    void Clear();
    void SetMediaEpoch(const StreamEpoch& epoch);
    void Stop();
    StreamEpoch GetMediaEpoch() const;
    bool IsModelLoaded() const;
    bool IsUsingGpuInference() const;
    bool HasGpuPreprocessCapability() const;
    QString GetBackendSummary() const;
    QString GetPipelineSummary() const;
    void SetMinimumSkipFrames(int skip);
    int GetActiveSkipFrames() const;
    int GetBaseSkipFrames() const;

    float confidenceThreshold = 0.5f;

signals:
    void DetectionsReady(DetectionResult result);
    void ModelReady(bool success);

private:
    void run();

    DetectorThreadState state_;
    DetectorResources resources_;
    DetectorModelSession modelSession_;
    DetectorOutputDecoder outputDecoder_;
    DetectorTrackingStage trackingStage_;
    DetectorPreprocessPipeline preprocessPipeline_;
    DetectorWorkerLoop workerLoop_;
    std::thread worker_;
};
