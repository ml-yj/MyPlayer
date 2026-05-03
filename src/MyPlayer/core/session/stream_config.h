#pragma once

#include <QtGlobal>

#include <string>

#include "../media/demux_types.h"

enum class StreamSessionState {
    Idle,
    Opening,
    Priming,
    Playing,
    Paused,
    Seeking,
    Buffering,
    Reconnecting,
    Draining,
    Eof,
    Stopped,
    Failed,
};

enum class StreamPlaybackKind {
    File,
    NetworkVod,
    Live,
};

enum class StreamSourceType {
    LocalFile,
    Rtsp,
    Rtmp,
    HttpProgressive,
    HttpFlv,
    Hls,
    WebRtc,
    Unknown,
};

struct StreamSourcePolicy {
    bool allowTransportFallback = false;
    bool allowAdaptiveAutoRecover = false;
    bool holdAudioUntilFirstVideo = false;
    bool disableFragileLiveHardwareDecode = false;
    bool forceBaseTuneInBalancedAudioMaster = false;

    int balancedProbeFloorBytes = 0;
    int balancedAnalyzeDurationUs = 0;
    int lowLatencyProbeFloorBytes = 0;
    int lowLatencyAnalyzeDurationUs = 0;

    int firstVideoGateTimeoutMs = 0;
    int liveVideoProgressGraceMs = 0;
    int liveLowWaterStreakThreshold = 0;
    int liveRebufferCooldownFloorMs = 0;
};

enum class LiveClockPolicy {
    AudioMaster,
    AudioMasterDropLateVideo,
};

struct ReconnectPolicy {
    bool enabled = true;
    int maxAttempts = 5;
    int baseDelayMs = 1000;
    int maxDelayMs = 16000;
};

struct StreamOpenOptions {
    bool enableLowLatency = false;
    bool forceTcpForRtsp = true;
    bool noBuffer = false;
    bool lowDelayFlag = false;
    int connectTimeoutMs = 3000;
    int maxDelayUs = 500000;
    int bufferSizeBytes = 0;
    int probeSizeBytes = 0;
    int analyzeDurationUs = 0;
    int reorderQueueSize = -1;
    int videoQueuePackets = 30;
    int audioQueuePackets = 50;
    int audioDeviceBufferMs = 0;
    LiveClockPolicy liveClockPolicy = LiveClockPolicy::AudioMaster;
    int videoLeadMs = 200;
    int lateVideoDropMs = 0;
    ReconnectPolicy reconnect;

    static StreamOpenOptions DefaultFile()
    {
        StreamOpenOptions options;
        options.connectTimeoutMs = 3000;
        options.maxDelayUs = 500000;
        options.videoQueuePackets = 50;
        options.audioQueuePackets = 50;
        return options;
    }

    static StreamOpenOptions DefaultNetwork()
    {
        StreamOpenOptions options = DefaultFile();
        options.videoQueuePackets = 24;
        options.audioQueuePackets = 40;
        options.audioDeviceBufferMs = 180;
        return options;
    }

    static StreamOpenOptions LowLatencyNetwork()
    {
        StreamOpenOptions options = DefaultNetwork();
        options.enableLowLatency = true;
        options.noBuffer = true;
        options.lowDelayFlag = true;
        options.probeSizeBytes = 32 * 1024;
        options.analyzeDurationUs = 300 * 1000;
        options.reorderQueueSize = 0;
        options.videoQueuePackets = 8;
        options.audioQueuePackets = 6;
        options.audioDeviceBufferMs = 60;
        options.liveClockPolicy = LiveClockPolicy::AudioMasterDropLateVideo;
        options.videoLeadMs = 80;
        options.lateVideoDropMs = 160;

        options.reconnect.enabled = true;
        options.reconnect.maxAttempts = 5;
        options.reconnect.baseDelayMs = 1000;
        options.reconnect.maxDelayMs = 16000;
        return options;
    }
};

struct StreamStatsSnapshot {
    StreamSessionState state = StreamSessionState::Idle;
    StreamPlaybackKind playbackKind = StreamPlaybackKind::File;
    StreamSourceType sourceType = StreamSourceType::LocalFile;

    bool isNetwork = false;
    bool isLive = false;
    bool lowLatencyEnabled = false;
    int openLatencyMs = 0;
    int reconnectAttempts = 0;
    int reconnectSuccesses = 0;
    int bufferingEvents = 0;
    int consecutiveReadFailures = 0;

    int videoQueuePackets = 0;
    int videoQueueLimit = 0;
    int audioQueuePackets = 0;
    int audioQueueLimit = 0;
    int audioDeviceBufferedMs = 0;

    int droppedVideoPackets = 0;
    int droppedAudioPackets = 0;
    int droppedLateVideoFrames = 0;

    int audioCatchUpEvents = 0;
    int audioThrottleEvents = 0;

    std::string adaptiveMode;
    std::string adaptiveHint;
    std::string liveTuneProfile;
    std::string liveTuneHint;

