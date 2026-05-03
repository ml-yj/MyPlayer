
#pragma once

#include <string>

#include "../media/demux_types.h"
#include "../session/stream_config.h"

enum class AiCapability
{
    Asr,
    Vad,
    Detector,
    Ocr,
    Segmentation,
    OpenVocabularyDetection,
};

enum class AiPriorityTier
{
    Background = 0,
    Candidate = 1,
    Focused = 2,
    Alarm = 3,
    Fullscreen = 4,
};

struct AiSessionContext
{
    StreamEpoch epoch;
    StreamPlaybackKind playbackKind = StreamPlaybackKind::File;
    bool live = false;
    bool lowLatency = false;

    AiPriorityTier priorityTier = AiPriorityTier::Candidate;

    bool focusRoute = false;
    bool alarmRoute = false;
    bool fullscreenRoute = false;

    int detectorMinimumSkipFrames = 0;
};

struct AiFeatureConfig
{
    AiCapability capability = AiCapability::Asr;
    std::string modelPath;
    std::string auxModelPath;
    bool preferGpu = true;
    bool allowCpuFallback = true;
};

inline bool operator==(const AiFeatureConfig& lhs, const AiFeatureConfig& rhs)
{

    return lhs.capability == rhs.capability
        && lhs.modelPath == rhs.modelPath
        && lhs.auxModelPath == rhs.auxModelPath
        && lhs.preferGpu == rhs.preferGpu
        && lhs.allowCpuFallback == rhs.allowCpuFallback;
}

inline bool operator!=(const AiFeatureConfig& lhs, const AiFeatureConfig& rhs)
{
    return !(lhs == rhs);
}

inline const char* AiCapabilityName(AiCapability capability)
{
    switch (capability)
    {
    case AiCapability::Asr:                    return "asr";
    case AiCapability::Vad:                    return "vad";
    case AiCapability::Detector:               return "detector";
    case AiCapability::Ocr:                    return "ocr";
    case AiCapability::Segmentation:           return "segmentation";
    case AiCapability::OpenVocabularyDetection:return "open-vocabulary-detection";
    }
    return "unknown";
}

inline const char* AiPriorityTierName(AiPriorityTier tier)
{
    switch (tier)
    {
    case AiPriorityTier::Background: return "background";
    case AiPriorityTier::Candidate:  return "candidate";
    case AiPriorityTier::Focused:    return "focused";
    case AiPriorityTier::Alarm:      return "alarm";
    case AiPriorityTier::Fullscreen: return "fullscreen";
    }
    return "candidate";
}
