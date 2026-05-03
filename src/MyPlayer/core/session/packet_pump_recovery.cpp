#include "packet_pump.h"

#include "../audio/audio_thread.h"
#include "../media/demux.h"
#include "live_stream_controller.h"
#include "stream_session_core.h"
#include "../video/video_thread.h"

#include <QThread>

extern "C" {
#include <libavcodec/packet.h>
}

namespace
{
constexpr int kLiveReadMissesBeforeBuffering = 3;
constexpr int kLiveReadMissesBeforeReconnect = 10;
constexpr int kVodReadMissesBeforeDrain = 6;
constexpr int kFileDrainPollStartMisses = 3;
constexpr long long kDrainOutputToleranceMs = 20;

bool ShouldHoldLiveAudioUntilFirstVideo(const DemuxThreadState& state, const SessionResources& resources)
{
        if (state.playbackKind.load() != StreamPlaybackKind::Live)
        return false;
    const StreamSourcePolicy sourcePolicy =
        ResolveStreamSourcePolicy(state.playbackKind.load(), state.sourceType.load(), state.activeOpenOptions);
    if (!sourcePolicy.holdAudioUntilFirstVideo)
        return false;
    const StreamSessionState sessionState = state.streamState.load();
    if (sessionState != StreamSessionState::Priming
        && sessionState != StreamSessionState::Buffering)
    {
        return false;
    }
    if (!resources.demux || !resources.vt)
        return false;
    if (!resources.demux->HasVideoStream())
        return false;
    return resources.vt->GetLastDecodedAtMs() <= 0;
}

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
}

void PacketPump::DispatchPacket(AVPacket* pkt, bool isAudioPacket,
    quint64 packetGeneration, quint64 packetSerial) const
{
    if (isAudioPacket)
    {
        if (ShouldHoldLiveAudioUntilFirstVideo(state, resources))
        {
            state.liveStartupAudioResyncPending.store(true);
            av_packet_free(&pkt);
            return;
        }
        if (resources.at)
            resources.at->PushWithEpoch(pkt, packetGeneration, packetSerial);
        else
            av_packet_free(&pkt);
        return;
    }

    if (resources.vt)
        resources.vt->PushWithEpoch(pkt, packetGeneration, packetSerial);
    else
        av_packet_free(&pkt);
}

void PacketPump::IssueDrainIfNeeded(quint64 packetGeneration, quint64 packetSerial,
    quint64& drainIssuedSerial) const
{
    if (drainIssuedSerial == packetSerial)
        return;

    if (resources.at)
        resources.at->PushDrain(packetGeneration, packetSerial);
    if (resources.vt)
        resources.vt->PushDrain(packetGeneration, packetSerial);
    drainIssuedSerial = packetSerial;
}

bool PacketPump::IsOutputDrainComplete() const
{
    const int videoQueuePackets = resources.vt ? resources.vt->GetQueueSize() : 0;
    const int audioQueuePackets = resources.at ? resources.at->GetQueueSize() : 0;
    const long long audioBufferedMs = resources.at ? resources.at->GetAudioDeviceBufferMs() : 0;
    return videoQueuePackets == 0
        && audioQueuePackets == 0
        && audioBufferedMs <= kDrainOutputToleranceMs;
}

void PacketPump::CompleteFiniteDrain()
{
    if (resources.vt)
        resources.vt->SetSyncPts(state.totalMs.load() + 100000);

    if (!IsOutputDrainComplete())
        return;

    std::lock_guard<std::mutex> lock(resources.mux);
    if (resources.at)
        resources.at->Clear();
    sessionCore.TransitionToEofLocked();
}

void PacketPump::HandleFileReadMiss(
    quint64 packetGeneration, quint64 packetSerial, quint64& drainIssuedSerial, int& readyStreak)
{
    readyStreak = 0;
    state.runtimeReadyStreak.store(0);
    IssueDrainIfNeeded(packetGeneration, packetSerial, drainIssuedSerial);
    {
        std::lock_guard<std::mutex> lock(resources.mux);
        sessionCore.TransitionToDrainingLocked();
    }

    if (state.eofCount >= kFileDrainPollStartMisses)
    {
        CompleteFiniteDrain();
        QThread::msleep(50);
    }
    else
    {
        QThread::msleep(5);
    }
}

