

#include "media_feature_bridge.h"

#include "../ai/ai_pipeline_manager.h"

MediaFeatureBridge::MediaFeatureBridge(DemuxThreadState& stateRef, SessionResources& resourcesRef,
    std::function<void(bool)> updateLiveRuntimeTuningCallback)
    : state(stateRef)
    , resources(resourcesRef)
    , updateLiveRuntimeTuning(std::move(updateLiveRuntimeTuningCallback))
    , pipelineManager_(std::make_unique<AiPipelineManager>(state, resources, updateLiveRuntimeTuning))
{
}

MediaFeatureBridge::~MediaFeatureBridge() = default;

void MediaFeatureBridge::DisableAll()
{
    if (pipelineManager_)
        pipelineManager_->DisableAll();
}

void MediaFeatureBridge::RebindEpoch(const StreamEpoch& epoch)
{
    if (pipelineManager_)
        pipelineManager_->RebindEpoch(epoch);
}

void MediaFeatureBridge::RebindEpochLocked(const StreamEpoch& epoch)
{
    if (pipelineManager_)
        pipelineManager_->RebindEpochLocked(epoch);
}

void MediaFeatureBridge::UpdateRuntimeHints(
    AiPriorityTier priorityTier,
    bool focusRoute,
    bool alarmRoute,
    bool fullscreenRoute,
    int detectorMinimumSkipFrames)
{
    if (pipelineManager_)
    {
        pipelineManager_->UpdateRuntimeHints(
            priorityTier,
            focusRoute,
            alarmRoute,
            fullscreenRoute,
            detectorMinimumSkipFrames);
    }
}

bool MediaFeatureBridge::SetCapabilityEnabled(const AiFeatureConfig& config, bool enable, std::string* error)
{
    return pipelineManager_ ? pipelineManager_->SetCapabilityEnabled(config, enable, error) : false;
}

bool MediaFeatureBridge::SetCapabilityEnabled(AiCapability capability, bool enable, std::string* error)
{
    return pipelineManager_ ? pipelineManager_->SetCapabilityEnabled(capability, enable, error) : false;
}

bool MediaFeatureBridge::IsCapabilityEnabled(AiCapability capability) const
{
    return pipelineManager_ ? pipelineManager_->IsCapabilityEnabled(capability) : false;
}

AiModelRecord MediaFeatureBridge::GetModelRecord(AiCapability capability) const
{
    return pipelineManager_ ? pipelineManager_->GetModelRecord(capability) : AiModelRecord{};
}

StreamEpoch MediaFeatureBridge::GetCapabilityEpoch(AiCapability capability) const
{
    return pipelineManager_ ? pipelineManager_->GetCapabilityEpoch(capability) : StreamEpoch{};
}

void MediaFeatureBridge::EnableWhisper(bool enable, const std::string& modelPath)
{
    AiFeatureConfig config;
    config.capability = AiCapability::Asr;
    config.modelPath = modelPath;
    config.preferGpu = true;
    config.allowCpuFallback = true;
    SetCapabilityEnabled(config, enable, nullptr);
}

QPointer<WhisperThread> MediaFeatureBridge::GetWhisperThread() const
{
    return pipelineManager_ ? pipelineManager_->GetWhisperThread() : QPointer<WhisperThread>();
}

StreamEpoch MediaFeatureBridge::GetWhisperEpoch() const
{
    return GetCapabilityEpoch(AiCapability::Asr);
}

void MediaFeatureBridge::EnableDetector(bool enable, const std::string& modelPath, const std::string& labelsPath)
{
    AiFeatureConfig config;
    config.capability = AiCapability::Detector;
    config.modelPath = modelPath;
    config.auxModelPath = labelsPath;
    config.preferGpu = true;
    config.allowCpuFallback = true;
    SetCapabilityEnabled(config, enable, nullptr);
}

QPointer<DetectorThread> MediaFeatureBridge::GetDetectorThread() const
{
    return pipelineManager_ ? pipelineManager_->GetDetectorThread() : QPointer<DetectorThread>();
}
