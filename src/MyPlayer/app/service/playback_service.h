#pragma once

#include "playback_service_interfaces.h"

#include <memory>

class PlayerCore;

class PlaybackService
    : public IPlaybackSessionService
    , public IPlaybackTrackService
    , public IPlaybackStatsService
    , public IPlaybackFeatureService
    , public IPlaybackArchiveService
{
public:

    PlaybackService();
    ~PlaybackService() override;

    void Start() override;
    void Close() override;
    void DisableAllAiFeatures() override;

    bool Open(const char* url, VideoCallback* call) override;
    bool OpenPrepared(Demux* preparedDemux, const char* url,
        const StreamOpenOptions& options, VideoCallback* call,
        int measuredOpenLatencyMs) override;

    void SetPause(bool isPause) override;
    void SetVolume(double vol) override;
    void SetSpeed(double speed) override;
    void Seek(double pos) override;

    PlaybackSessionSnapshot GetSessionSnapshot() override;
    void ClearError() override;

    PlaybackMediaSnapshot GetMediaSnapshot() override;
    int FetchRenderedFrames() override;
    std::string GetOsdDetail() override;
    void SetNetworkOpenOptions(const StreamOpenOptions& options) override;
    StreamOpenOptions GetNetworkOpenOptions() override;
    StreamOpenOptions GetActiveOpenOptions() override;
    StreamStatsSnapshot GetStreamStats() override;

    int GetAudioStreamCount() override;
    AudioStreamInfo GetAudioStreamInfo(int idx) override;
    int GetCurrentAudioIndex() override;
    bool SwitchAudioStream(int idx) override;

    int GetSubtitleStreamCount() override;
    SubtitleStreamInfo GetSubtitleStreamInfo(int idx) override;
    bool LoadSubtitleTrack(int idx, std::vector<SubtitleCueData>& cues) override;

    bool SetFeatureEnabled(const AiFeatureConfig& config, bool enable, std::string* error = nullptr) override;
    bool SetFeatureEnabled(AiCapability capability, bool enable, std::string* error = nullptr) override;
    bool IsFeatureEnabled(AiCapability capability) const override;
    AiModelRecord GetAiModelRecord(AiCapability capability) const override;
    StreamEpoch GetFeatureEpoch(AiCapability capability) const override;
    void ClearAsrOutput() override;

    void BindAsrSubtitleHandler(QObject* context, AsrSubtitleHandler handler) override;
    void BindDetectorResultHandler(QObject* context, DetectorResultHandler handler) override;
    void BindDetectorModelReadyHandler(QObject* context, DetectorModelReadyHandler handler) override;

    bool StartRecording(const RecordingConfiguration& configuration, std::string* error = nullptr) override;
    void StopRecording() override;
    bool IsRecording() const override;
    bool RecordArchiveEvent(const ArchiveEventRecord& record, std::string* error = nullptr) override;

private:

    std::unique_ptr<PlayerCore> core_;
};
