

#include "ai_pipeline_manager.h"

#include "detector_capability_adapter.h"
#include "whisper_capability_adapter.h"

#include <algorithm>
#include <utility>

namespace
{
InferenceScheduler& SharedInferenceScheduler()
{
    static InferenceScheduler scheduler;
    return scheduler;
}
}

AiPipelineManager::AiPipelineManager(DemuxThreadState& state, SessionResources& resources,
    std::function<void(bool)> updateLiveRuntimeTuning)
    : state_(state)
    , resources_(resources)
    , updateLiveRuntimeTuning_(std::move(updateLiveRuntimeTuning))
{
    inferenceScheduler_ = &SharedInferenceScheduler();
    inferenceScheduler_->RegisterState(&state_);

    const AiCapabilityFactoryContext factoryContext{
        resources_,
        inferenceScheduler_,
        updateLiveRuntimeTuning_
    };
    capabilityRegistry_ = CreateDefaultAiCapabilityRegistry(factoryContext);

    for (const AiCapability ownerCapability : capabilityRegistry_.OwnerCapabilities())
    {
        const AiCapabilityAdapterFactory* factory = capabilityRegistry_.FindFactory(ownerCapability);
        if (!factory)
            continue;

        std::unique_ptr<IAiCapabilityAdapter> adapter = (*factory)();
        if (adapter)
            ownerAdapters_.emplace(ownerCapability, std::move(adapter));
    }
}

AiPipelineManager::~AiPipelineManager()
{
    SharedInferenceScheduler().UnregisterState(&state_);
}

bool AiPipelineManager::SetCapabilityEnabled(const AiFeatureConfig& config, bool enable, std::string* error)
{
    modelManager_.Configure(config);
    return SetCapabilityEnabled(config.capability, enable, error);
}

bool AiPipelineManager::SetCapabilityEnabled(AiCapability capability, bool enable, std::string* error)
{
    const AiCapabilityDescriptor* descriptor = FindDescriptor(capability);
    if (!descriptor)
    {
        if (error)
            *error = std::string("Unsupported AI capability: ") + AiCapabilityName(capability);
        return false;
    }

    if (!descriptor->directEnableAllowed)
    {
        const std::string localError = std::string(AiCapabilityName(capability))
            + " is a managed dependency of "
            + AiCapabilityName(descriptor->ownerCapability);
        if (error)
            *error = localError;
        return false;
    }

    IAiCapabilityAdapter* adapter = FindAdapter(capability);
    if (!adapter)
    {
        const std::string localError = std::string("No adapter registered for AI capability: ")
            + AiCapabilityName(capability);
        if (error)
            *error = localError;
        return false;
    }

    std::string localError;
    const bool ok = adapter->SetEnabled(capability, enable, BuildSessionContext(), modelManager_, localError);
    if (error)
        *error = localError;
    return ok;
}

bool AiPipelineManager::IsCapabilityEnabled(AiCapability capability) const
{
    const IAiCapabilityAdapter* adapter = FindAdapter(capability);
    return adapter ? adapter->IsEnabled(capability) : false;
}

StreamEpoch AiPipelineManager::GetCapabilityEpoch(AiCapability capability) const
{
    const IAiCapabilityAdapter* adapter = FindAdapter(capability);
    return adapter ? adapter->GetEpoch(capability) : StreamEpoch{};
}

const AiCapabilityDescriptor* AiPipelineManager::FindDescriptor(AiCapability capability) const
{
    return capabilityRegistry_.Find(capability);
}

IAiCapabilityAdapter* AiPipelineManager::FindAdapter(AiCapability capability)
{
    const AiCapabilityDescriptor* descriptor = FindDescriptor(capability);
    if (!descriptor)
        return nullptr;

    const auto it = ownerAdapters_.find(descriptor->ownerCapability);
    return it != ownerAdapters_.end() ? it->second.get() : nullptr;
}

const IAiCapabilityAdapter* AiPipelineManager::FindAdapter(AiCapability capability) const
{
    const AiCapabilityDescriptor* descriptor = FindDescriptor(capability);
    if (!descriptor)
        return nullptr;

    const auto it = ownerAdapters_.find(descriptor->ownerCapability);
    return it != ownerAdapters_.end() ? it->second.get() : nullptr;
}

