#pragma once

#include <QObject>
#include <QString>

#include "../ai/ai_model_manager.h"
#include "../archive/archive_models.h"
#include "../media/demux_types.h"
#include "../recording/recording_models.h"
#include "../session/stream_config.h"
#include "../../features/detector/detector_types.h"

#include <functional>
#include <memory>

class DemuxThread;
class Demux;
class VideoCallback;

class PlayerCore
{
public:

    using AsrSubtitleHandler = std::function<void(
        const QString& text, long long startMs, long long endMs, quint64 generation, quint64 serial)>;

    using DetectorResultHandler = std::function<void(DetectionResult result)>;

    using DetectorModelReadyHandler = std::function<void(bool success)>;

    PlayerCore();
    ~PlayerCore();

    void Start();
    void Close();
    void DisableAllAiFeatures();

    bool Open(const char* url, const std::shared_ptr<VideoCallback>& call);

    bool OpenPrepared(Demux* preparedDemux, const char* url,
        const StreamOpenOptions& options, const std::shared_ptr<VideoCallback>& call,
        int measuredOpenLatencyMs);

    void SetPause(bool isPause);
    void SetVolume(double vol);
    void SetSpeed(double speed);
    void Seek(double pos);

    PlaybackSessionSnapshot GetSessionSnapshot();
    void ClearError();

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

    bool SetFeatureEnabled(const AiFeatureConfig& config, bool enable, std::string* error = nullptr);
    bool SetFeatureEnabled(AiCapability capability, bool enable, std::string* error = nullptr);
    bool IsFeatureEnabled(AiCapability capability) const;

    AiModelRecord GetAiModelRecord(AiCapability capability) const;
    StreamEpoch GetFeatureEpoch(AiCapability capability) const;

    void UpdateAiRuntimeHints(
        AiPriorityTier priorityTier,
        bool focusRoute,
        bool alarmRoute,
        bool fullscreenRoute,
        int detectorMinimumSkipFrames);

    bool StartRecording(const RecordingConfiguration& configuration, std::string* error = nullptr);
    void StopRecording();
    bool IsRecording() const;
    RecordingRuntimeSnapshot GetRecordingSnapshot() const;

    bool RecordArchiveEvent(ArchiveEventRecord* record, std::string* error = nullptr);

    void ClearAsrOutput();
    void BindAsrSubtitleHandler(QObject* context, AsrSubtitleHandler handler);
    void BindDetectorResultHandler(QObject* context, DetectorResultHandler handler);
    void BindDetectorModelReadyHandler(QObject* context, DetectorModelReadyHandler handler);

private:

    std::unique_ptr<DemuxThread> demux_;
};
