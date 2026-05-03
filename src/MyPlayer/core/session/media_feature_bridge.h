#pragma once

#include <QPointer>

#include <functional>
#include <memory>
#include <string>

#include "demux_thread_shared.h"
#include "../ai/ai_model_manager.h"
#include "../media/demux_types.h"

class AiPipelineManager;

class MediaFeatureBridge
{
public:
    MediaFeatureBridge(DemuxThreadState& state, SessionResources& resources,
        std::function<void(bool)> updateLiveRuntimeTuning);
    ~MediaFeatureBridge();

    void DisableAll();
    void RebindEpoch(const StreamEpoch& epoch);
    void RebindEpochLocked(const StreamEpoch& epoch);
    void UpdateRuntimeHints(
        AiPriorityTier priorityTier,
        bool focusRoute,
        bool alarmRoute,
        bool fullscreenRoute,
        int detectorMinimumSkipFrames);
    bool SetCapabilityEnabled(const AiFeatureConfig& config, bool enable, std::string* error = nullptr);
    bool SetCapabilityEnabled(AiCapability capability, bool enable, std::string* error = nullptr);
    bool IsCapabilityEnabled(AiCapability capability) const;
    AiModelRecord GetModelRecord(AiCapability capability) const;
    StreamEpoch GetCapabilityEpoch(AiCapability capability) const;
    void EnableWhisper(bool enable, const std::string& modelPath);
    QPointer<WhisperThread> GetWhisperThread() const;
    StreamEpoch GetWhisperEpoch() const;

    void EnableDetector(bool enable, const std::string& modelPath, const std::string& labelsPath = "");
    QPointer<DetectorThread> GetDetectorThread() const;

private:
    DemuxThreadState& state;
    SessionResources& resources;
    std::function<void(bool)> updateLiveRuntimeTuning;
    std::unique_ptr<AiPipelineManager> pipelineManager_;
};
