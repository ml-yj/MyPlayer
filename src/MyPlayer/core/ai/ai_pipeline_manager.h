#pragma once

#include <QPointer>

#include <functional>
#include <map>
#include <memory>
#include <string>

#include "ai_capability_registry.h"
#include "ai_model_manager.h"
#include "inference_scheduler.h"
#include "../session/demux_thread_shared.h"

class DetectorThread;
class WhisperThread;

class AiPipelineManager
{
public:
    AiPipelineManager(DemuxThreadState& state, SessionResources& resources,
        std::function<void(bool)> updateLiveRuntimeTuning);
    ~AiPipelineManager();

    bool SetCapabilityEnabled(const AiFeatureConfig& config, bool enable, std::string* error = nullptr);
    bool SetCapabilityEnabled(AiCapability capability, bool enable, std::string* error = nullptr);
    bool IsCapabilityEnabled(AiCapability capability) const;
    StreamEpoch GetCapabilityEpoch(AiCapability capability) const;

    bool EnableWhisper(const std::string& modelPath, std::string* error = nullptr);
    void DisableWhisper();

    bool EnableDetector(const std::string& modelPath, const std::string& labelsPath,
        std::string* error = nullptr);
    void DisableDetector();

    void DisableAll();
    void RebindEpoch(const StreamEpoch& epoch);
    void RebindEpochLocked(const StreamEpoch& epoch);
    void UpdateRuntimeHints(
        AiPriorityTier priorityTier,
        bool focusRoute,
        bool alarmRoute,
        bool fullscreenRoute,
        int detectorMinimumSkipFrames);
    AiModelRecord GetModelRecord(AiCapability capability) const;

    QPointer<WhisperThread> GetWhisperThread() const;
    StreamEpoch GetWhisperEpoch() const;
    QPointer<DetectorThread> GetDetectorThread() const;

    const AiModelManager& Models() const { return modelManager_; }

private:
    AiSessionContext BuildSessionContext(const StreamEpoch* epochOverride = nullptr) const;
    const AiCapabilityDescriptor* FindDescriptor(AiCapability capability) const;
    IAiCapabilityAdapter* FindAdapter(AiCapability capability);
    const IAiCapabilityAdapter* FindAdapter(AiCapability capability) const;

    DemuxThreadState& state_;
    SessionResources& resources_;
    std::function<void(bool)> updateLiveRuntimeTuning_;
    AiModelManager modelManager_;
    InferenceScheduler* inferenceScheduler_ = nullptr;
    AiCapabilityRegistry capabilityRegistry_;
    std::map<AiCapability, std::unique_ptr<IAiCapabilityAdapter>> ownerAdapters_;
};
