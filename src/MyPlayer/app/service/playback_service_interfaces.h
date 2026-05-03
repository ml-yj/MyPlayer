#pragma once

#include <QObject>
#include <QString>
#include <QtGlobal>

#include "../../core/ai/ai_model_manager.h"
#include "../../core/archive/archive_models.h"
#include "../../core/media/demux_types.h"
#include "../../core/recording/recording_models.h"
#include "../../core/session/stream_config.h"
#include "../../features/detector/detector_types.h"

#include <functional>
#include <string>
#include <vector>

class Demux;
class VideoCallback;

class IPlaybackSessionService
{
public:
    virtual ~IPlaybackSessionService() = default;

    virtual void Start() = 0;
    virtual void Close() = 0;

    virtual bool Open(const char* url, VideoCallback* call) = 0;

    virtual bool OpenPrepared(Demux* preparedDemux, const char* url,
                              const StreamOpenOptions& options, VideoCallback* call,
                              int measuredOpenLatencyMs) = 0;

    virtual void SetPause(bool isPause) = 0;
    virtual void SetVolume(double vol) = 0;
    virtual void SetSpeed(double speed) = 0;
    virtual void Seek(double pos) = 0;

    virtual PlaybackSessionSnapshot GetSessionSnapshot() = 0;
    virtual void ClearError() = 0;

    virtual void SetNetworkOpenOptions(const StreamOpenOptions& options) = 0;
    virtual StreamOpenOptions GetNetworkOpenOptions() = 0;
    virtual StreamOpenOptions GetActiveOpenOptions() = 0;
};

class IPlaybackTrackService
{
public:
    virtual ~IPlaybackTrackService() = default;

    virtual int GetAudioStreamCount() = 0;
    virtual AudioStreamInfo GetAudioStreamInfo(int idx) = 0;
    virtual int GetCurrentAudioIndex() = 0;
    virtual bool SwitchAudioStream(int idx) = 0;

    virtual int GetSubtitleStreamCount() = 0;
    virtual SubtitleStreamInfo GetSubtitleStreamInfo(int idx) = 0;

    virtual bool LoadSubtitleTrack(int idx, std::vector<SubtitleCueData>& cues) = 0;
};

class IPlaybackStatsService
{
public:
    virtual ~IPlaybackStatsService() = default;

    virtual PlaybackMediaSnapshot GetMediaSnapshot() = 0;
    virtual int FetchRenderedFrames() = 0;
    virtual std::string GetOsdDetail() = 0;
    virtual StreamStatsSnapshot GetStreamStats() = 0;
};

class IPlaybackFeatureService
{
public:

    using AsrSubtitleHandler = std::function<void(
        const QString& text, long long startMs, long long endMs, quint64 generation, quint64 serial)>;

    using DetectorResultHandler = std::function<void(DetectionResult result)>;

    using DetectorModelReadyHandler = std::function<void(bool success)>;

    virtual ~IPlaybackFeatureService() = default;

    virtual void DisableAllAiFeatures() = 0;

    virtual bool SetFeatureEnabled(const AiFeatureConfig& config, bool enable, std::string* error = nullptr) = 0;
    virtual bool SetFeatureEnabled(AiCapability capability, bool enable, std::string* error = nullptr) = 0;

    virtual bool IsFeatureEnabled(AiCapability capability) const = 0;
    virtual AiModelRecord GetAiModelRecord(AiCapability capability) const = 0;
    virtual StreamEpoch GetFeatureEpoch(AiCapability capability) const = 0;

    virtual void ClearAsrOutput() = 0;

    virtual void BindAsrSubtitleHandler(QObject* context, AsrSubtitleHandler handler) = 0;
    virtual void BindDetectorResultHandler(QObject* context, DetectorResultHandler handler) = 0;
    virtual void BindDetectorModelReadyHandler(QObject* context, DetectorModelReadyHandler handler) = 0;
};

class IPlaybackArchiveService
{
public:
    virtual ~IPlaybackArchiveService() = default;

    virtual bool StartRecording(const RecordingConfiguration& configuration, std::string* error = nullptr) = 0;
    virtual void StopRecording() = 0;
    virtual bool IsRecording() const = 0;

    virtual bool RecordArchiveEvent(const ArchiveEventRecord& record, std::string* error = nullptr) = 0;
};
