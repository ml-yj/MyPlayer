#include "packet_pump.h"

#include "live_stream_controller.h"
#include "packet_pump_buffering.h"
#include "stream_session_core.h"
#include "../audio/audio_thread.h"
#include "../media/decode_thread.h"
#include "../media/demux.h"
#include "../video/video_thread.h"
#include "../../common/diagnostics/logger.h"

#include <algorithm>
#include <mutex>
#include <string>

using namespace PacketPumpBuffering;

namespace
{
QueueOverflowPolicy ResolveDefaultLiveAudioOverflow(const DemuxThreadState& state, bool balancedAudioMasterLive)
{
    return state.playbackKind.load() == StreamPlaybackKind::Live
        ? (balancedAudioMasterLive ? QueueOverflowPolicy::BlockProducer : QueueOverflowPolicy::DropOldest)
        : QueueOverflowPolicy::BlockProducer;
}
}

void PacketPump::MaybeAdvanceBufferedPlayback(
    int& readyStreak, long long nowMs, long long& lastPlaybackResumeMs,
    long long lastBufferingStartMs, long long lastPrimingStartMs) const
{
    const StreamSessionState sessionState = state.streamState.load();
    const bool priming = sessionState == StreamSessionState::Priming;
    const bool buffering = sessionState == StreamSessionState::Buffering;
    if (!priming && !buffering)
    {
        readyStreak = 0;
        state.runtimeReadyStreak.store(0);
        return;
    }

    const BufferProfile profile = BuildBufferProfile(state, resources);
    const bool shouldHold = priming ? profile.holdDuringPriming : profile.holdDuringBuffering;
    const StreamSourcePolicy sourcePolicy =
        ResolveStreamSourcePolicy(state.playbackKind.load(), state.sourceType.load(), state.activeOpenOptions);
    const bool balancedAudioMasterLive =
        sourcePolicy.forceBaseTuneInBalancedAudioMaster
        && !state.activeOpenOptions.enableLowLatency
        && state.activeOpenOptions.liveClockPolicy == LiveClockPolicy::AudioMaster;
    const QueueOverflowPolicy defaultLiveAudioOverflow =
        ResolveDefaultLiveAudioOverflow(state, balancedAudioMasterLive);

    if (state.isPause.load())
    {
        readyStreak = 0;
        state.runtimeReadyStreak.store(0);
        if (shouldHold)
            SetConsumerPause(true);
        return;
    }

    if (!shouldHold)
    {
        state.primingPending.store(false);
        SetConsumerPause(false);
        std::lock_guard<std::mutex> lock(resources.mux);
        sessionCore.TransitionToPlaybackLocked();
        liveController.UpdateRuntimeTuningLocked(false);
        if (state.playbackKind.load() != StreamPlaybackKind::File)
        {
            state.statusEventText = std::string("Buffer ready: ")
                + BufferReasonLabel(state.playbackKind.load());
            state.statusEventGeneration.fetch_add(1);
        }
        state.runtimePlaybackResumeCount.fetch_add(1);
        readyStreak = 0;
        state.runtimeReadyStreak.store(0);
        lastPlaybackResumeMs = nowMs;
        return;
    }

    const bool firstVideoDecoded = !resources.vt || resources.vt->GetLastDecodedAtMs() > 0;
    const bool firstVideoGateTimedOut =
        lastPrimingStartMs > 0 && nowMs - lastPrimingStartMs >= sourcePolicy.firstVideoGateTimeoutMs;
    const bool softHoldBalancedAudioMaster = balancedAudioMasterLive;
    const bool waitForFirstVideoFrame =
        state.playbackKind.load() == StreamPlaybackKind::Live
        && resources.demux
        && resources.vt
        && resources.demux->HasVideoStream()
        && !firstVideoDecoded
        && !firstVideoGateTimedOut;
    if (waitForFirstVideoFrame)
    {
        std::lock_guard<std::mutex> lock(resources.mux);
        state.liveStartupAudioResyncPending.store(!softHoldBalancedAudioMaster);
        if (resources.at)
        {
            resources.at->SetOverflowPolicy(softHoldBalancedAudioMaster
                ? defaultLiveAudioOverflow
                : QueueOverflowPolicy::DropOldest);
            resources.at->SetPause(!softHoldBalancedAudioMaster);
        }
        if (resources.vt)
            resources.vt->SetPause(false);
        LogPrimingWaitState(state, resources, profile, buffering);
        readyStreak = 0;
        state.runtimeReadyStreak.store(0);
        return;
    }

    SetConsumerPause(true);
    if (buffering
        && state.playbackKind.load() != StreamPlaybackKind::File
        && state.consecutiveReadFailures.load() > 1)
    {
        readyStreak = 0;
        state.runtimeReadyStreak.store(0);
        return;
    }

    if (resources.at)
        resources.at->SetOverflowPolicy(defaultLiveAudioOverflow);

    if (!softHoldBalancedAudioMaster && state.liveStartupAudioResyncPending.load() && firstVideoDecoded && resources.at)
    {
        const int queuedAudioPackets = resources.at->GetQueueSize();
        const int bufferedAudioMs = static_cast<int>(resources.at->GetAudioDeviceBufferMs());
        const int bufferedAudioToleranceMs = std::max(8, profile.resumeAudioBufferedMs);
        const bool needStartupAudioResync =
            queuedAudioPackets >= 3 || bufferedAudioMs > bufferedAudioToleranceMs;

        if (needStartupAudioResync)
        {
            resources.at->DiscardQueuedDataAndResetOutput();
            resources.at->ResetDroppedPacketCount();
            resources.at->ResetLiveLatencyStats();
            Logger::Instance().Log(
                LogLevel::Info,
                "session",
                "live.audio_startup_resync",
                "Dropped startup audio backlog after first video frame",
                {
                    { "url", state.currentUrl },
                    { "playback_kind", StreamPlaybackKindName(state.playbackKind.load()) },
                    { "queued_audio_packets", std::to_string(queuedAudioPackets) },
                    { "buffered_audio_ms", std::to_string(bufferedAudioMs) },
                });
            readyStreak = 0;
            state.runtimeReadyStreak.store(0);
            resources.at->SetOverflowPolicy(defaultLiveAudioOverflow);
            state.liveStartupAudioResyncPending.store(false);
            return;
        }

        resources.at->SetOverflowPolicy(defaultLiveAudioOverflow);
        state.liveStartupAudioResyncPending.store(false);
    }

    const bool allowQueueOnlyAudioReady = shouldHold;
    const bool allowDecodedVideoAsReady =
        priming
        && state.playbackKind.load() == StreamPlaybackKind::Live
        && resources.vt
        && (resources.vt->GetLastDecodedAtMs() > 0 || firstVideoGateTimedOut);
    if (!IsBufferReady(profile, resources, buffering, allowQueueOnlyAudioReady, allowDecodedVideoAsReady))
    {
        LogPrimingWaitState(state, resources, profile, buffering);
        readyStreak = 0;
        state.runtimeReadyStreak.store(0);
        return;
    }

    ++readyStreak;
    state.runtimeReadyStreak.store(readyStreak);
    if (readyStreak < ComputeReadyStreakTarget(state, sessionState))
        return;

    const int minBufferHoldMs = ComputeMinBufferHoldMs(state, sessionState);
    if (minBufferHoldMs > 0
        && lastBufferingStartMs > 0
        && nowMs - lastBufferingStartMs < minBufferHoldMs)
    {
        return;
    }

    state.primingPending.store(false);
    SetConsumerPause(false);
    std::lock_guard<std::mutex> lock(resources.mux);
    sessionCore.TransitionToPlaybackLocked();
    liveController.UpdateRuntimeTuningLocked(false);
    if (state.playbackKind.load() != StreamPlaybackKind::File)
    {
        state.statusEventText = std::string("Buffer ready: ")
            + BufferReasonLabel(state.playbackKind.load());
        state.statusEventGeneration.fetch_add(1);
    }
    state.runtimePlaybackResumeCount.fetch_add(1);
    readyStreak = 0;
    state.runtimeReadyStreak.store(0);
    lastPlaybackResumeMs = nowMs;
}
