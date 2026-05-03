#include "stream_session_core.h"

#include "../audio/audio_thread.h"
#include "../media/demux.h"
#include "../recording/session_recorder.h"
#include "../../common/diagnostics/logger.h"
#include "../../features/detector/detector_thread.h"
#include "../video/video_thread.h"
#include "../../features/asr/whisper_thread.h"

#include <QThread>

#include <algorithm>

void StreamSessionCore::ShutdownResources()
{
    VideoThread* vt = nullptr;
    AudioThread* at = nullptr;
    WhisperThread* wt = nullptr;
    DetectorThread* det = nullptr;
    Demux* demux = nullptr;

    {
        std::lock_guard<std::mutex> lock(resources.mux);

        vt = resources.vt;
        at = resources.at;
        wt = resources.wt;
        det = resources.det;
        demux = resources.demux;
        resources.vt = nullptr;
        resources.at = nullptr;
        resources.wt = nullptr;
        resources.det = nullptr;
        resources.demux = nullptr;
        resources.currentCall.reset();

        state.currentUrl.clear();
        state.lastError.clear();
        state.isLiveStream.store(false);
        state.isBuffering.store(false);
        state.playbackKind.store(StreamPlaybackKind::File);
        state.sourceType.store(StreamSourceType::LocalFile);
        state.reconnectCount = 0;
        state.openLatencyMs.store(0);
        state.reconnectSuccessCount.store(0);
        state.bufferingEventCount.store(0);
        state.consecutiveReadFailures.store(0);
        state.adaptiveRecoveryStage = 0;
        state.adaptiveHint.clear();
        state.liveTuneStage = 0;
        state.liveTuneProfile.clear();
        state.liveTuneHint.clear();
        state.runtimeAudioTargetMs = 0;
        state.runtimeVideoLeadMs = state.activeOpenOptions.videoLeadMs;
        state.runtimeLateDropMs = state.activeOpenOptions.lateVideoDropMs;
        state.runtimeStartupVideoPackets.store(0);
        state.runtimeStartupAudioPackets.store(0);
        state.runtimeResumeVideoPackets.store(0);
        state.runtimeResumeAudioPackets.store(0);
        state.runtimeLowWaterVideoPackets.store(0);
        state.runtimeLowWaterAudioPackets.store(0);
        state.runtimeStartupAudioBufferedMs.store(0);
        state.runtimeResumeAudioBufferedMs.store(0);
        state.runtimeLowWaterAudioBufferedMs.store(0);
        state.runtimeBufferProfileLevel.store(0);
        state.runtimeReadyStreak.store(0);
        state.runtimeReadyTarget.store(0);
        state.runtimeRebufferCooldownMs.store(0);
        state.runtimeMinBufferHoldMs.store(0);
        state.runtimeLowWaterStreak.store(0);
        state.runtimePlaybackResumeCount.store(0);
        state.runtimeRebufferSuppressedCount.store(0);
        state.aiGpuQueueDepth.store(0);
        state.aiGpuActiveTasks.store(0);
        state.aiCpuQueueDepth.store(0);
        state.aiCpuActiveTasks.store(0);
        state.aiCompletedTasks.store(0);
        state.aiDroppedTasks.store(0);
        state.aiCancelledTasks.store(0);
        state.aiDetectorDroppedTasks.store(0);
        state.aiDetectorCancelledTasks.store(0);
        state.aiLastWaitMs.store(0);
        state.aiAverageWaitMs.store(0);
        state.aiAccumulatedWaitMs.store(0);
        state.aiAcquireCount.store(0);
        state.runtimeDetectorSkipFrames = 0;
        state.runtimeDetectorBaseSkipFrames = 0;
        state.statusEventText.clear();
        state.statusEventGeneration.store(0);
        state.lastAdaptiveActionMs.store(0);
        state.lastLiveTuneMs = 0;
        state.primingPending.store(false);
        TransitionToStoppedLocked();
    }

    if (vt)
        vt->Close();
    if (at)
        at->Close();
    if (wt)
        wt->Stop();
    if (det)
        det->Stop();
    if (resources.recorder)
        resources.recorder->OnSessionClosed();

    delete vt;
    delete at;
    delete wt;
    delete det;
    delete demux;
}

