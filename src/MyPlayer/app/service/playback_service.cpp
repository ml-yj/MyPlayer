#include "playback_service.h"

#include "../../core/player/player_core.h"

PlaybackService::PlaybackService()

    : core_(std::make_unique<PlayerCore>())
{
}

PlaybackService::~PlaybackService() = default;

void PlaybackService::Start()
{

    core_->Start();
}

void PlaybackService::Close()
{

    core_->Close();
}

void PlaybackService::DisableAllAiFeatures()
{

    core_->DisableAllAiFeatures();
}

bool PlaybackService::Open(const char* url, VideoCallback* call)
{

    const std::shared_ptr<VideoCallback> callback(
        call,
        [](VideoCallback*) {});

    return core_->Open(url, callback);
}

bool PlaybackService::OpenPrepared(Demux* preparedDemux, const char* url,
    const StreamOpenOptions& options, VideoCallback* call,
    int measuredOpenLatencyMs)
{

    const std::shared_ptr<VideoCallback> callback(
        call,
        [](VideoCallback*) {});
    return core_->OpenPrepared(preparedDemux, url, options, callback, measuredOpenLatencyMs);
}

void PlaybackService::SetPause(bool isPause)
{

    core_->SetPause(isPause);
}

void PlaybackService::SetVolume(double vol)
{

    core_->SetVolume(vol);
}

void PlaybackService::SetSpeed(double speed)
{

    core_->SetSpeed(speed);
}

void PlaybackService::Seek(double pos)
{

    core_->Seek(pos);
}

PlaybackSessionSnapshot PlaybackService::GetSessionSnapshot()
{

    return core_->GetSessionSnapshot();
}

void PlaybackService::ClearError()
{

    core_->ClearError();
}

PlaybackMediaSnapshot PlaybackService::GetMediaSnapshot()
{

    return core_->GetMediaSnapshot();
}

int PlaybackService::FetchRenderedFrames()
{

    return core_->FetchRenderedFrames();
}

std::string PlaybackService::GetOsdDetail()
{

    return core_->GetOsdDetail();
}

void PlaybackService::SetNetworkOpenOptions(const StreamOpenOptions& options)
{

    core_->SetNetworkOpenOptions(options);
}

StreamOpenOptions PlaybackService::GetNetworkOpenOptions()
{

    return core_->GetNetworkOpenOptions();
}

StreamOpenOptions PlaybackService::GetActiveOpenOptions()
{

    return core_->GetActiveOpenOptions();
}

StreamStatsSnapshot PlaybackService::GetStreamStats()
{

    return core_->GetStreamStats();
}

int PlaybackService::GetAudioStreamCount()
{

    return core_->GetAudioStreamCount();
}

AudioStreamInfo PlaybackService::GetAudioStreamInfo(int idx)
{
    return core_->GetAudioStreamInfo(idx);
}

int PlaybackService::GetCurrentAudioIndex()
{

    return core_->GetCurrentAudioIndex();
}

bool PlaybackService::SwitchAudioStream(int idx)
{

    return core_->SwitchAudioStream(idx);
}

int PlaybackService::GetSubtitleStreamCount()
{
    return core_->GetSubtitleStreamCount();
}

SubtitleStreamInfo PlaybackService::GetSubtitleStreamInfo(int idx)
{
    return core_->GetSubtitleStreamInfo(idx);
}

bool PlaybackService::LoadSubtitleTrack(int idx, std::vector<SubtitleCueData>& cues)
{

    return core_->LoadSubtitleTrack(idx, cues);
}

bool PlaybackService::SetFeatureEnabled(const AiFeatureConfig& config, bool enable, std::string* error)
{

    return core_->SetFeatureEnabled(config, enable, error);
}

bool PlaybackService::SetFeatureEnabled(AiCapability capability, bool enable, std::string* error)
{

    return core_->SetFeatureEnabled(capability, enable, error);
}

bool PlaybackService::IsFeatureEnabled(AiCapability capability) const
{

    return core_->IsFeatureEnabled(capability);
}

AiModelRecord PlaybackService::GetAiModelRecord(AiCapability capability) const
{

    return core_->GetAiModelRecord(capability);
}

StreamEpoch PlaybackService::GetFeatureEpoch(AiCapability capability) const
{

    return core_->GetFeatureEpoch(capability);
}

bool PlaybackService::StartRecording(const RecordingConfiguration& configuration, std::string* error)
{

    return core_->StartRecording(configuration, error);
}

void PlaybackService::StopRecording()
{
    core_->StopRecording();
}

bool PlaybackService::IsRecording() const
{

    return core_->IsRecording();
}

bool PlaybackService::RecordArchiveEvent(const ArchiveEventRecord& record, std::string* error)
{

    ArchiveEventRecord stored = record;
    return core_->RecordArchiveEvent(&stored, error);
}

void PlaybackService::ClearAsrOutput()
{

    core_->ClearAsrOutput();
}

void PlaybackService::BindAsrSubtitleHandler(QObject* context, AsrSubtitleHandler handler)
{

    core_->BindAsrSubtitleHandler(context, std::move(handler));
}

void PlaybackService::BindDetectorResultHandler(QObject* context, DetectorResultHandler handler)
{

    core_->BindDetectorResultHandler(context, std::move(handler));
}

void PlaybackService::BindDetectorModelReadyHandler(QObject* context, DetectorModelReadyHandler handler)
{

    core_->BindDetectorModelReadyHandler(context, std::move(handler));
}
