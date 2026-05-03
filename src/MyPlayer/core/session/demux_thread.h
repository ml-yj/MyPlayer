#pragma once

#include <QPointer>
#include <QThread>

#include <memory>
#include <string>
#include <vector>

#include "../media/demux.h"
#include "../media/demux_types.h"
#include "../ai/ai_model_manager.h"
#include "../archive/archive_models.h"
#include "../recording/recording_models.h"
#include "demux_thread_shared.h"

class AudioThread;
class DetectorThread;
class LiveStreamController;
class MediaFeatureBridge;
class PacketPump;
class StreamSessionCore;
class StreamStatsReporter;
class VideoThread;
class WhisperThread;
class SessionRecorder;

class DemuxThread : public QThread
{
public:

    DemuxThread();
    ~DemuxThread() override;

    bool Open(const char* url, const std::shared_ptr<VideoCallback>& call);

    bool OpenPrepared(Demux* preparedDemux, const char* url,
        const StreamOpenOptions& options, const std::shared_ptr<VideoCallback>& call,
        int measuredOpenLatencyMs);

    void Start();

    void Close();

    void DisableAllAiFeatures();

    void Clear();

    void AcknowledgeError();

    void Seek(double pos);

    void SetPause(bool isPause);
    void SetVolume(double vol);
    void SetSpeed(double speed);

    void run() override;

    PlaybackSessionSnapshot GetSessionSnapshot();
    PlaybackMediaSnapshot GetMediaSnapshot();
    int FetchRenderedFrames();
    std::string GetOsdDetail();
    void SetNetworkOpenOptions(const StreamOpenOptions& options);
    StreamOpenOptions GetNetworkOpenOptions();
    StreamOpenOptions GetActiveOpenOptions();
    StreamStatsSnapshot GetStreamStats();

    int GetAudioStreamCount();
    AudioStreamInfo GetAudioStreamInfo(int idx);
    int GetCurrentAudioIndex();
    bool SwitchAudioStream(int idx);

    int GetSubtitleStreamCount();
    SubtitleStreamInfo GetSubtitleStreamInfo(int idx);
    bool LoadSubtitleTrack(int idx, std::vector<SubtitleCueData>& cues);

    bool SetAiCapabilityEnabled(const AiFeatureConfig& config, bool enable, std::string* error = nullptr);
    bool SetAiCapabilityEnabled(AiCapability capability, bool enable, std::string* error = nullptr);
    bool IsAiCapabilityEnabled(AiCapability capability) const;
    StreamEpoch GetAiCapabilityEpoch(AiCapability capability) const;

    void UpdateAiRuntimeHints(
        AiPriorityTier priorityTier,
        bool focusRoute,
        bool alarmRoute,
        bool fullscreenRoute,
        int detectorMinimumSkipFrames);

    void EnableWhisper(bool enable, const std::string& modelPath);
    bool IsWhisperEnabled() const;
    AiModelRecord GetAiModelRecord(AiCapability capability) const;
    QPointer<WhisperThread> GetWhisperThread() const;
    StreamEpoch GetWhisperEpoch();

    void EnableDetector(bool enable, const std::string& modelPath, const std::string& labelsPath = "");
    bool IsDetectorEnabled() const;
    QPointer<DetectorThread> GetDetectorThread() const;

    bool StartRecording(const RecordingConfiguration& configuration, std::string* error = nullptr);
    void StopRecording();
    bool IsRecording() const;
    RecordingRuntimeSnapshot GetRecordingSnapshot() const;
    bool RecordArchiveEvent(ArchiveEventRecord* record, std::string* error = nullptr);

private:

    DemuxThreadState state;

    SessionResources resources;

    std::unique_ptr<StreamStatsReporter> statsReporter;
    std::unique_ptr<StreamSessionCore> sessionCore;
    std::unique_ptr<LiveStreamController> liveController;
    std::unique_ptr<MediaFeatureBridge> featureBridge;
    std::unique_ptr<PacketPump> packetPump;
    std::unique_ptr<SessionRecorder> recorder;
};