void PacketPump::HandleNetworkVodReadMiss(
    quint64 packetGeneration, quint64 packetSerial, quint64& drainIssuedSerial,
    int& readyStreak, long long nowMs, long long& lastBufferingStartMs)
{
    if (state.eofCount < kVodReadMissesBeforeDrain)
    {
        readyStreak = 0;
        state.runtimeReadyStreak.store(0);
        SetConsumerPause(true);
        {
            std::lock_guard<std::mutex> lock(resources.mux);
            sessionCore.TransitionToBufferingLocked(true);
            lastBufferingStartMs = nowMs;
        }
        QThread::msleep(25);
        return;
    }

    IssueDrainIfNeeded(packetGeneration, packetSerial, drainIssuedSerial);
    {
        std::lock_guard<std::mutex> lock(resources.mux);
        sessionCore.TransitionToDrainingLocked();
    }
    CompleteFiniteDrain();
    QThread::msleep(50);
}

void PacketPump::HandleLiveReadMiss(int& readyStreak, long long nowMs, long long& lastBufferingStartMs)
{
        const StreamSourcePolicy sourcePolicy =
        ResolveStreamSourcePolicy(state.playbackKind.load(), state.sourceType.load(), state.activeOpenOptions);
    const bool balancedAudioMaster =
        sourcePolicy.forceBaseTuneInBalancedAudioMaster
        && !state.activeOpenOptions.enableLowLatency
        && state.activeOpenOptions.liveClockPolicy == LiveClockPolicy::AudioMaster;
    const int audioBufferedMs = resources.at ? static_cast<int>(resources.at->GetAudioDeviceBufferMs()) : 0;
    const int audioQueuePackets = resources.at ? resources.at->GetQueueSize() : 0;
    const int videoQueuePackets = resources.vt ? resources.vt->GetQueueSize() : 0;
    const long long lastRenderedVideoMs = resources.vt ? resources.vt->GetLastRenderedAtMs() : 0;
        const bool recentVideoProgress =
        lastRenderedVideoMs > 0 && nowMs - lastRenderedVideoMs <= sourcePolicy.liveVideoProgressGraceMs;
    const bool bufferedPlaybackStillHealthy =
        audioBufferedMs > 30 || audioQueuePackets > 0 || videoQueuePackets > 0 || recentVideoProgress;

    if (state.eofCount >= kLiveReadMissesBeforeBuffering
        && !(balancedAudioMaster && bufferedPlaybackStillHealthy))
    {
        readyStreak = 0;
        state.runtimeReadyStreak.store(0);
        std::lock_guard<std::mutex> lock(resources.mux);
        sessionCore.TransitionToBufferingLocked(true);
        liveController.UpdateRuntimeTuningLocked(false);
        lastBufferingStartMs = nowMs;
    }

    if (state.eofCount < kLiveReadMissesBeforeReconnect)
    {
        QThread::msleep(balancedAudioMaster && bufferedPlaybackStillHealthy ? 10 : 50);
        return;
    }

    int waitMs = 0;
    bool shouldReconnect = false;
    {
        std::lock_guard<std::mutex> lock(resources.mux);

        if (liveController.MaybeAutoRecoverLocked())
            return;

        if (state.activeOpenOptions.reconnect.enabled
            && state.reconnectCount < state.activeOpenOptions.reconnect.maxAttempts)
        {
            state.reconnectCount++;
            sessionCore.TransitionToReconnectingLocked();
            waitMs = ComputeReconnectDelayMs(
                state.activeOpenOptions.reconnect, state.reconnectCount);
            shouldReconnect = true;
        }
        else
        {
            sessionCore.TransitionToFailedLocked(
                "Live stream disconnected (reconnection failed)", true);
        }
    }

    if (!shouldReconnect)
    {
        QThread::msleep(50);
        return;
    }

    QThread::msleep(waitMs);
    std::lock_guard<std::mutex> lock(resources.mux);

    if (resources.demux)
        sessionCore.ReopenLocked(state.activeOpenOptions, true);
}
