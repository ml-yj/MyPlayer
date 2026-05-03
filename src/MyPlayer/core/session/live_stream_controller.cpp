

#include "live_stream_controller.h"

#include "../audio/audio_thread.h"
#include "../media/demux.h"
#include "../../features/detector/detector_thread.h"
#include "stream_session_core.h"
#include "../video/video_thread.h"

#include <chrono>

namespace
{

int ComputeReconnectDelayMs(const ReconnectPolicy& policy, int attempt)
{
    if (attempt <= 0)
        return policy.baseDelayMs;

    int delayMs = policy.baseDelayMs;
    for (int i = 1; i < attempt; ++i)
    {
        if (delayMs >= policy.maxDelayMs / 2)
        {
            delayMs = policy.maxDelayMs;
            break;
        }
        delayMs *= 2;
    }

    if (delayMs > policy.maxDelayMs)
        delayMs = policy.maxDelayMs;
    if (delayMs < 0)
        delayMs = policy.baseDelayMs;
    return delayMs;
}

long long SteadyNowMs()
{
    return static_cast<long long>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}

const char* LiveTuneProfileName(int stage)
{
    switch (stage)
    {
    case 1: return "tight";
    case 2: return "aggressive";
    default: return "base";
    }
}
}

LiveStreamController::LiveStreamController(DemuxThreadState& stateRef, SessionResources& resourcesRef,
    StreamSessionCore& sessionCoreRef, std::function<void(const std::string&)> publishStatusEventRef)
    : state(stateRef)
    , resources(resourcesRef)
    , sessionCore(sessionCoreRef)
    , publishStatusEvent(std::move(publishStatusEventRef))
{
}

void LiveStreamController::ResetRuntimeTuningLocked()
{
    state.liveTuneStage = 0;
    state.liveTuneProfile.clear();
    state.liveTuneHint.clear();
    state.runtimeAudioTargetMs = 0;
    state.runtimeVideoLeadMs = state.activeOpenOptions.videoLeadMs;
    state.runtimeLateDropMs = state.activeOpenOptions.lateVideoDropMs;
    state.runtimeLowWaterStreak.store(0);
    state.runtimeDetectorSkipFrames = 0;
    state.runtimeDetectorBaseSkipFrames = 0;
    state.lastLiveTuneMs = 0;

    if (resources.det)
    {
        state.runtimeDetectorBaseSkipFrames = resources.det->GetBaseSkipFrames();
        resources.det->SetMinimumSkipFrames(state.runtimeDetectorBaseSkipFrames);
        state.runtimeDetectorSkipFrames = resources.det->GetActiveSkipFrames();
    }
}

