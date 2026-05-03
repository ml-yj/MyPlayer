

#include "ai_capability_registry.h"

#include "detector_capability_adapter.h"
#include "whisper_capability_adapter.h"

#include <utility>

void AiCapabilityRegistry::RegisterOwner(
    AiCapability capability,
    AiCapabilityAdapterFactory factory,
    bool directEnableAllowed)
{
    descriptors_[capability] = AiCapabilityDescriptor{
        capability,
        capability,
        directEnableAllowed
    };
    ownerFactories_[capability] = std::move(factory);
}

void AiCapabilityRegistry::RegisterAlias(
    AiCapability capability,
    AiCapability ownerCapability,
    bool directEnableAllowed)
{
    descriptors_[capability] = AiCapabilityDescriptor{
        capability,
        ownerCapability,
        directEnableAllowed
    };
}

const AiCapabilityDescriptor* AiCapabilityRegistry::Find(AiCapability capability) const
{
    const auto it = descriptors_.find(capability);
    return it != descriptors_.end() ? &it->second : nullptr;
}

const AiCapabilityAdapterFactory* AiCapabilityRegistry::FindFactory(AiCapability ownerCapability) const
{
    const auto it = ownerFactories_.find(ownerCapability);
    return it != ownerFactories_.end() ? &it->second : nullptr;
}

std::vector<AiCapability> AiCapabilityRegistry::OwnerCapabilities() const
{
    std::vector<AiCapability> owners;
    owners.reserve(ownerFactories_.size());
    for (const auto& [capability, _] : ownerFactories_)
        owners.push_back(capability);
    return owners;
}

AiCapabilityRegistry CreateDefaultAiCapabilityRegistry(const AiCapabilityFactoryContext& context)
{
    AiCapabilityRegistry registry;
    SessionResources* resources = &context.resources;
    InferenceScheduler* scheduler = context.scheduler;
    std::function<void(bool)> updateLiveRuntimeTuning = context.updateLiveRuntimeTuning;

    registry.RegisterOwner(
        AiCapability::Asr,
        [resources, scheduler]()
        {
            return std::make_unique<WhisperCapabilityAdapter>(*resources, scheduler);
        });
    registry.RegisterAlias(AiCapability::Vad, AiCapability::Asr, false);

    registry.RegisterOwner(
        AiCapability::Detector,
        [resources, scheduler, updateLiveRuntimeTuning]()
        {
            return std::make_unique<DetectorCapabilityAdapter>(
                *resources,
                scheduler,
                updateLiveRuntimeTuning);
        });

    return registry;
}
