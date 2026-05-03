

#include "ai_model_manager.h"

void AiModelManager::Configure(const AiFeatureConfig& config)
{
    std::lock_guard<std::mutex> lock(mux_);
    AiModelRecord& record = records_[config.capability];
    record.config = config;
    record.configured = true;
}

bool AiModelManager::HasConfig(AiCapability capability) const
{
    std::lock_guard<std::mutex> lock(mux_);
    const auto it = records_.find(capability);
    return it != records_.end() && it->second.configured;
}

bool AiModelManager::HasMatchingConfig(AiCapability capability, const AiFeatureConfig& config) const
{
    std::lock_guard<std::mutex> lock(mux_);
    const auto it = records_.find(capability);
    return it != records_.end() && it->second.configured && it->second.config == config;
}

AiModelRecord AiModelManager::GetRecord(AiCapability capability) const
{
    std::lock_guard<std::mutex> lock(mux_);
    const auto it = records_.find(capability);
    if (it != records_.end())
        return it->second;

    AiModelRecord record;
    record.config.capability = capability;
    return record;
}

void AiModelManager::MarkActive(AiCapability capability, bool loaded, const std::string& backendSummary)
{
    std::lock_guard<std::mutex> lock(mux_);
    AiModelRecord& record = records_[capability];
    record.config.capability = capability;
    record.configured = true;
    record.active = true;
    record.loaded = loaded;
    record.backendSummary = backendSummary;
    record.lastError.clear();
}

void AiModelManager::MarkLoadFailure(AiCapability capability, const std::string& error)
{
    std::lock_guard<std::mutex> lock(mux_);
    AiModelRecord& record = records_[capability];
    record.config.capability = capability;
    record.configured = true;
    record.active = false;
    record.loaded = false;
    record.backendSummary.clear();
    record.lastError = error;
}

void AiModelManager::MarkDisabled(AiCapability capability)
{
    std::lock_guard<std::mutex> lock(mux_);
    AiModelRecord& record = records_[capability];
    record.config.capability = capability;
    record.active = false;
    record.loaded = false;
    record.backendSummary.clear();
    record.lastError.clear();
}
