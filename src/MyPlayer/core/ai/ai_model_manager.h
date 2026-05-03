#pragma once

#include <map>
#include <mutex>
#include <string>

#include "ai_types.h"

struct AiModelRecord
{
    AiFeatureConfig config;
    bool configured = false;
    bool active = false;
    bool loaded = false;
    std::string backendSummary;
    std::string lastError;
};

class AiModelManager
{
public:
    void Configure(const AiFeatureConfig& config);
    bool HasConfig(AiCapability capability) const;
    bool HasMatchingConfig(AiCapability capability, const AiFeatureConfig& config) const;
    AiModelRecord GetRecord(AiCapability capability) const;
    void MarkActive(AiCapability capability, bool loaded, const std::string& backendSummary);
    void MarkLoadFailure(AiCapability capability, const std::string& error);
    void MarkDisabled(AiCapability capability);

private:
    mutable std::mutex mux_;
    std::map<AiCapability, AiModelRecord> records_;
};
