

#include "whisper_capability_adapter.h"

#include "../audio/audio_thread.h"
#include "../session/demux_thread_shared.h"
#include "../../features/asr/whisper_thread.h"

#include <filesystem>

namespace
{

AiFeatureConfig ResolveVadConfigForWhisper(const std::string& whisperModelPath)
{
    AiFeatureConfig config;
    config.capability = AiCapability::Vad;
    config.preferGpu = false;
    config.allowCpuFallback = true;

    try
    {
        const std::filesystem::path whisperPath(whisperModelPath);
        const std::filesystem::path vadPath = whisperPath.parent_path() / "silero_vad.onnx";
        if (std::filesystem::exists(vadPath))
            config.modelPath = vadPath.string();
    }
    catch (...)
    {
    }

    return config;
}
}

WhisperCapabilityAdapter::WhisperCapabilityAdapter(SessionResources& resources, InferenceScheduler* scheduler)
    : resources_(resources)
    , scheduler_(scheduler)
{
}

bool WhisperCapabilityAdapter::SetEnabled(
    AiCapability capability,
    bool enable,
    const AiSessionContext& context,
    AiModelManager& modelManager,
    std::string& error)
{
    if (capability != AiCapability::Asr && capability != AiCapability::Vad)
    {
        error = "asr adapter does not own requested capability";
        return false;
    }

    if (capability == AiCapability::Vad)
    {
        error = "vad is managed by asr capability";
        return false;
    }

    if (!enable)
    {
        Disable();
        modelManager.MarkDisabled(AiCapability::Asr);
        modelManager.MarkDisabled(AiCapability::Vad);
        error.clear();
        return true;
    }

    const AiModelRecord asrRecord = modelManager.GetRecord(AiCapability::Asr);
    if (!asrRecord.configured || asrRecord.config.modelPath.empty())
    {
        error = "ASR model not configured";
        modelManager.MarkLoadFailure(AiCapability::Asr, error);
        modelManager.MarkDisabled(AiCapability::Vad);
        return false;
    }

    AiFeatureConfig vadConfig = modelManager.HasConfig(AiCapability::Vad)
        ? modelManager.GetRecord(AiCapability::Vad).config
        : ResolveVadConfigForWhisper(asrRecord.config.modelPath);
    vadConfig.capability = AiCapability::Vad;
    if (!modelManager.HasConfig(AiCapability::Vad))
        modelManager.Configure(vadConfig);

    if (IsEnabled(AiCapability::Asr) && !MatchesConfig(asrRecord.config, vadConfig))
        Disable();

    std::string localError;
    const bool ok = Enable(context, asrRecord.config, vadConfig, localError);
    if (ok)
    {
        modelManager.MarkActive(AiCapability::Asr, IsModelLoaded(), GetBackendSummary());
        modelManager.MarkActive(AiCapability::Vad, IsVadModelLoaded(), GetVadBackendSummary());
        error.clear();
        return true;
    }

    modelManager.MarkLoadFailure(AiCapability::Asr, localError);
    modelManager.MarkDisabled(AiCapability::Vad);
    error = localError;
    return false;
}

void WhisperCapabilityAdapter::Rebind(const AiSessionContext& context)
{
    std::lock_guard<std::mutex> lock(resources_.mux);
    RebindLocked(context);
}

void WhisperCapabilityAdapter::RebindLocked(const AiSessionContext& context)
{
    if (!resources_.wt)
        return;

    resources_.wt->SetInferenceScheduler(scheduler_);
    resources_.wt->SetSessionContext(context);
    resources_.wt->SetMediaEpoch(context.epoch);
    if (resources_.at)
        resources_.at->SetWhisperThread(resources_.wt);
}

bool WhisperCapabilityAdapter::IsEnabled(AiCapability capability) const
{
    if (capability != AiCapability::Asr && capability != AiCapability::Vad)
        return false;

    std::lock_guard<std::mutex> lock(resources_.mux);
    return resources_.wt != nullptr;
}

StreamEpoch WhisperCapabilityAdapter::GetEpoch(AiCapability capability) const
{
    if (capability != AiCapability::Asr && capability != AiCapability::Vad)
        return StreamEpoch{};

    std::lock_guard<std::mutex> lock(resources_.mux);
    return resources_.wt ? resources_.wt->GetEpoch() : StreamEpoch{};
}

QPointer<WhisperThread> WhisperCapabilityAdapter::GetThread() const
{
    std::lock_guard<std::mutex> lock(resources_.mux);
    return QPointer<WhisperThread>(resources_.wt);
}

bool WhisperCapabilityAdapter::Enable(
    const AiSessionContext& context,
    const AiFeatureConfig& asrConfig,
    const AiFeatureConfig& vadConfig,
    std::string& error)
{
    std::lock_guard<std::mutex> lock(resources_.mux);
    if (!resources_.wt)
    {
        WhisperThread* wt = new WhisperThread();
        wt->SetInferenceScheduler(scheduler_);
        wt->SetSessionContext(context);
        wt->SetVadConfig(true, std::filesystem::path(vadConfig.modelPath).wstring());
        if (!wt->LoadModel(asrConfig.modelPath))
        {
            delete wt;
            error = "Failed to load Whisper model";
            return false;
        }
        wt->SetMediaEpoch(context.epoch);
        wt->start();
        resources_.wt = wt;
    }
    else
    {
        resources_.wt->SetInferenceScheduler(scheduler_);
        resources_.wt->SetSessionContext(context);
        resources_.wt->SetVadConfig(true, std::filesystem::path(vadConfig.modelPath).wstring());
        resources_.wt->SetMediaEpoch(context.epoch);
    }

    if (resources_.at)
        resources_.at->SetWhisperThread(resources_.wt);
    activeAsrConfig_ = asrConfig;
    activeVadConfig_ = vadConfig;
    hasActiveConfig_ = true;
    return resources_.wt != nullptr;
}

void WhisperCapabilityAdapter::Disable()
{
    WhisperThread* oldWt = nullptr;
    {
        std::lock_guard<std::mutex> lock(resources_.mux);
        oldWt = resources_.wt;
        resources_.wt = nullptr;
        hasActiveConfig_ = false;
        if (resources_.at)
            resources_.at->SetWhisperThread(nullptr);
    }

    if (oldWt)
    {
        oldWt->Stop();
        delete oldWt;
    }
}

bool WhisperCapabilityAdapter::MatchesConfig(
    const AiFeatureConfig& asrConfig,
    const AiFeatureConfig& vadConfig) const
{
    std::lock_guard<std::mutex> lock(resources_.mux);
    return resources_.wt != nullptr
        && hasActiveConfig_
        && activeAsrConfig_ == asrConfig
        && activeVadConfig_ == vadConfig;
}

bool WhisperCapabilityAdapter::IsModelLoaded() const
{
    std::lock_guard<std::mutex> lock(resources_.mux);
    return resources_.wt && resources_.wt->IsModelLoaded();
}

bool WhisperCapabilityAdapter::IsVadModelLoaded() const
{
    std::lock_guard<std::mutex> lock(resources_.mux);
    return resources_.wt && resources_.wt->IsVadModelLoaded();
}

std::string WhisperCapabilityAdapter::GetBackendSummary() const
{
    std::lock_guard<std::mutex> lock(resources_.mux);
    return resources_.wt ? resources_.wt->GetBackendSummary().toStdString() : std::string();
}

std::string WhisperCapabilityAdapter::GetVadBackendSummary() const
{
    std::lock_guard<std::mutex> lock(resources_.mux);
    return resources_.wt ? resources_.wt->GetVadBackendSummary().toStdString() : std::string("off");
}
