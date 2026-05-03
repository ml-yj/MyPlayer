#pragma once

#include <QPointer>

#include "ai_capability_registry.h"

struct SessionResources;
class InferenceScheduler;
class WhisperThread;

class WhisperCapabilityAdapter final : public IAiCapabilityAdapter
{
public:
    WhisperCapabilityAdapter(SessionResources& resources, InferenceScheduler* scheduler);

    AiCapability OwnerCapability() const override { return AiCapability::Asr; }
    bool SetEnabled(
        AiCapability capability,
        bool enable,
        const AiSessionContext& context,
        AiModelManager& modelManager,
        std::string& error) override;
    void Rebind(const AiSessionContext& context) override;
    void RebindLocked(const AiSessionContext& context) override;
    bool IsEnabled(AiCapability capability) const override;
    StreamEpoch GetEpoch(AiCapability capability) const override;

    QPointer<WhisperThread> GetThread() const;

private:
    bool Enable(
        const AiSessionContext& context,
        const AiFeatureConfig& asrConfig,
        const AiFeatureConfig& vadConfig,
        std::string& error);
    void Disable();
    bool MatchesConfig(const AiFeatureConfig& asrConfig, const AiFeatureConfig& vadConfig) const;
    bool IsModelLoaded() const;
    bool IsVadModelLoaded() const;
    std::string GetBackendSummary() const;
    std::string GetVadBackendSummary() const;

    SessionResources& resources_;
    InferenceScheduler* scheduler_ = nullptr;
    AiFeatureConfig activeAsrConfig_;
    AiFeatureConfig activeVadConfig_;
    bool hasActiveConfig_ = false;
};