void LiveStreamController::UpdateRuntimeTuningLocked(bool force)
{
    if (!resources.vt || !resources.at)
        return;

    const StreamPlaybackKind playbackKind = state.playbackKind.load();
    const StreamSourceType sourceType = state.sourceType.load();
    const StreamSourcePolicy sourcePolicy =
        ResolveStreamSourcePolicy(playbackKind, sourceType, state.activeOpenOptions);
    const bool liveNetwork = state.isLiveStream.load() && IsNetworkUrl(state.currentUrl);
    const bool liveLowLatency = liveNetwork && state.activeOpenOptions.enableLowLatency;
    const bool balancedAudioMaster =
        liveNetwork && sourcePolicy.forceBaseTuneInBalancedAudioMaster;
    const bool balancedRtspLive =
        balancedAudioMaster && sourceType == StreamSourceType::Rtsp;

    state.runtimeDetectorBaseSkipFrames = 0;
    state.runtimeDetectorSkipFrames = 0;
    if (resources.det)
    {
        state.runtimeDetectorBaseSkipFrames = resources.det->GetBaseSkipFrames();
        state.runtimeDetectorSkipFrames = resources.det->GetActiveSkipFrames();
    }

    if (!liveNetwork)
    {
        state.liveTuneStage = 0;
        state.liveTuneProfile = "inactive";
        state.liveTuneHint = "Live auto-tuning is inactive";
        state.runtimeAudioTargetMs = 0;
        state.runtimeVideoLeadMs = state.activeOpenOptions.videoLeadMs;
        state.runtimeLateDropMs = state.activeOpenOptions.lateVideoDropMs;
        if (resources.det)
        {
            resources.det->SetMinimumSkipFrames(state.runtimeDetectorBaseSkipFrames);
            state.runtimeDetectorSkipFrames = resources.det->GetActiveSkipFrames();
        }
        state.lastLiveTuneMs = SteadyNowMs();
        return;
    }

    if (balancedAudioMaster)
    {
        const StreamOpenOptions balancedDefaults = StreamOpenOptions::DefaultNetwork();
        const int configuredAudioTargetMs = state.activeOpenOptions.audioDeviceBufferMs > 0
            ? state.activeOpenOptions.audioDeviceBufferMs
            : balancedDefaults.audioDeviceBufferMs;
        const int baseAudioTargetMs = std::max(
            balancedRtspLive ? 220 : 120,
            configuredAudioTargetMs + (balancedRtspLive ? 40 : 0));
        const int baseVideoLeadMs = std::max(
            balancedRtspLive ? 260 : 40,
            state.activeOpenOptions.videoLeadMs + (balancedRtspLive ? 80 : 0));
        const int baseLateDropMs = std::max(80, state.activeOpenOptions.lateVideoDropMs);

        resources.at->SetOutputBufferMs(baseAudioTargetMs);
        resources.vt->SetLatencyTuning(
            StreamPlaybackKind::Live,
            false,
            baseVideoLeadMs,
            baseLateDropMs);

        if (resources.det)
        {
            resources.det->SetMinimumSkipFrames(state.runtimeDetectorBaseSkipFrames);
            state.runtimeDetectorSkipFrames = resources.det->GetActiveSkipFrames();
        }
        else
        {
            state.runtimeDetectorSkipFrames = 0;
        }

        state.liveTuneStage = 0;
        state.liveTuneProfile = "base";
        state.liveTuneHint = balancedRtspLive
            ? "Balanced RTSP live playback favors smoother audio-master sync"
            : "Balanced live playback follows audio-master sync";
        state.runtimeAudioTargetMs = baseAudioTargetMs;
        state.runtimeVideoLeadMs = baseVideoLeadMs;
        state.runtimeLateDropMs = baseLateDropMs;
        state.lastLiveTuneMs = SteadyNowMs();
        return;
    }

    const long long nowMs = SteadyNowMs();
    if (!force && state.lastLiveTuneMs > 0 && nowMs - state.lastLiveTuneMs < 250)
        return;
    state.lastLiveTuneMs = nowMs;

    const StreamOpenOptions balancedDefaults = StreamOpenOptions::DefaultNetwork();
    const int configuredAudioTargetMs = state.activeOpenOptions.audioDeviceBufferMs > 0
        ? state.activeOpenOptions.audioDeviceBufferMs
        : balancedDefaults.audioDeviceBufferMs;
    const int baseAudioTargetMs = std::max(liveLowLatency ? 30 : 120, configuredAudioTargetMs);
    const int baseVideoLeadMs = std::max(40, state.activeOpenOptions.videoLeadMs);
    const int baseLateDropMs = std::max(80, state.activeOpenOptions.lateVideoDropMs);
    const int audioBufferedMs = static_cast<int>(resources.at->GetAudioDeviceBufferMs());
    const int audioQueuePackets = resources.at->GetQueueSize();
    const int videoQueuePackets = resources.vt->GetQueueSize();
    const int audioQueueLimit = std::max(1, resources.at->maxList);
    const int videoQueueLimit = std::max(1, resources.vt->maxList);
    const int readMiss = state.consecutiveReadFailures.load();
    const bool queuePressureRelevant =
        liveLowLatency
        || state.activeOpenOptions.liveClockPolicy == LiveClockPolicy::AudioMasterDropLateVideo;
    const bool severeQueuePressure =
        queuePressureRelevant
        && (audioQueuePackets >= std::max(2, audioQueueLimit - 1)
            || videoQueuePackets >= std::max(2, videoQueueLimit - 1));
    const bool mildQueuePressure =
        queuePressureRelevant
        && (audioQueuePackets >= std::max(2, audioQueueLimit - 2)
            || videoQueuePackets >= std::max(2, videoQueueLimit - 2));

    int targetStage = 0;
    std::string reason = "stable";
    const int severeAudioMarginMs = liveLowLatency ? 140 : 200;
    const int mildAudioMarginMs = liveLowLatency ? 60 : 100;
    if (state.isBuffering.load()
        || readMiss >= 8
        || audioBufferedMs > baseAudioTargetMs + severeAudioMarginMs
        || severeQueuePressure)
    {
        targetStage = 2;
        if (state.isBuffering.load())
            reason = "buffering";
        else if (readMiss >= 8)
            reason = "read miss";
        else if (audioBufferedMs > baseAudioTargetMs + 140)
            reason = "audio backlog";
        else if (audioQueuePackets >= std::max(2, audioQueueLimit - 1))
            reason = "audio queue pressure";
        else
            reason = "video queue pressure";
    }
    else if (readMiss >= 4
        || audioBufferedMs > baseAudioTargetMs + mildAudioMarginMs
        || mildQueuePressure)
    {
        targetStage = 1;
        if (readMiss >= 4)
            reason = "read miss";
        else if (audioBufferedMs > baseAudioTargetMs + 60)
            reason = "audio backlog";
        else if (audioQueuePackets >= std::max(2, audioQueueLimit - 2))
            reason = "audio queue pressure";
        else
            reason = "video queue pressure";
    }

    if (!force && targetStage < state.liveTuneStage)
    {
        const bool stableToRelax = !state.isBuffering.load()
            && readMiss <= 1
            && audioBufferedMs <= baseAudioTargetMs + 20
            && (!queuePressureRelevant
                || (audioQueuePackets <= std::max(1, audioQueueLimit / 2)
                    && videoQueuePackets <= std::max(1, videoQueueLimit / 2)));
        if (!stableToRelax)
            targetStage = state.liveTuneStage;
    }

    int tunedAudioTargetMs = baseAudioTargetMs;
    int tunedVideoLeadMs = baseVideoLeadMs;
    int tunedLateDropMs = baseLateDropMs;
    int tunedDetectorSkipFrames = state.runtimeDetectorBaseSkipFrames;

    if (targetStage == 1)
    {
        tunedVideoLeadMs = std::max(liveLowLatency ? 55 : 90, baseVideoLeadMs - (liveLowLatency ? 20 : 25));
        tunedLateDropMs = std::max(liveLowLatency ? 110 : 120, baseLateDropMs - 30);
        if (tunedDetectorSkipFrames > 0)
            tunedDetectorSkipFrames += 1;
    }
    else if (targetStage >= 2)
    {
        tunedVideoLeadMs = std::max(liveLowLatency ? 40 : 75, baseVideoLeadMs - (liveLowLatency ? 35 : 45));
        tunedLateDropMs = std::max(liveLowLatency ? 80 : 100, baseLateDropMs - 60);
        if (tunedDetectorSkipFrames > 0)
            tunedDetectorSkipFrames += 2;
    }

    resources.at->SetOutputBufferMs(tunedAudioTargetMs);
    resources.vt->SetLatencyTuning(
        StreamPlaybackKind::Live,
        state.activeOpenOptions.liveClockPolicy == LiveClockPolicy::AudioMasterDropLateVideo,
        tunedVideoLeadMs,
        tunedLateDropMs);
    if (resources.det)
    {
        resources.det->SetMinimumSkipFrames(tunedDetectorSkipFrames > 0
            ? tunedDetectorSkipFrames
            : state.runtimeDetectorBaseSkipFrames);
        state.runtimeDetectorSkipFrames = resources.det->GetActiveSkipFrames();
    }
    else
    {
        state.runtimeDetectorSkipFrames = 0;
    }

    state.runtimeAudioTargetMs = tunedAudioTargetMs;
    state.runtimeVideoLeadMs = tunedVideoLeadMs;
    state.runtimeLateDropMs = tunedLateDropMs;
    state.liveTuneProfile = LiveTuneProfileName(targetStage);
    state.liveTuneHint = targetStage == 0
        ? (liveLowLatency ? "Stable low-latency live playback" : "Stable balanced live playback")
        : (targetStage == 1
            ? "Tightened live latency for " + reason
            : "Aggressive live catch-up for " + reason);

    if (!force && targetStage != state.liveTuneStage && publishStatusEvent)
    {
        std::string eventText = "Live tune: ";
        eventText += state.liveTuneProfile;
        eventText += " | audio ";
        eventText += std::to_string(state.runtimeAudioTargetMs);
        eventText += " ms | vlead ";
        eventText += std::to_string(state.runtimeVideoLeadMs);
        eventText += " ms | late ";
        eventText += std::to_string(state.runtimeLateDropMs);
        eventText += " ms";
        if (state.runtimeDetectorSkipFrames > 0)
        {
            eventText += " | det skip ";
            eventText += std::to_string(state.runtimeDetectorSkipFrames);
        }
        publishStatusEvent(eventText);
    }
    state.liveTuneStage = targetStage;
}

