#include "packet_pump.h"

#include "../audio/audio_thread.h"
#include "../media/decode.h"
#include "../media/demux.h"
#include "../recording/session_recorder.h"
#include "../../common/diagnostics/logger.h"
#include "live_stream_controller.h"
#include "packet_pump_buffering.h"
#include "stream_session_core.h"
#include "../video/video_thread.h"

#include <QThread>

#include <algorithm>
#include <chrono>

extern "C" {
#include <libavcodec/packet.h>
}

using namespace PacketPumpBuffering;

PacketPump::PacketPump(DemuxThreadState& stateRef, SessionResources& resourcesRef,
    LiveStreamController& liveControllerRef, StreamSessionCore& sessionCoreRef)
    : state(stateRef)
    , resources(resourcesRef)
    , liveController(liveControllerRef)
    , sessionCore(sessionCoreRef)
{
}

void PacketPump::UpdatePlaybackClock() const
{
    if (!resources.vt || !resources.at)
        return;

    const bool useAudioClock = state.audioPlaybackAvailable.load();
    const long long audioPts = resources.at->GetPts();
    Decode* videoDecode = resources.vt->GetDecode();
    const long long videoPts = videoDecode ? videoDecode->pts.load() : audioPts;
    const long long masterPts = useAudioClock ? audioPts : videoPts;
    state.pts.store(masterPts);
    resources.vt->SetSyncPts(masterPts);
}

bool PacketPump::ReadNextPacket(AVPacket*& pkt, Demux*& localDemux,
    quint64& packetGeneration, quint64& packetSerial) const
{
    pkt = nullptr;
    localDemux = nullptr;
    packetGeneration = 0;
    packetSerial = 0;

    std::lock_guard<std::mutex> lock(resources.mux);
    packetGeneration = state.generation.load();
    packetSerial = state.serial.load();
    localDemux = resources.demux;
    if (localDemux)
        pkt = localDemux->Read();
    return pkt != nullptr;
}

void PacketPump::HandlePacketReadSuccess()
{
    state.eofCount = 0;
    state.reconnectCount = 0;
    state.consecutiveReadFailures.store(0);

    std::lock_guard<std::mutex> lock(resources.mux);
    liveController.UpdateRuntimeTuningLocked(false);
}

void PacketPump::SetConsumerPause(bool paused) const
{
    std::lock_guard<std::mutex> lock(resources.mux);
    if (resources.at)
        resources.at->SetPause(paused);
    if (resources.vt)
        resources.vt->SetPause(paused);
}