    int runtimeAudioTargetMs = 0;
    int runtimeVideoLeadMs = 0;
    int runtimeLateDropMs = 0;
    bool primingPending = false;
    int runtimeStartupVideoPackets = 0;
    int runtimeStartupAudioPackets = 0;
    int runtimeResumeVideoPackets = 0;
    int runtimeResumeAudioPackets = 0;
    int runtimeLowWaterVideoPackets = 0;
    int runtimeLowWaterAudioPackets = 0;
    int runtimeStartupAudioBufferedMs = 0;
    int runtimeResumeAudioBufferedMs = 0;
    int runtimeLowWaterAudioBufferedMs = 0;
    int runtimeBufferProfileLevel = 0;
    int runtimeReadyStreak = 0;
    int runtimeReadyTarget = 0;
    int runtimeRebufferCooldownMs = 0;
    int runtimeMinBufferHoldMs = 0;
    int runtimeLowWaterStreak = 0;
    int runtimePlaybackResumeCount = 0;
    int runtimeRebufferSuppressedCount = 0;

    int aiGpuQueueDepth = 0;
    int aiGpuActiveTasks = 0;
    int aiCpuQueueDepth = 0;
    int aiCpuActiveTasks = 0;
    int aiCompletedTasks = 0;
    int aiDroppedTasks = 0;
    int aiCancelledTasks = 0;
    int aiDetectorDroppedTasks = 0;
    int aiDetectorCancelledTasks = 0;
    int aiLastWaitMs = 0;
    int aiAverageWaitMs = 0;
    int runtimeDetectorSkipFrames = 0;
    int runtimeDetectorBaseSkipFrames = 0;

    long long diagnosticsUptimeMs = 0;
    int diagnosticsLogLines = 0;
    int diagnosticsWarningLines = 0;
    int diagnosticsErrorLines = 0;
    int diagnosticsQtWarnings = 0;
    int diagnosticsQtCriticals = 0;
    int diagnosticsFileWriteFailures = 0;
    int diagnosticsCrashDumpsWritten = 0;
    bool diagnosticsCrashHandlerInstalled = false;
};

struct PlaybackSessionSnapshot
{
    bool isPaused = false;
    bool isComplete = false;
    bool isLiveStream = false;
    bool isBuffering = false;
    bool hasError = false;
    long long positionMs = 0;
    long long totalMs = 0;
    StreamSessionState state = StreamSessionState::Idle;
    StreamSourceType sourceType = StreamSourceType::LocalFile;
    StreamEpoch epoch;
    quint64 statusEventGeneration = 0;
    std::string statusEventText;
    std::string currentUrl;
    std::string lastError;
};

struct PlaybackMediaSnapshot
{
    int videoWidth = 0;
    int videoHeight = 0;
    int videoFpsNum = 0;
    int videoFpsDen = 1;
    long long bitrate = 0;
    int audioSampleRate = 0;
    int audioChannels = 0;
};

inline const char* StreamSessionStateName(StreamSessionState state)
{
    switch (state)
    {
    case StreamSessionState::Idle:         return "Idle";
    case StreamSessionState::Opening:      return "Opening";
    case StreamSessionState::Priming:      return "Priming";
    case StreamSessionState::Playing:      return "Playing";
    case StreamSessionState::Paused:       return "Paused";
    case StreamSessionState::Seeking:      return "Seeking";
    case StreamSessionState::Buffering:    return "Buffering";
    case StreamSessionState::Reconnecting: return "Reconnecting";
    case StreamSessionState::Draining:     return "Draining";
    case StreamSessionState::Eof:          return "EOF";
    case StreamSessionState::Stopped:      return "Stopped";
    case StreamSessionState::Failed:       return "Failed";
    }
    return "Unknown";
}

inline StreamPlaybackKind ResolveStreamPlaybackKind(bool isNetwork, bool isLive)
{
    if (!isNetwork) return StreamPlaybackKind::File;
    return isLive ? StreamPlaybackKind::Live : StreamPlaybackKind::NetworkVod;
}

inline const char* StreamPlaybackKindName(StreamPlaybackKind kind)
{
    switch (kind)
    {
    case StreamPlaybackKind::File:       return "file";
    case StreamPlaybackKind::NetworkVod: return "network-vod";
    case StreamPlaybackKind::Live:       return "live";
    }
    return "unknown";
}

inline const char* StreamSourceTypeName(StreamSourceType type)
{
    switch (type)
    {
    case StreamSourceType::LocalFile:       return "local-file";
    case StreamSourceType::Rtsp:            return "rtsp";
    case StreamSourceType::Rtmp:            return "rtmp";
    case StreamSourceType::HttpProgressive: return "http-progressive";
    case StreamSourceType::HttpFlv:         return "http-flv";
    case StreamSourceType::Hls:             return "hls";
    case StreamSourceType::WebRtc:          return "webrtc";
    case StreamSourceType::Unknown:         return "unknown";
    }
    return "unknown";
}

inline const char* LiveClockPolicyName(LiveClockPolicy policy)
{
    switch (policy)
    {
    case LiveClockPolicy::AudioMaster:              return "audio-master";
    case LiveClockPolicy::AudioMasterDropLateVideo: return "audio-master/drop-late-video";
    }
    return "unknown";
}

StreamSourceType ResolveStreamSourceType(const std::string& url);
bool UsesContinuousLiveBufferStrategy(StreamSourceType sourceType);
StreamSourcePolicy ResolveStreamSourcePolicy(
    StreamPlaybackKind playbackKind,
    StreamSourceType sourceType,
    const StreamOpenOptions& options);
bool IsNetworkUrl(const std::string& url);
bool IsNetworkUrl(const char* url);
