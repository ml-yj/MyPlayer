#pragma once

#include <functional>
#include <memory>
#include <string>

#include "demux_thread_shared.h"
#include "../archive/archive_models.h"
#include "../recording/recording_models.h"
#include "../media/demux_types.h"

class Demux;
class VideoCallback;

class StreamSessionCore
{
public:

    StreamSessionCore(DemuxThreadState& state, SessionResources& resources);

    void SetLiveTuningCallbacks(std::function<void()> resetLiveRuntimeTuning,
        std::function<void(bool)> updateLiveRuntimeTuning);
    void SetAiEpochRebindCallback(std::function<void(const StreamEpoch&)> rebindAiEpochLocked);

    bool Open(const char* url, const std::shared_ptr<VideoCallback>& call);

    bool OpenPrepared(Demux* preparedDemux, const char* url, const StreamOpenOptions& options,
        const std::shared_ptr<VideoCallback>& call, int measuredOpenLatencyMs);

    void EnsurePipelineObjects();

    void StartChildThreads();

    void ShutdownResources();

    void Clear();

    void AcknowledgeError();

    void Seek(double pos);

    void SetPause(bool isPause);
    void SetVolume(double vol);
    void SetSpeed(double speed);

    bool StartRecordingLocked(const RecordingConfiguration& configuration, std::string* error = nullptr);
    void StopRecordingLocked();
    bool IsRecordingLocked() const;
    bool RecordArchiveEventLocked(ArchiveEventRecord* record, std::string* error = nullptr);

    quint64 AdvanceGeneration();
    StreamEpoch GetEpoch() const;
    bool SwitchAudioStream(int idx);

    void UpdateStreamMetadataLocked();
    void ApplyRuntimeOptionsLocked(const StreamOpenOptions& selectedOptions, bool isNetworkStream);
    bool ReopenLocked(const StreamOpenOptions& options, bool countReconnectSuccess);
    void TransitionToOpeningLocked();
    void TransitionToPrimingLocked();
    void TransitionToPlaybackLocked();
    void TransitionToSeekingLocked();
    void TransitionToBufferingLocked(bool countEvent);
    void TransitionToReconnectingLocked();
    void TransitionToDrainingLocked();
    void TransitionToEofLocked();
    void TransitionToIdleLocked();
    void TransitionToStoppedLocked();
    void TransitionToFailedLocked(const std::string& error, bool markComplete);

    void SetNetworkOpenOptions(const StreamOpenOptions& options);
    StreamOpenOptions GetNetworkOpenOptions() const;
    StreamOpenOptions GetActiveOpenOptions() const;
    std::string GetCurrentUrl() const;
    std::string GetLastError() const;

private:

    void EnsurePipelineObjectsLocked();
    StreamEpoch CurrentEpochLocked() const;
    StreamEpoch AdvanceEpochLocked();
    void ApplyEpochLocked(const StreamEpoch& epoch);
    void RebindAiEpochLocked(const StreamEpoch& epoch);
    void SetChildThreadsPauseLocked(bool paused);
    bool CanSeekLocked() const;
    long long ResolveSeekTargetMsLocked(double pos) const;
    void FlushDemuxLocked();
    void FlushDecodeLocked();
    void ApplyEpochResetLocked(const StreamEpoch& epoch, bool enqueueAudioFlush, bool enqueueVideoFlush);
    bool PrimeSeekPreview(long long seekPts);
    void SyncPlaybackPositionLocked(long long pts);
    void SetStreamStateLocked(StreamSessionState newState);

    DemuxThreadState& state;
    SessionResources& resources;

    std::function<void()> resetLiveRuntimeTuning;
    std::function<void(bool)> updateLiveRuntimeTuning;

    std::function<void(const StreamEpoch&)> rebindAiEpochLocked;
};
