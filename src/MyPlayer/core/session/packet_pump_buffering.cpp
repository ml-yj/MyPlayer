#include "packet_pump_buffering.h"

#include "../audio/audio_thread.h"
#include "../media/demux.h"
#include "../video/video_thread.h"
#include "../../common/diagnostics/logger.h"

#include <algorithm>
#include <chrono>

namespace PacketPumpBuffering
{
namespace
{
bool HasAudioStream(const DemuxThreadState& state, const SessionResources& resources)
{
    return state.audioPlaybackAvailable.load()
        && resources.demux
        && resources.demux->GetAudioStreamCount() > 0;
}
}

int ComputeBufferProfileLevel(const DemuxThreadState& state, StreamPlaybackKind playbackKind)
{
    const int bufferingEvents = state.bufferingEventCount.load();
    const int readMisses = state.consecutiveReadFailures.load();

    if (playbackKind == StreamPlaybackKind::NetworkVod)
    {
        int level = 0;
        if (bufferingEvents >= 2 || readMisses >= 3)
            level = 1;
        if (bufferingEvents >= 5 || readMisses >= 6)
            level = 2;
        return std::clamp(level, 0, 2);
    }

    if (playbackKind == StreamPlaybackKind::Live)
    {
        int level = std::clamp(state.liveTuneStage, 0, 2);
        if (bufferingEvents >= 3)
            level = std::max(level, 1);
        if (bufferingEvents >= 6 || readMisses >= 8)
            level = std::max(level, 2);
        return std::clamp(level, 0, 2);
    }

    return 0;
}

BufferProfile BuildBufferProfile(const DemuxThreadState& state, const SessionResources& resources)
{
    const StreamPlaybackKind playbackKind = state.playbackKind.load();
    const int videoLimit = std::max(1, resources.vt ? resources.vt->maxList : 1);
    const int audioLimit = std::max(1, resources.at ? resources.at->maxList : 1);
    const int profileLevel = ComputeBufferProfileLevel(state, playbackKind);
    const bool hasAudioStream = HasAudioStream(state, resources);

    BufferProfile profile;
    switch (playbackKind)
    {
    case StreamPlaybackKind::File:
        profile.startupVideoPackets = std::clamp(videoLimit / 4, 4, 12);
        profile.startupAudioPackets = hasAudioStream ? std::clamp(audioLimit / 5, 4, 12) : 0;
        profile.resumeVideoPackets = profile.startupVideoPackets;
        profile.resumeAudioPackets = profile.startupAudioPackets;
        break;
    case StreamPlaybackKind::NetworkVod:
        profile.startupVideoPackets = std::clamp(videoLimit / 4 + profileLevel, 3, 10);
        profile.startupAudioPackets = hasAudioStream ? std::clamp(audioLimit / 5 + profileLevel, 4, 12) : 0;
        profile.startupAudioBufferedMs = hasAudioStream ? 18 + profileLevel * 6 : 0;
        profile.resumeVideoPackets = std::max(2, profile.startupVideoPackets - 1);
        profile.resumeAudioPackets = hasAudioStream ? std::max(2, profile.startupAudioPackets - 1) : 0;
        profile.lowWaterVideoPackets = std::max(1, profile.resumeVideoPackets - 1);
        profile.lowWaterAudioPackets = hasAudioStream ? std::max(1, profile.resumeAudioPackets - 1) : 0;
        profile.resumeAudioBufferedMs = hasAudioStream ? 12 + profileLevel * 4 : 0;
        profile.lowWaterAudioBufferedMs = hasAudioStream ? 4 + profileLevel * 2 : 0;
        profile.holdDuringPriming = true;
        profile.holdDuringBuffering = true;
        break;
    case StreamPlaybackKind::Live:
        profile.startupVideoPackets = std::clamp(
            videoLimit / 6 + (state.activeOpenOptions.enableLowLatency ? 0 : std::min(profileLevel, 1)),
            1, state.activeOpenOptions.enableLowLatency ? 3 : 4);
        profile.startupAudioPackets = hasAudioStream
            ? std::clamp(
                audioLimit / 6 + (state.activeOpenOptions.enableLowLatency ? 0 : std::min(profileLevel, 1)),
                1, state.activeOpenOptions.enableLowLatency ? 3 : 4)
            : 0;
        profile.startupAudioBufferedMs = hasAudioStream ? 8 + profileLevel * 2 : 0;
        profile.resumeVideoPackets = state.activeOpenOptions.enableLowLatency ? 1 : std::min(2, profile.startupVideoPackets);
        profile.resumeAudioPackets = hasAudioStream
            ? (state.activeOpenOptions.enableLowLatency ? 1 : std::min(2, profile.startupAudioPackets))
            : 0;
        profile.lowWaterVideoPackets = state.activeOpenOptions.enableLowLatency ? 0 : std::max(0, profile.resumeVideoPackets - 1);
        profile.lowWaterAudioPackets = hasAudioStream
            ? (state.activeOpenOptions.enableLowLatency ? 0 : std::max(0, profile.resumeAudioPackets - 1))
            : 0;
        profile.resumeAudioBufferedMs = hasAudioStream ? 4 + profileLevel * 2 : 0;
        profile.lowWaterAudioBufferedMs = hasAudioStream ? 2 + profileLevel : 0;
        profile.holdDuringPriming = true;
        profile.holdDuringBuffering = true;
        break;
    }

    return profile;
}

const char* BufferReasonLabel(StreamPlaybackKind playbackKind)
{
    switch (playbackKind)
    {
    case StreamPlaybackKind::File:       return "file";
    case StreamPlaybackKind::NetworkVod: return "network-vod";
    case StreamPlaybackKind::Live:       return "live";
    }
    return "unknown";
}

bool IsBufferReady(const BufferProfile& profile, const SessionResources& resources,
    bool recovering, bool allowQueueOnlyAudioReady, bool allowDecodedVideoAsReady)
{
    const int requiredVideoPackets = recovering ? profile.resumeVideoPackets : profile.startupVideoPackets;
    const int requiredAudioPackets = recovering ? profile.resumeAudioPackets : profile.startupAudioPackets;
    const int requiredAudioBufferedMs = recovering ? profile.resumeAudioBufferedMs : profile.startupAudioBufferedMs;
    const int videoQueuePackets = resources.vt ? resources.vt->GetQueueSize() : requiredVideoPackets;
    const int audioQueuePackets = resources.at ? resources.at->GetQueueSize() : requiredAudioPackets;
    const int audioBufferedMs = resources.at ? static_cast<int>(resources.at->GetAudioDeviceBufferMs()) : requiredAudioBufferedMs;

    bool videoReady = videoQueuePackets >= requiredVideoPackets;
    if (allowDecodedVideoAsReady && resources.vt && resources.vt->GetLastDecodedAtMs() > 0)
        videoReady = true;

    const bool queueReady = videoReady && audioQueuePackets >= requiredAudioPackets;
    if (!queueReady)
        return false;

    if (allowQueueOnlyAudioReady)
        return true;

    return audioBufferedMs >= requiredAudioBufferedMs;
}

void LogPrimingWaitState(const DemuxThreadState& state, const SessionResources& resources,
    const BufferProfile& profile, bool recovering)
{
    static std::atomic<long long> lastLogMs{ 0 };
    const long long nowMs = static_cast<long long>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    const long long previous = lastLogMs.load();
    if (previous > 0 && nowMs - previous < 500)
        return;
    lastLogMs.store(nowMs);

    const int requiredVideoPackets = recovering ? profile.resumeVideoPackets : profile.startupVideoPackets;
    const int requiredAudioPackets = recovering ? profile.resumeAudioPackets : profile.startupAudioPackets;
    const int requiredAudioBufferedMs = recovering ? profile.resumeAudioBufferedMs : profile.startupAudioBufferedMs;
    const int videoQueuePackets = resources.vt ? resources.vt->GetQueueSize() : -1;
    const int audioQueuePackets = resources.at ? resources.at->GetQueueSize() : -1;
    const int audioBufferedMs = resources.at ? static_cast<int>(resources.at->GetAudioDeviceBufferMs()) : -1;
    const bool firstVideoDecoded = !resources.vt || resources.vt->GetLastDecodedAtMs() > 0;
    const bool firstVideoRendered = !resources.vt || resources.vt->GetLastRenderedAtMs() > 0;

    Logger::Instance().Log(
        LogLevel::Info,
        "session",
        "priming.wait",
        "Waiting for priming/buffering thresholds",
        {
            { "state", StreamSessionStateName(state.streamState.load()) },
            { "playback_kind", StreamPlaybackKindName(state.playbackKind.load()) },
            { "paused", state.isPause.load() ? "true" : "false" },
            { "recovering", recovering ? "true" : "false" },
            { "video_queue", std::to_string(videoQueuePackets) },
            { "audio_queue", std::to_string(audioQueuePackets) },
            { "audio_buffer_ms", std::to_string(audioBufferedMs) },
            { "first_video_decoded", firstVideoDecoded ? "true" : "false" },
            { "first_video_rendered", firstVideoRendered ? "true" : "false" },
            { "need_video_queue", std::to_string(requiredVideoPackets) },
            { "need_audio_queue", std::to_string(requiredAudioPackets) },
            { "need_audio_buffer_ms", std::to_string(requiredAudioBufferedMs) },
        });
}

bool IsBufferLow(const BufferProfile& profile, const SessionResources& resources)
{
    const int videoQueuePackets = resources.vt ? resources.vt->GetQueueSize() : profile.lowWaterVideoPackets;
    const int audioQueuePackets = resources.at ? resources.at->GetQueueSize() : profile.lowWaterAudioPackets;
    const int audioBufferedMs = resources.at ? static_cast<int>(resources.at->GetAudioDeviceBufferMs()) : profile.lowWaterAudioBufferedMs;

    return videoQueuePackets <= profile.lowWaterVideoPackets
        || audioQueuePackets <= profile.lowWaterAudioPackets
        || audioBufferedMs <= profile.lowWaterAudioBufferedMs;
}

long long SteadyNowMs()
{
    return static_cast<long long>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}

int ComputeReadyStreakTarget(const DemuxThreadState& state, StreamSessionState sessionState)
{
    const StreamPlaybackKind playbackKind = state.playbackKind.load();
    if (playbackKind == StreamPlaybackKind::File)
        return 1;

    if (playbackKind == StreamPlaybackKind::NetworkVod)
        return sessionState == StreamSessionState::Priming ? 2 : 3;

    if (state.activeOpenOptions.enableLowLatency)
        return sessionState == StreamSessionState::Priming ? 1 : 2;

    return sessionState == StreamSessionState::Priming ? 2 : 3;
}

int ComputeRebufferCooldownMs(const DemuxThreadState& state)
{
    const StreamPlaybackKind playbackKind = state.playbackKind.load();
    const StreamSourcePolicy sourcePolicy =
        ResolveStreamSourcePolicy(playbackKind, state.sourceType.load(), state.activeOpenOptions);
    switch (playbackKind)
    {
    case StreamPlaybackKind::File:
        return 0;
    case StreamPlaybackKind::NetworkVod:
        return 250;
    case StreamPlaybackKind::Live:
        return std::max(state.activeOpenOptions.enableLowLatency ? 120 : 220, sourcePolicy.liveRebufferCooldownFloorMs);
    }
    return 0;
}

int ComputeMinBufferHoldMs(const DemuxThreadState& state, StreamSessionState sessionState)
{
    if (sessionState != StreamSessionState::Buffering)
        return 0;

    switch (state.playbackKind.load())
    {
    case StreamPlaybackKind::File:
        return 0;
    case StreamPlaybackKind::NetworkVod:
        return 80;
    case StreamPlaybackKind::Live:
        return state.activeOpenOptions.enableLowLatency ? 40 : 70;
    }
    return 0;
}

void PublishBufferProfile(DemuxThreadState& state, const BufferProfile& profile, int profileLevel,
    int readyStreak, int lowWaterStreak)
{
    const StreamSessionState sessionState = state.streamState.load();
    state.runtimeStartupVideoPackets.store(profile.startupVideoPackets);
    state.runtimeStartupAudioPackets.store(profile.startupAudioPackets);
    state.runtimeStartupAudioBufferedMs.store(profile.startupAudioBufferedMs);
    state.runtimeResumeVideoPackets.store(profile.resumeVideoPackets);
    state.runtimeResumeAudioPackets.store(profile.resumeAudioPackets);
    state.runtimeResumeAudioBufferedMs.store(profile.resumeAudioBufferedMs);
    state.runtimeLowWaterVideoPackets.store(profile.lowWaterVideoPackets);
    state.runtimeLowWaterAudioPackets.store(profile.lowWaterAudioPackets);
    state.runtimeLowWaterAudioBufferedMs.store(profile.lowWaterAudioBufferedMs);
    state.runtimeBufferProfileLevel.store(profileLevel);
    state.runtimeReadyStreak.store(readyStreak);
    state.runtimeReadyTarget.store(ComputeReadyStreakTarget(state, sessionState));
    state.runtimeRebufferCooldownMs.store(ComputeRebufferCooldownMs(state));
    state.runtimeMinBufferHoldMs.store(ComputeMinBufferHoldMs(state, sessionState));
    state.runtimeLowWaterStreak.store(lowWaterStreak);
}

bool ShouldStartNetworkVodRebuffer(const DemuxThreadState& state, const SessionResources& resources,
    int lowWaterStreak)
{
    if (state.playbackKind.load() != StreamPlaybackKind::NetworkVod
        || state.isPause.load()
        || state.streamState.load() != StreamSessionState::Playing)
    {
        return false;
    }

    const BufferProfile profile = BuildBufferProfile(state, resources);
    return lowWaterStreak >= 2 && IsBufferLow(profile, resources);
}

bool ShouldStartLowWaterBuffering(DemuxThreadState& state, const SessionResources& resources,
    int lowWaterStreak, long long nowMs, long long lastPlaybackResumeMs, long long lastRenderedVideoMs)
{
    const StreamPlaybackKind playbackKind = state.playbackKind.load();
    const StreamSourcePolicy sourcePolicy =
        ResolveStreamSourcePolicy(playbackKind, state.sourceType.load(), state.activeOpenOptions);
    if (state.isPause.load() || state.streamState.load() != StreamSessionState::Playing)
        return false;

    const BufferProfile profile = BuildBufferProfile(state, resources);
    if (playbackKind == StreamPlaybackKind::Live)
    {
        const int audioBufferedMs = resources.at ? static_cast<int>(resources.at->GetAudioDeviceBufferMs()) : 0;
        const int audioQueuePackets = resources.at ? resources.at->GetQueueSize() : 0;
        const bool hasVideoRendered = lastRenderedVideoMs > 0;
        const bool aggressiveLiveRecovery =
            state.activeOpenOptions.enableLowLatency
            || state.activeOpenOptions.liveClockPolicy == LiveClockPolicy::AudioMasterDropLateVideo;
        const long long videoProgressGraceMs = aggressiveLiveRecovery
            ? std::min<long long>(sourcePolicy.liveVideoProgressGraceMs, 1500)
            : sourcePolicy.liveVideoProgressGraceMs;
        const bool recentVideoProgress = hasVideoRendered && nowMs - lastRenderedVideoMs <= videoProgressGraceMs;
        const bool hasReadFailures = state.consecutiveReadFailures.load() > 0;
        const bool audioStillHealthy = audioBufferedMs > profile.lowWaterAudioBufferedMs + 5
            || audioQueuePackets >= 2;

        if (!hasReadFailures && audioStillHealthy)
            return false;

        if (!hasReadFailures && recentVideoProgress)
            return false;

        const bool balancedAudioMaster =
            sourcePolicy.forceBaseTuneInBalancedAudioMaster
            && !state.activeOpenOptions.enableLowLatency
            && state.activeOpenOptions.liveClockPolicy == LiveClockPolicy::AudioMaster;
        if (balancedAudioMaster && !hasReadFailures)
            return false;
    }

    if (!IsBufferLow(profile, resources))
        return false;

    bool shouldTrigger = false;
    if (playbackKind == StreamPlaybackKind::NetworkVod)
        shouldTrigger = lowWaterStreak >= 2;
    else if (playbackKind == StreamPlaybackKind::Live)
        shouldTrigger = lowWaterStreak >= sourcePolicy.liveLowWaterStreakThreshold;

    if (!shouldTrigger)
        return false;

    int cooldownMs = ComputeRebufferCooldownMs(state);
    if (playbackKind == StreamPlaybackKind::Live)
        cooldownMs = std::max(cooldownMs, sourcePolicy.liveRebufferCooldownFloorMs);

    if (cooldownMs > 0 && lastPlaybackResumeMs > 0 && nowMs - lastPlaybackResumeMs < cooldownMs)
    {
        state.runtimeRebufferSuppressedCount.fetch_add(1);
        return false;
    }

    return true;
}
}
