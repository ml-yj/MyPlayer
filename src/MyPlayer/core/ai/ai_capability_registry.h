#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "ai_model_manager.h"

struct SessionResources;
class InferenceScheduler;

class IAiCapabilityAdapter
{
public:
    virtual ~IAiCapabilityAdapter() = default;

    virtual AiCapability OwnerCapability() const = 0;
    virtual bool SetEnabled(
        AiCapability capability,
        bool enable,
        const AiSessionContext& context,
        AiModelManager& modelManager,
        std::string& error) = 0;
    virtual void Rebind(const AiSessionContext& context) = 0;
    virtual void RebindLocked(const AiSessionContext& context) = 0;
    virtual bool IsEnabled(AiCapability capability) const = 0;
    virtual StreamEpoch GetEpoch(AiCapability capability) const = 0;
};

using AiCapabilityAdapterFactory = std::function<std::unique_ptr<IAiCapabilityAdapter>()>;

struct AiCapabilityDescriptor
{
    AiCapability capability = AiCapability::Asr;
    AiCapability ownerCapability = AiCapability::Asr;
    bool directEnableAllowed = true;
};

struct AiCapabilityFactoryContext
{
    SessionResources& resources;
    InferenceScheduler* scheduler = nullptr;
    std::function<void(bool)> updateLiveRuntimeTuning;
};

class AiCapabilityRegistry
{
public:
    void RegisterOwner(
        AiCapability capability,
        AiCapabilityAdapterFactory factory,
        bool directEnableAllowed = true);
    void RegisterAlias(
        AiCapability capability,
        AiCapability ownerCapability,
        bool directEnableAllowed = false);

    const AiCapabilityDescriptor* Find(AiCapability capability) const;
    const AiCapabilityAdapterFactory* FindFactory(AiCapability ownerCapability) const;
    std::vector<AiCapability> OwnerCapabilities() const;

private:
    std::map<AiCapability, AiCapabilityDescriptor> descriptors_;
    std::map<AiCapability, AiCapabilityAdapterFactory> ownerFactories_;
};

AiCapabilityRegistry CreateDefaultAiCapabilityRegistry(const AiCapabilityFactoryContext& context);
