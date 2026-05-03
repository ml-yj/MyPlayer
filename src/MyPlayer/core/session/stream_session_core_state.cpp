#include "stream_session_core.h"

#include "../audio/audio_thread.h"
#include "../media/decode.h"
#include "../media/demux.h"
#include "../../common/diagnostics/logger.h"
#include "../video/video_thread.h"

extern "C" {
#include <libavcodec/packet.h>
}

StreamEpoch StreamSessionCore::CurrentEpochLocked() const
{
    return StreamEpoch{ state.generation.load(), state.serial.load() };
}

void StreamSessionCore::EnsurePipelineObjectsLocked()
{
    if (!resources.vt)
        resources.vt = new VideoThread();
    if (!resources.at)
        resources.at = new AudioThread();
}

void StreamSessionCore::TransitionToOpeningLocked()
{
    state.isComplete.store(false);
    state.hasError.store(false);
    state.isBuffering.store(false);
    state.primingPending.store(false);
    state.lastError.clear();
    SetStreamStateLocked(StreamSessionState::Opening);
}

void StreamSessionCore::TransitionToPrimingLocked()
{
    state.isComplete.store(false);
    state.isBuffering.store(false);
    SetStreamStateLocked(StreamSessionState::Priming);
}

void StreamSessionCore::TransitionToPlaybackLocked()
{
    state.isComplete.store(false);
    state.isBuffering.store(false);
    SetStreamStateLocked(state.isPause.load() ? StreamSessionState::Paused : StreamSessionState::Playing);
}

void StreamSessionCore::TransitionToSeekingLocked()
{
    state.isComplete.store(false);
    state.isBuffering.store(false);
    SetStreamStateLocked(StreamSessionState::Seeking);
}

void StreamSessionCore::TransitionToBufferingLocked(bool countEvent)
{
    const bool wasBuffering = state.isBuffering.exchange(true);
    if (countEvent && !wasBuffering)
        state.bufferingEventCount.fetch_add(1);
    state.isComplete.store(false);
    SetStreamStateLocked(StreamSessionState::Buffering);
}

void StreamSessionCore::TransitionToReconnectingLocked()
{
    state.isComplete.store(false);
    state.isBuffering.store(true);
    SetStreamStateLocked(StreamSessionState::Reconnecting);
}

void StreamSessionCore::TransitionToDrainingLocked()
{
    state.isComplete.store(false);
    state.isBuffering.store(false);
    SetStreamStateLocked(StreamSessionState::Draining);
}

void StreamSessionCore::TransitionToEofLocked()
{
    state.primingPending.store(false);
    state.isBuffering.store(false);
    state.isComplete.store(true);
    SetStreamStateLocked(StreamSessionState::Eof);
}

void StreamSessionCore::TransitionToIdleLocked()
{
    state.primingPending.store(false);
    state.isBuffering.store(false);
    state.isComplete.store(false);
    state.hasError.store(false);
    SetStreamStateLocked(StreamSessionState::Idle);
}

void StreamSessionCore::TransitionToStoppedLocked()
{
    state.primingPending.store(false);
    state.isBuffering.store(false);
    state.isComplete.store(false);
    state.hasError.store(false);
    SetStreamStateLocked(StreamSessionState::Stopped);
}

void StreamSessionCore::TransitionToFailedLocked(const std::string& error, bool markComplete)
{
    if (!error.empty())
        state.lastError = error;
    state.primingPending.store(false);
    state.hasError.store(true);
    state.isBuffering.store(false);
    state.isComplete.store(markComplete);
    SetStreamStateLocked(StreamSessionState::Failed);
    Logger::Instance().Log(
        LogLevel::Error,
        "session",
        "transition.failed",
        error.empty() ? "Session entered failed state" : error,
        {
            { "url", state.currentUrl },
            { "mark_complete", markComplete ? "true" : "false" },
            { "generation", std::to_string(state.generation.load()) },
            { "serial", std::to_string(state.serial.load()) },
        });
}

void StreamSessionCore::SetStreamStateLocked(StreamSessionState newState)
{
    const StreamSessionState oldState = state.streamState.load();
    if (oldState == newState)
        return;

    state.streamState.store(newState);
    Logger::Instance().Log(
        LogLevel::Info,
        "session",
        "state.transition",
        std::string(StreamSessionStateName(oldState)) + " -> " + StreamSessionStateName(newState),
        {
            { "from", StreamSessionStateName(oldState) },
            { "to", StreamSessionStateName(newState) },
            { "url", state.currentUrl },
            { "generation", std::to_string(state.generation.load()) },
            { "serial", std::to_string(state.serial.load()) },
        });
}

