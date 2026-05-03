#pragma once

#include <QtGlobal>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

#include "stream_config.h"

class Demux;
class VideoThread;
class AudioThread;
class WhisperThread;
class DetectorThread;
class VideoCallback;
class SessionRecorder;

struct DemuxThreadState
{
    std::atomic<bool> isExit{ false };
    std::atomic<quint64> generation{ 1 };
    std::atomic<quint64> serial{ 1 };
    std::atomic<long long> pts{ 0 };
    std::atomic<long long> totalMs{ 0 };
    std::atomic<bool> isPause{ false };
    std::atomic<bool> isComplete{ false };
    std::atomic<bool> hasError{ false };
    std::atomic<bool> isLiveStream{ false };
    std::atomic<bool> isBuffering{ false };
    std::atomic<bool> primingPending{ false };
    std::atomic<bool> liveStartupAudioResyncPending{ false };
    std::atomic<bool> audioPlaybackAvailable{ false };

    std::string lastError;
    std::string currentUrl;

    std::atomic<int> videoWidth{ 0 };
    std::atomic<int> videoHeight{ 0 };
    std::atomic<int> videoFpsNum{ 0 };
    std::atomic<int> videoFpsDen{ 1 };
    std::atomic<long long> bitrate{ 0 };
    std::atomic<int> audioSampleRate{ 0 };
    std::atomic<int> audioChannels{ 0 };

    int eofCount = 0;
    int reconnectCount = 0;
    int maxReconnect = 5;
    StreamOpenOptions fileOpenOptions = StreamOpenOptions::DefaultFile();
    StreamOpenOptions networkOpenOptions = StreamOpenOptions::LowLatencyNetwork();
    StreamOpenOptions activeOpenOptions = StreamOpenOptions::DefaultFile();
    std::atomic<StreamPlaybackKind> playbackKind{ StreamPlaybackKind::File };
    std::atomic<StreamSourceType> sourceType{ StreamSourceType::LocalFile };
    std::atomic<StreamSessionState> streamState{ StreamSessionState::Idle };
    std::atomic<int> openLatencyMs{ 0 };
    std::atomic<int> reconnectSuccessCount{ 0 };
    std::atomic<int> bufferingEventCount{ 0 };
    std::atomic<int> consecutiveReadFailures{ 0 };
    int adaptiveRecoveryStage = 0;
    std::string adaptiveHint;
    int liveTuneStage = 0;
    std::string liveTuneProfile;
    std::string liveTuneHint;
    int runtimeAudioTargetMs = 0;
    int runtimeVideoLeadMs = 0;
    int runtimeLateDropMs = 0;
    std::atomic<int> runtimeStartupVideoPackets{ 0 };
    std::atomic<int> runtimeStartupAudioPackets{ 0 };
    std::atomic<int> runtimeResumeVideoPackets{ 0 };
    std::atomic<int> runtimeResumeAudioPackets{ 0 };
    std::atomic<int> runtimeLowWaterVideoPackets{ 0 };
    std::atomic<int> runtimeLowWaterAudioPackets{ 0 };
    std::atomic<int> runtimeStartupAudioBufferedMs{ 0 };
    std::atomic<int> runtimeResumeAudioBufferedMs{ 0 };
    std::atomic<int> runtimeLowWaterAudioBufferedMs{ 0 };
    std::atomic<int> runtimeBufferProfileLevel{ 0 };
    std::atomic<int> runtimeReadyStreak{ 0 };
    std::atomic<int> runtimeReadyTarget{ 0 };
    std::atomic<int> runtimeRebufferCooldownMs{ 0 };
    std::atomic<int> runtimeMinBufferHoldMs{ 0 };
    std::atomic<int> runtimeLowWaterStreak{ 0 };
    std::atomic<int> runtimePlaybackResumeCount{ 0 };
    std::atomic<int> runtimeRebufferSuppressedCount{ 0 };
    std::atomic<int> aiGpuQueueDepth{ 0 };
    std::atomic<int> aiGpuActiveTasks{ 0 };
    std::atomic<int> aiCpuQueueDepth{ 0 };
    std::atomic<int> aiCpuActiveTasks{ 0 };
    std::atomic<int> aiCompletedTasks{ 0 };
    std::atomic<int> aiDroppedTasks{ 0 };
    std::atomic<int> aiCancelledTasks{ 0 };
    std::atomic<int> aiDetectorDroppedTasks{ 0 };
    std::atomic<int> aiDetectorCancelledTasks{ 0 };
    std::atomic<int> aiLastWaitMs{ 0 };
    std::atomic<int> aiAverageWaitMs{ 0 };
    std::atomic<long long> aiAccumulatedWaitMs{ 0 };
    std::atomic<int> aiAcquireCount{ 0 };
    std::atomic<int> aiPriorityTier{ 1 };
    std::atomic<bool> aiFocusRoute{ false };
    std::atomic<bool> aiAlarmRoute{ false };
    std::atomic<bool> aiFullscreenRoute{ false };
    std::atomic<int> aiDetectorMinimumSkipFrames{ 0 };
    int runtimeDetectorSkipFrames = 0;
    int runtimeDetectorBaseSkipFrames = 0;
    std::string statusEventText;
    std::atomic<quint64> statusEventGeneration{ 0 };
    std::atomic<long long> lastAdaptiveActionMs{ 0 };
    long long lastLiveTuneMs = 0;
};

struct SessionResources
{
    mutable std::mutex mux;
    Demux* demux = nullptr;
    VideoThread* vt = nullptr;
    AudioThread* at = nullptr;
    WhisperThread* wt = nullptr;
    DetectorThread* det = nullptr;
    SessionRecorder* recorder = nullptr;
    std::shared_ptr<VideoCallback> currentCall;
};

inline std::string AdaptiveRecoveryModeLabel(int stage)
{
    switch (stage)
    {
    case 1: return "auto-balanced-udp";
    case 2: return "auto-tcp";
    default: return "manual";
    }
}