void StreamSessionCore::Clear()
{
    std::lock_guard<std::mutex> lock(resources.mux);
    if (resources.recorder)
        resources.recorder->OnSessionClosed();
    FlushDemuxLocked();
    FlushDecodeLocked();
    state.primingPending.store(false);
    state.runtimeStartupVideoPackets.store(0);
    state.runtimeStartupAudioPackets.store(0);
    state.runtimeResumeVideoPackets.store(0);
    state.runtimeResumeAudioPackets.store(0);
    state.runtimeLowWaterVideoPackets.store(0);
    state.runtimeLowWaterAudioPackets.store(0);
    state.runtimeStartupAudioBufferedMs.store(0);
    state.runtimeResumeAudioBufferedMs.store(0);
    state.runtimeLowWaterAudioBufferedMs.store(0);
    state.runtimeBufferProfileLevel.store(0);
    state.runtimeReadyStreak.store(0);
    state.runtimeReadyTarget.store(0);
    state.runtimeRebufferCooldownMs.store(0);
    state.runtimeMinBufferHoldMs.store(0);
    state.runtimeLowWaterStreak.store(0);
    state.runtimePlaybackResumeCount.store(0);
    state.runtimeRebufferSuppressedCount.store(0);
    state.aiGpuQueueDepth.store(0);
    state.aiGpuActiveTasks.store(0);
    state.aiCpuQueueDepth.store(0);
    state.aiCpuActiveTasks.store(0);
    state.aiCompletedTasks.store(0);
    state.aiDroppedTasks.store(0);
    state.aiCancelledTasks.store(0);
    state.aiDetectorDroppedTasks.store(0);
    state.aiDetectorCancelledTasks.store(0);
    state.aiLastWaitMs.store(0);
    state.aiAverageWaitMs.store(0);
    state.aiAccumulatedWaitMs.store(0);
    state.aiAcquireCount.store(0);
    const StreamEpoch epoch = AdvanceEpochLocked();
    ApplyEpochResetLocked(epoch, true, true);
}

void StreamSessionCore::AcknowledgeError()
{
    std::lock_guard<std::mutex> lock(resources.mux);
    state.hasError.store(false);
    if (state.streamState.load() == StreamSessionState::Failed)
        TransitionToStoppedLocked();
}

void StreamSessionCore::SetPause(bool isPauseValue)
{
    std::lock_guard<std::mutex> lock(resources.mux);
    state.isPause.store(isPauseValue);
    if (resources.at)
        resources.at->SetPause(isPauseValue);
    if (resources.vt)
        resources.vt->SetPause(isPauseValue);

    if (!isPauseValue && state.primingPending.load())
    {
        SetChildThreadsPauseLocked(true);
        TransitionToPrimingLocked();
        return;
    }

    switch (state.streamState.load())
    {
    case StreamSessionState::Opening:
    case StreamSessionState::Priming:
    case StreamSessionState::Seeking:
    case StreamSessionState::Buffering:
    case StreamSessionState::Reconnecting:
    case StreamSessionState::Draining:
    case StreamSessionState::Eof:
    case StreamSessionState::Failed:
    case StreamSessionState::Stopped:
        break;
    default:
        TransitionToPlaybackLocked();
        break;
    }
}

void StreamSessionCore::SetVolume(double vol)
{
    std::lock_guard<std::mutex> lock(resources.mux);
    if (resources.at)
        resources.at->SetVolume(vol);
}

void StreamSessionCore::SetSpeed(double speed)
{
    std::lock_guard<std::mutex> lock(resources.mux);
    if (state.isLiveStream.load())
        speed = 1.0;
    if (resources.at)
        resources.at->SetSpeed(speed);
}

bool StreamSessionCore::StartRecordingLocked(const RecordingConfiguration& configuration, std::string* error)
{
    if (!resources.recorder)
    {
        if (error)
            *error = "Session recorder is not available.";
        return false;
    }

    const bool started = resources.recorder->StartRecording(configuration, error);
    if (started && resources.demux && resources.demux->IsOpen())
        resources.recorder->OnSessionOpened(*resources.demux, state.currentUrl, error);
    return started;
}

void StreamSessionCore::StopRecordingLocked()
{
    if (resources.recorder)
        resources.recorder->StopRecording();
}

bool StreamSessionCore::IsRecordingLocked() const
{
    return resources.recorder && resources.recorder->IsRecording();
}

bool StreamSessionCore::RecordArchiveEventLocked(ArchiveEventRecord* record, std::string* error)
{
    if (!resources.recorder)
    {
        if (error)
            *error = "Session recorder is not available.";
        return false;
    }

    return resources.recorder->RecordEvent(record, error);
}