StreamEpoch StreamSessionCore::AdvanceEpochLocked()
{
    return StreamEpoch{
        state.generation.fetch_add(1) + 1,
        state.serial.fetch_add(1) + 1,
    };
}

void StreamSessionCore::ApplyEpochLocked(const StreamEpoch& epoch)
{
    if (resources.vt)
        resources.vt->SetQueueEpoch(epoch);
    if (resources.at)
        resources.at->SetQueueEpoch(epoch);
    RebindAiEpochLocked(epoch);
}

void StreamSessionCore::RebindAiEpochLocked(const StreamEpoch& epoch)
{
    if (rebindAiEpochLocked)
        rebindAiEpochLocked(epoch);
}

void StreamSessionCore::SetChildThreadsPauseLocked(bool paused)
{
    if (resources.at)
        resources.at->SetPause(paused);
    if (resources.vt)
        resources.vt->SetPause(paused);
}

bool StreamSessionCore::CanSeekLocked() const
{
    const StreamPlaybackKind kind = state.playbackKind.load();
    return resources.demux
        && (kind == StreamPlaybackKind::File || kind == StreamPlaybackKind::NetworkVod)
        && resources.demux->totalMs.load() > 0;
}

long long StreamSessionCore::ResolveSeekTargetMsLocked(double pos) const
{
    if (!resources.demux)
        return 0;

    if (pos < 0.0)
        pos = 0.0;
    if (pos > 1.0)
        pos = 1.0;

    return static_cast<long long>(pos * resources.demux->totalMs.load());
}

void StreamSessionCore::FlushDemuxLocked()
{
    if (resources.demux)
        resources.demux->Clear();
}

void StreamSessionCore::FlushDecodeLocked()
{
    if (resources.vt)
        resources.vt->Clear();
    if (resources.at)
        resources.at->Clear();
}

void StreamSessionCore::ApplyEpochResetLocked(const StreamEpoch& epoch,
    bool enqueueAudioFlush, bool enqueueVideoFlush)
{
    if (resources.vt)
        resources.vt->ResetQueueEpoch(epoch, enqueueVideoFlush);
    if (resources.at)
        resources.at->ResetQueueEpoch(epoch, enqueueAudioFlush);
    RebindAiEpochLocked(epoch);
}

bool StreamSessionCore::PrimeSeekPreview(long long seekPts)
{
    int seekFrames = 0;
    const int maxSeekFrames = 200;
    while (!state.isExit.load() && seekFrames < maxSeekFrames)
    {
        AVPacket* pkt = nullptr;
        {
            std::lock_guard<std::mutex> lock(resources.mux);
            if (!resources.demux)
                break;
            pkt = resources.demux->ReadVideo();
            if (!pkt)
            {
                pkt = resources.demux->Read();
                if (!pkt)
                    break;
            }
        }

        if (resources.demux && resources.demux->IsAudio(pkt))
        {
            av_packet_free(&pkt);
            continue;
        }

        ++seekFrames;
        const bool reached = resources.vt && resources.vt->RepaintPts(pkt, seekPts);
        av_packet_free(&pkt);
        if (reached)
            return true;
    }

    return false;
}

void StreamSessionCore::SyncPlaybackPositionLocked(long long pts)
{
    state.pts.store(pts);
    if (resources.at)
        resources.at->SetPts(pts);
    if (resources.vt)
    {
        resources.vt->SetSyncPts(pts);
        Decode* vDec = resources.vt->GetDecode();
        if (vDec)
            vDec->pts.store(pts);
    }
}

quint64 StreamSessionCore::AdvanceGeneration()
{
    std::lock_guard<std::mutex> lock(resources.mux);
    const StreamEpoch epoch = AdvanceEpochLocked();
    ApplyEpochLocked(epoch);
    return epoch.generation;
}

StreamEpoch StreamSessionCore::GetEpoch() const
{
    std::lock_guard<std::mutex> lock(resources.mux);
    return CurrentEpochLocked();
}

void StreamSessionCore::EnsurePipelineObjects()
{
    std::lock_guard<std::mutex> lock(resources.mux);
    EnsurePipelineObjectsLocked();
}

void StreamSessionCore::StartChildThreads()
{
    std::lock_guard<std::mutex> lock(resources.mux);
    if (resources.vt && !resources.vt->isRunning())
        resources.vt->start();
    if (resources.at && !resources.at->isRunning())
        resources.at->start();
}