bool AiPipelineManager::EnableWhisper(const std::string& modelPath, std::string* error)
{
    AiFeatureConfig config;
    config.capability = AiCapability::Asr;
    config.modelPath = modelPath;
    config.preferGpu = true;
    config.allowCpuFallback = true;
    return SetCapabilityEnabled(config, true, error);
}

void AiPipelineManager::DisableWhisper()
{
    SetCapabilityEnabled(AiCapability::Asr, false, nullptr);
}

bool AiPipelineManager::EnableDetector(const std::string& modelPath, const std::string& labelsPath,
    std::string* error)
{
    AiFeatureConfig config;
    config.capability = AiCapability::Detector;
    config.modelPath = modelPath;
    config.auxModelPath = labelsPath;
    config.preferGpu = true;
    config.allowCpuFallback = true;
    return SetCapabilityEnabled(config, true, error);
}

void AiPipelineManager::DisableDetector()
{
    SetCapabilityEnabled(AiCapability::Detector, false, nullptr);
}

void AiPipelineManager::DisableAll()
{
    for (const AiCapability ownerCapability : capabilityRegistry_.OwnerCapabilities())
        SetCapabilityEnabled(ownerCapability, false, nullptr);
}

void AiPipelineManager::RebindEpoch(const StreamEpoch& epoch)
{
    std::lock_guard<std::mutex> lock(resources_.mux);
    RebindEpochLocked(epoch);
}

void AiPipelineManager::RebindEpochLocked(const StreamEpoch& epoch)
{
    const AiSessionContext context = BuildSessionContext(&epoch);
    for (const AiCapability ownerCapability : capabilityRegistry_.OwnerCapabilities())
    {
        if (IAiCapabilityAdapter* adapter = FindAdapter(ownerCapability))
            adapter->RebindLocked(context);
    }
}

void AiPipelineManager::UpdateRuntimeHints(
    AiPriorityTier priorityTier,
    bool focusRoute,
    bool alarmRoute,
    bool fullscreenRoute,
    int detectorMinimumSkipFrames)
{
    state_.aiPriorityTier.store(static_cast<int>(priorityTier));
    state_.aiFocusRoute.store(focusRoute);
    state_.aiAlarmRoute.store(alarmRoute);
    state_.aiFullscreenRoute.store(fullscreenRoute);
    state_.aiDetectorMinimumSkipFrames.store(std::max(0, detectorMinimumSkipFrames));
    RebindEpoch(StreamEpoch{ state_.generation.load(), state_.serial.load() });
}

AiModelRecord AiPipelineManager::GetModelRecord(AiCapability capability) const
{
    return modelManager_.GetRecord(capability);
}

QPointer<WhisperThread> AiPipelineManager::GetWhisperThread() const
{
    const auto* adapter = dynamic_cast<const WhisperCapabilityAdapter*>(FindAdapter(AiCapability::Asr));
    return adapter ? adapter->GetThread() : QPointer<WhisperThread>();
}

StreamEpoch AiPipelineManager::GetWhisperEpoch() const
{
    return GetCapabilityEpoch(AiCapability::Asr);
}

QPointer<DetectorThread> AiPipelineManager::GetDetectorThread() const
{
    const auto* adapter = dynamic_cast<const DetectorCapabilityAdapter*>(FindAdapter(AiCapability::Detector));
    return adapter ? adapter->GetThread() : QPointer<DetectorThread>();
}

AiSessionContext AiPipelineManager::BuildSessionContext(const StreamEpoch* epochOverride) const
{
    AiSessionContext context;
    context.epoch = epochOverride
        ? *epochOverride
        : StreamEpoch{ state_.generation.load(), state_.serial.load() };
    context.playbackKind = state_.playbackKind.load();
    context.live = state_.isLiveStream.load();
    context.lowLatency = state_.activeOpenOptions.enableLowLatency;
    context.priorityTier = static_cast<AiPriorityTier>(state_.aiPriorityTier.load());
    context.focusRoute = state_.aiFocusRoute.load();
    context.alarmRoute = state_.aiAlarmRoute.load();
    context.fullscreenRoute = state_.aiFullscreenRoute.load();
    context.detectorMinimumSkipFrames = state_.aiDetectorMinimumSkipFrames.load();
    return context;
}
