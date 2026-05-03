

#include "detector_thread.h"

#include <QMetaType>

DetectorThread::DetectorThread()
    : modelSession_(state_, resources_)
    , outputDecoder_(state_, resources_, confidenceThreshold)
    , trackingStage_(state_, resources_)
    , preprocessPipeline_(state_, resources_, modelSession_)
    , workerLoop_(state_, resources_, modelSession_, preprocessPipeline_, outputDecoder_, trackingStage_)
{
    qRegisterMetaType<DetectionResult>("DetectionResult");
}

DetectorThread::~DetectorThread()
{
    Stop();
}

void DetectorThread::SetModelPath(const std::string& path)
{
    modelSession_.SetModelPath(path);
}

void DetectorThread::SetModelConfig(const std::string& modelPath, const std::string& labelsPath)
{
    modelSession_.SetModelConfig(modelPath, labelsPath);
}

void DetectorThread::SetInferenceScheduler(InferenceScheduler* scheduler)
{
    state_.scheduler = scheduler;
}

void DetectorThread::SetSessionContext(const AiSessionContext& context)
{
    state_.playbackKind.store(context.playbackKind);
    state_.liveMode.store(context.live);
    state_.lowLatencyMode.store(context.lowLatency);
    state_.priorityTier.store(static_cast<int>(context.priorityTier));
    state_.focusRoute.store(context.focusRoute);
    state_.alarmRoute.store(context.alarmRoute);
    state_.fullscreenRoute.store(context.fullscreenRoute);
    SetMinimumSkipFrames(context.detectorMinimumSkipFrames);
}

void DetectorThread::start()
{
    if (worker_.joinable())
        return;

    state_.isExit.store(false);
    worker_ = std::thread(&DetectorThread::run, this);
}

void DetectorThread::PushFrame(AVFrame* frame)
{
    preprocessPipeline_.PushFrame(frame);
}

void DetectorThread::Clear()
{
    preprocessPipeline_.Clear();
    trackingStage_.Reset();
}

void DetectorThread::SetMediaEpoch(const StreamEpoch& epoch)
{
    state_.mediaGeneration.store(epoch.generation);
    state_.mediaSerial.store(epoch.serial);
    Clear();
}

StreamEpoch DetectorThread::GetMediaEpoch() const
{
    return StreamEpoch{ state_.mediaGeneration.load(), state_.mediaSerial.load() };
}

void DetectorThread::Stop()
{
    state_.isExit.store(true);
    resources_.frameCV.notify_one();
    if (worker_.joinable())
        worker_.join();
}

bool DetectorThread::IsModelLoaded() const
{
    return modelSession_.IsModelLoaded();
}

bool DetectorThread::IsUsingGpuInference() const
{
    return modelSession_.IsUsingGpuInference();
}

bool DetectorThread::HasGpuPreprocessCapability() const
{
    return modelSession_.HasGpuPreprocessCapability();
}

QString DetectorThread::GetBackendSummary() const
{
    return modelSession_.GetBackendSummary();
}

QString DetectorThread::GetPipelineSummary() const
{
    return modelSession_.GetPipelineSummary();
}

void DetectorThread::SetMinimumSkipFrames(int skip)
{
    modelSession_.SetMinimumSkipFrames(skip);
}

int DetectorThread::GetActiveSkipFrames() const
{
    return modelSession_.GetActiveSkipFrames();
}

int DetectorThread::GetBaseSkipFrames() const
{
    return modelSession_.GetBaseSkipFrames();
}

void DetectorThread::run()
{
    workerLoop_.Run(
        [this](bool success) {
            emit ModelReady(success);
        },
        [this](DetectionResult result) {
            emit DetectionsReady(std::move(result));
        });
}