void PacketPump::Run()
{
    quint64 drainIssuedSerial = 0;
    int lowWaterStreak = 0;
    int readyStreak = 0;
    long long lastBufferingStartMs = 0;
    long long lastPrimingStartMs = 0;
    long long lastPlaybackResumeMs = 0;
    long long lastRenderedVideoMs = 0;
    StreamSessionState observedSessionState = state.streamState.load();
    if (observedSessionState == StreamSessionState::Priming)
        lastPrimingStartMs = SteadyNowMs();
    else if (observedSessionState == StreamSessionState::Buffering)
        lastBufferingStartMs = SteadyNowMs();

    while (!state.isExit.load())
    {
        const StreamPlaybackKind playbackKind = state.playbackKind.load();
        const long long nowMs = SteadyNowMs();
        const StreamSessionState currentSessionState = state.streamState.load();
        if (currentSessionState != observedSessionState)
        {
            if (currentSessionState == StreamSessionState::Priming)
                lastPrimingStartMs = nowMs;
            else if (currentSessionState == StreamSessionState::Buffering)
                lastBufferingStartMs = nowMs;
            observedSessionState = currentSessionState;
        }
        if (resources.vt)
            lastRenderedVideoMs = std::max(lastRenderedVideoMs, resources.vt->GetLastRenderedAtMs());
        {
            std::unique_lock<std::mutex> lock(resources.mux);
            const bool hasOpenDemux = resources.demux && resources.demux->IsOpen();

            if (state.isPause.load() || !hasOpenDemux)
            {
                readyStreak = 0;
                lowWaterStreak = 0;
                state.runtimeReadyStreak.store(0);
                state.eofCount = 0;
                state.consecutiveReadFailures.store(0);
                drainIssuedSerial = 0;
                if (!hasOpenDemux)
                {
                    const StreamSessionState sessionState = state.streamState.load();
                    if (state.currentUrl.empty()
                        && sessionState != StreamSessionState::Opening
                        && sessionState != StreamSessionState::Stopped
                        && sessionState != StreamSessionState::Failed
                        && sessionState != StreamSessionState::Idle)
                    {
                        sessionCore.TransitionToIdleLocked();
                    }
                }
                lock.unlock();
                QThread::msleep(5);
                continue;
            }
        }

        UpdatePlaybackClock();

        if (playbackKind == StreamPlaybackKind::NetworkVod || playbackKind == StreamPlaybackKind::Live)
        {
            const BufferProfile profile = BuildBufferProfile(state, resources);
            const int profileLevel = ComputeBufferProfileLevel(state, playbackKind);
            if (IsBufferLow(profile, resources))
                ++lowWaterStreak;
            else
                lowWaterStreak = 0;

            PublishBufferProfile(state, profile, profileLevel, readyStreak, lowWaterStreak);
            if (ShouldStartLowWaterBuffering(
                    state, resources, lowWaterStreak, nowMs, lastPlaybackResumeMs, lastRenderedVideoMs))
            {
                readyStreak = 0;
                state.runtimeReadyStreak.store(0);
                SetConsumerPause(true);
                {
                    std::lock_guard<std::mutex> lock(resources.mux);
                    sessionCore.TransitionToBufferingLocked(true);
                    if (playbackKind == StreamPlaybackKind::Live)
                        liveController.UpdateRuntimeTuningLocked(false);
                    lastBufferingStartMs = nowMs;
                    state.statusEventText = std::string("Low-water buffering: ")
                        + BufferReasonLabel(playbackKind)
                        + " | streak "
                        + std::to_string(lowWaterStreak);
                    state.statusEventGeneration.fetch_add(1);
                }
            }
        }
        else
        {
            lowWaterStreak = 0;
            PublishBufferProfile(state, BuildBufferProfile(state, resources), 0, readyStreak, lowWaterStreak);
        }

        MaybeAdvanceBufferedPlayback(
            readyStreak, nowMs, lastPlaybackResumeMs, lastBufferingStartMs, lastPrimingStartMs);
        if (state.streamState.load() != StreamSessionState::Playing)
            lowWaterStreak = 0;

        AVPacket* pkt = nullptr;
        Demux* localDemux = nullptr;
        quint64 packetGeneration = 0;
        quint64 packetSerial = 0;
        ReadNextPacket(pkt, localDemux, packetGeneration, packetSerial);

        if (!pkt)
        {
            state.eofCount++;
            state.consecutiveReadFailures.fetch_add(1);

            if (playbackKind == StreamPlaybackKind::Live)
            {
                HandleLiveReadMiss(readyStreak, nowMs, lastBufferingStartMs);
            }
            else if (playbackKind == StreamPlaybackKind::NetworkVod)
            {
                HandleNetworkVodReadMiss(
                    packetGeneration, packetSerial, drainIssuedSerial, readyStreak, nowMs, lastBufferingStartMs);
            }
            else
            {
                HandleFileReadMiss(packetGeneration, packetSerial, drainIssuedSerial, readyStreak);
            }
            continue;
        }

        HandlePacketReadSuccess();
        drainIssuedSerial = 0;
        const bool isAudioPacket = localDemux && localDemux->IsAudio(pkt);
        if (resources.recorder)
            resources.recorder->OnPacket(pkt, isAudioPacket);
        DispatchPacket(pkt, isAudioPacket, packetGeneration, packetSerial);
    }
}