bool LiveStreamController::MaybeAutoRecoverLocked()
{
    const StreamSourcePolicy sourcePolicy =
        ResolveStreamSourcePolicy(state.playbackKind.load(), state.sourceType.load(), state.activeOpenOptions);
    if (state.currentUrl.empty()
        || state.sourceType.load() != StreamSourceType::Rtsp
        || !sourcePolicy.allowAdaptiveAutoRecover
        || state.activeOpenOptions.forceTcpForRtsp)
        return false;

    const long long nowMs = SteadyNowMs();
    const long long lastActionMs = state.lastAdaptiveActionMs.load();
    if (lastActionMs > 0 && nowMs - lastActionMs < 1500)
        return false;

    const int audioCatchUps = resources.at ? resources.at->GetLiveCatchUpCount() : 0;
    const int lateDrops = resources.vt ? resources.vt->GetLateFrameDropCount() : 0;
    const int audioQueueDrops = resources.at ? resources.at->GetDroppedPacketCount() : 0;

    StreamOpenOptions nextOptions = state.activeOpenOptions;
    std::string eventText;
    if (state.adaptiveRecoveryStage == 0 && state.activeOpenOptions.enableLowLatency)
    {
        const bool unstable = state.bufferingEventCount.load() >= 2
            || state.reconnectSuccessCount.load() >= 1
            || audioCatchUps >= 2
            || lateDrops >= 48
            || audioQueueDrops >= 12;
        if (!unstable)
            return false;

        const StreamOpenOptions defaults = StreamOpenOptions::DefaultNetwork();
        nextOptions.enableLowLatency = false;
        nextOptions.noBuffer = false;
        nextOptions.lowDelayFlag = false;
        nextOptions.reorderQueueSize = -1;
        nextOptions.videoQueuePackets = defaults.videoQueuePackets;
        nextOptions.audioQueuePackets = defaults.audioQueuePackets;
        nextOptions.liveClockPolicy = defaults.liveClockPolicy;
        nextOptions.videoLeadMs = defaults.videoLeadMs;
        nextOptions.lateVideoDropMs = defaults.lateVideoDropMs;
        nextOptions.audioDeviceBufferMs = defaults.audioDeviceBufferMs;
        nextOptions.probeSizeBytes = std::max(nextOptions.probeSizeBytes, 128 * 1024);
        nextOptions.analyzeDurationUs = std::max(nextOptions.analyzeDurationUs, 1000 * 1000);
        state.adaptiveRecoveryStage = 1;
        state.adaptiveHint = "Auto fallback: switched to Balanced over RTSP/UDP after live jitter";
        eventText = state.adaptiveHint;
    }
    else if (state.adaptiveRecoveryStage <= 1)
    {
        const bool unstable = state.bufferingEventCount.load() >= 4
            || state.reconnectSuccessCount.load() >= 2
            || state.consecutiveReadFailures.load() >= 20
            || audioCatchUps >= 4;
        if (!unstable)
            return false;

        nextOptions.enableLowLatency = false;
        nextOptions.noBuffer = false;
        nextOptions.lowDelayFlag = false;
        nextOptions.reorderQueueSize = -1;
        nextOptions.forceTcpForRtsp = true;
        nextOptions.audioDeviceBufferMs = 0;
        nextOptions.probeSizeBytes = std::max(nextOptions.probeSizeBytes, 128 * 1024);
        nextOptions.analyzeDurationUs = std::max(nextOptions.analyzeDurationUs, 1000 * 1000);
        state.adaptiveRecoveryStage = 2;
        state.adaptiveHint = "Auto fallback: switched to RTSP/TCP after repeated UDP instability";
        eventText = state.adaptiveHint;
    }
    else
    {
        return false;
    }

    state.lastAdaptiveActionMs.store(nowMs);
    sessionCore.TransitionToReconnectingLocked();
    if (!sessionCore.ReopenLocked(nextOptions, false))
        return false;

    if (publishStatusEvent)
        publishStatusEvent(eventText);
    return true;
}