bool StreamSessionCore::SwitchAudioStream(int idx)
{
    const bool wasPaused = state.isPause.load();
    long long resumePts = 0;
    int previousAudioIndex = -1;
    bool switchApplied = false;
    bool pipelineReady = false;
    bool shouldStartAudioThread = false;

    {
        std::lock_guard<std::mutex> lock(resources.mux);
        if (!CanSeekLocked() || !resources.demux || !resources.at)
            return false;

        resumePts = std::clamp(state.pts.load(), 0LL, static_cast<long long>(state.totalMs.load()));
        previousAudioIndex = resources.demux->GetCurrentAudioIndex();
        if (idx < 0 || idx >= resources.demux->GetAudioStreamCount())
            return false;
        if (previousAudioIndex == idx)
            return true;

        Logger::Instance().Log(
            LogLevel::Info,
            "session",
            "audio_track.switch.begin",
            "Switching audio track",
            {
                { "from", std::to_string(previousAudioIndex) },
                { "to", std::to_string(idx) },
                { "url", state.currentUrl },
            });

        state.isComplete.store(false);
        state.eofCount = 0;
        TransitionToSeekingLocked();
    }

    SetPause(true);
    QThread::msleep(10);

    {
        std::lock_guard<std::mutex> lock(resources.mux);
        if (!resources.demux || !resources.at)
            return false;

        FlushDemuxLocked();
        FlushDecodeLocked();
        const StreamEpoch switchEpoch = AdvanceEpochLocked();
        ApplyEpochResetLocked(switchEpoch, true, true);

        if (resources.at->isRunning())
            resources.at->Close();

        auto openAudioStream = [this](int streamIndex) -> bool
        {
            if (!resources.demux || !resources.at)
                return false;

            resources.demux->SetAudioStream(streamIndex);
            const bool opened = resources.at->Open(resources.demux->CopyAPara(),
                resources.demux->sampleRate.load(), resources.demux->channels.load());
            state.audioPlaybackAvailable.store(opened);
            state.audioSampleRate.store(resources.demux->sampleRate.load());
            state.audioChannels.store(resources.demux->channels.load());
            return opened;
        };

        int activeAudioIndex = idx;
        bool audioOpened = openAudioStream(activeAudioIndex);
        if (!audioOpened && previousAudioIndex >= 0)
        {
            activeAudioIndex = previousAudioIndex;
            audioOpened = openAudioStream(activeAudioIndex);
            state.lastError = "Failed to switch audio track";
        }

        if (audioOpened)
        {
            pipelineReady = resources.demux->SeekMs(resumePts);
            if (!pipelineReady)
                state.lastError = "Failed to seek after audio track switch";
        }
        else
        {
            state.lastError = "Failed to switch audio track";
        }

        switchApplied = pipelineReady && activeAudioIndex == idx;
        shouldStartAudioThread = audioOpened && !resources.at->isRunning();
        SyncPlaybackPositionLocked(resumePts);
        state.primingPending.store(true);
    }

    if (shouldStartAudioThread && resources.at && !resources.at->isRunning())
        resources.at->start();

    {
        std::lock_guard<std::mutex> lock(resources.mux);
        state.isPause.store(wasPaused);
        SetChildThreadsPauseLocked(true);
        if (wasPaused)
            TransitionToPlaybackLocked();
        else
            TransitionToPrimingLocked();
    }
    Logger::Instance().Log(
        switchApplied ? LogLevel::Info : LogLevel::Warning,
        "session",
        "audio_track.switch.end",
        switchApplied ? "Audio track switch completed" : "Audio track switch failed",
        {
            { "target", std::to_string(idx) },
            { "success", switchApplied ? "true" : "false" },
            { "url", state.currentUrl },
        });
    return switchApplied;
}

void StreamSessionCore::Seek(double pos)
{
    const bool wasPaused = state.isPause.load();
    long long seekPts = 0;
    {
        std::lock_guard<std::mutex> lock(resources.mux);
        if (!CanSeekLocked())
            return;

        state.isComplete.store(false);
        state.eofCount = 0;
        TransitionToSeekingLocked();
        seekPts = ResolveSeekTargetMsLocked(pos);
        Logger::Instance().Log(
            LogLevel::Info,
            "session",
            "seek.begin",
            "Seek requested",
            {
                { "target_ms", std::to_string(seekPts) },
                { "ratio", std::to_string(pos) },
                { "url", state.currentUrl },
            });
    }

    SetPause(true);
    QThread::msleep(10);

    {
        std::lock_guard<std::mutex> lock(resources.mux);
        FlushDemuxLocked();
        FlushDecodeLocked();
        const StreamEpoch seekEpoch = AdvanceEpochLocked();
        ApplyEpochResetLocked(seekEpoch, true, false);
        if (resources.demux)
            resources.demux->SeekMs(seekPts);
    }

    PrimeSeekPreview(seekPts);

    {
        std::lock_guard<std::mutex> lock(resources.mux);
        SyncPlaybackPositionLocked(seekPts);
        state.primingPending.store(true);
        state.isPause.store(wasPaused);
        SetChildThreadsPauseLocked(true);
        if (wasPaused)
            TransitionToPlaybackLocked();
        else
            TransitionToPrimingLocked();
    }
    Logger::Instance().Log(
        LogLevel::Info,
        "session",
        "seek.end",
        "Seek completed",
        {
            { "target_ms", std::to_string(seekPts) },
            { "url", state.currentUrl },
        });
}
