

#include "detector_capability_adapter.h"

#include "../session/demux_thread_shared.h"
#include "../video/video_thread.h"
#include "../../features/detector/detector_thread.h"

#include <utility>

DetectorCapabilityAdapter::DetectorCapabilityAdapter(
    SessionResources& resources,
    InferenceScheduler* scheduler,
    std::function<void(bool)> updateLiveRuntimeTuning)
    : resources_(resources)
    , scheduler_(scheduler)
    , updateLiveRuntimeTuning_(std::move(updateLiveRuntimeTuning))
{
}

bool DetectorCapabilityAdapter::SetEnabled(
    AiCapability capability,
    bool enable,
    const AiSessionContext& context,
    AiModelManager& modelManager,
    std::string& error)
{
    if (capability != AiCapability::Detector)
    {
        error = "detector adapter does not own requested capability";
        return false;
    }

    if (!enable)
    {
        Disable();
        modelManager.MarkDisabled(AiCapability::Detector);
        error.clear();
        return true;
    }

    const AiModelRecord detectorRecord = modelManager.GetRecord(AiCapability::Detector);
    if (!detectorRecord.configured || detectorRecord.config.modelPath.empty())
    {
        error = "Detector model not configured";
        modelManager.MarkLoadFailure(AiCapability::Detector, error);
        return false;
    }

    if (IsEnabled(AiCapability::Detector) && !MatchesConfig(detectorRecord.config))
        Disable();

    std::string localError;
    const bool ok = Enable(context, detectorRecord.config, localError);
    if (ok)
    {
        modelManager.MarkActive(AiCapability::Detector, IsModelLoaded(), GetBackendSummary());
        error.clear();
        return true;
    }

    modelManager.MarkLoadFailure(AiCapability::Detector, localError);
    error = localError;
    return false;
}

void DetectorCapabilityAdapter::Rebind(const AiSessionContext& context)
{
    std::lock_guard<std::mutex> lock(resources_.mux);
    RebindLocked(context);
}

void DetectorCapabilityAdapter::RebindLocked(const AiSessionContext& context)
{
    if (!resources_.det)
        return;

    resources_.det->SetInferenceScheduler(scheduler_);
    resources_.det->SetSessionContext(context);
    resources_.det->SetMediaEpoch(context.epoch);
    if (resources_.vt)
        resources_.vt->SetDetectorThread(resources_.det);
    if (updateLiveRuntimeTuning_)
        updateLiveRuntimeTuning_(true);
}

bool DetectorCapabilityAdapter::IsEnabled(AiCapability capability) const
{
    if (capability != AiCapability::Detector)
        return false;

    std::lock_guard<std::mutex> lock(resources_.mux);
    return resources_.det != nullptr;
}

StreamEpoch DetectorCapabilityAdapter::GetEpoch(AiCapability capability) const
{
    if (capability != AiCapability::Detector)
        return StreamEpoch{};

    std::lock_guard<std::mutex> lock(resources_.mux);
    return resources_.det ? resources_.det->GetMediaEpoch() : StreamEpoch{};
}

QPointer<DetectorThread> DetectorCapabilityAdapter::GetThread() const
{
    std::lock_guard<std::mutex> lock(resources_.mux);
    return QPointer<DetectorThread>(resources_.det);
}

bool DetectorCapabilityAdapter::Enable(
    const AiSessionContext& context,
    const AiFeatureConfig& config,
    std::string& error)
{
    std::lock_guard<std::mutex> lock(resources_.mux);
    if (!resources_.det)
    {
        DetectorThread* det = new DetectorThread();
        det->SetInferenceScheduler(scheduler_);
        det->SetSessionContext(context);
        det->SetModelConfig(config.modelPath, config.auxModelPath);
        det->SetMediaEpoch(context.epoch);
        det->start();
        resources_.det = det;
    }
    else
    {
        resources_.det->SetInferenceScheduler(scheduler_);
        resources_.det->SetSessionContext(context);
        resources_.det->SetModelConfig(config.modelPath, config.auxModelPath);
        resources_.det->SetMediaEpoch(context.epoch);
    }

    if (!resources_.det)
    {
        error = "Failed to create detector thread";
        return false;
    }

    if (resources_.vt)
        resources_.vt->SetDetectorThread(resources_.det);
    if (updateLiveRuntimeTuning_)
        updateLiveRuntimeTuning_(true);
    activeConfig_ = config;
    hasActiveConfig_ = true;
    return true;
}

void DetectorCapabilityAdapter::Disable()
{
    DetectorThread* oldDet = nullptr;
    {
        std::lock_guard<std::mutex> lock(resources_.mux);
        oldDet = resources_.det;
        resources_.det = nullptr;
        hasActiveConfig_ = false;
        if (resources_.vt)
            resources_.vt->SetDetectorThread(nullptr);
        if (updateLiveRuntimeTuning_)
            updateLiveRuntimeTuning_(true);
    }

    if (oldDet)
    {
        oldDet->Stop();
        delete oldDet;
    }
}

bool DetectorCapabilityAdapter::MatchesConfig(const AiFeatureConfig& config) const
{
    std::lock_guard<std::mutex> lock(resources_.mux);
    return resources_.det != nullptr
        && hasActiveConfig_
        && activeConfig_ == config;
}

bool DetectorCapabilityAdapter::IsModelLoaded() const
{
    std::lock_guard<std::mutex> lock(resources_.mux);
    return resources_.det && resources_.det->IsModelLoaded();
}

std::string DetectorCapabilityAdapter::GetBackendSummary() const
{
    std::lock_guard<std::mutex> lock(resources_.mux);
    return resources_.det ? resources_.det->GetBackendSummary().toStdString() : std::string();
}
