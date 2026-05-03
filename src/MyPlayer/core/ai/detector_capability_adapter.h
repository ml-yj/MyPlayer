#pragma once

#include <QPointer>

#include <functional>

#include "ai_capability_registry.h"

struct SessionResources;
class InferenceScheduler;
class DetectorThread;

class DetectorCapabilityAdapter final : public IAiCapabilityAdapter
{
public:
    DetectorCapabilityAdapter(
        SessionResources& resources,
        InferenceScheduler* scheduler,
        std::function<void(bool)> updateLiveRuntimeTuning);

    AiCapability OwnerCapability() const override { return AiCapability::Detector; }
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

    QPointer<DetectorThread> GetThread() const;

private:
    bool Enable(const AiSessionContext& context, const AiFeatureConfig& config, std::string& error);
    void Disable();
    bool MatchesConfig(const AiFeatureConfig& config) const;
    bool IsModelLoaded() const;
    std::string GetBackendSummary() const;

    SessionResources& resources_;
    InferenceScheduler* scheduler_ = nullptr;
    std::function<void(bool)> updateLiveRuntimeTuning_;
    AiFeatureConfig activeConfig_;
    bool hasActiveConfig_ = false;
};
