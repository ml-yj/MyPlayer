
#include "player_core.h"

#include "../session/demux_thread.h"

#include "../../features/asr/whisper_thread.h"

#include "../../features/detector/detector_thread.h"

#include <QObject>

PlayerCore::PlayerCore()

    : demux_(std::make_unique<DemuxThread>())
{

}

PlayerCore::~PlayerCore() = default;

void PlayerCore::Start()
{
    demux_->Start();
}

void PlayerCore::Close()
{
    demux_->Close();
}

void PlayerCore::DisableAllAiFeatures()
{
    demux_->DisableAllAiFeatures();
}

bool PlayerCore::Open(const char* url, const std::shared_ptr<VideoCallback>& call)
{
    return demux_->Open(url, call);
}

bool PlayerCore::OpenPrepared(Demux* preparedDemux, const char* url,
    const StreamOpenOptions& options, const std::shared_ptr<VideoCallback>& call,
    int measuredOpenLatencyMs)
{
    return demux_->OpenPrepared(preparedDemux, url, options, call, measuredOpenLatencyMs);
}

void PlayerCore::SetPause(bool isPause)
{
    demux_->SetPause(isPause);
}

void PlayerCore::SetVolume(double vol)
{
    demux_->SetVolume(vol);
}

void PlayerCore::SetSpeed(double speed)
{
    demux_->SetSpeed(speed);
}

void PlayerCore::Seek(double pos)
{
    demux_->Seek(pos);
}

PlaybackSessionSnapshot PlayerCore::GetSessionSnapshot()
{
    return demux_->GetSessionSnapshot();
}

void PlayerCore::ClearError()
{
    demux_->AcknowledgeError();
}

PlaybackMediaSnapshot PlayerCore::GetMediaSnapshot()
{
    return demux_->GetMediaSnapshot();
}

int PlayerCore::FetchRenderedFrames()
{
    return demux_->FetchRenderedFrames();
}

std::string PlayerCore::GetOsdDetail()
{
    return demux_->GetOsdDetail();
}

void PlayerCore::SetNetworkOpenOptions(const StreamOpenOptions& options)
{
    demux_->SetNetworkOpenOptions(options);
}

StreamOpenOptions PlayerCore::GetNetworkOpenOptions()
{
    return demux_->GetNetworkOpenOptions();
}

StreamOpenOptions PlayerCore::GetActiveOpenOptions()
{
    return demux_->GetActiveOpenOptions();
}

StreamStatsSnapshot PlayerCore::GetStreamStats()
{
    return demux_->GetStreamStats();
}

int PlayerCore::GetAudioStreamCount()
{
    return demux_->GetAudioStreamCount();
}

AudioStreamInfo PlayerCore::GetAudioStreamInfo(int idx)
{
    return demux_->GetAudioStreamInfo(idx);
}

int PlayerCore::GetCurrentAudioIndex()
{
    return demux_->GetCurrentAudioIndex();
}

bool PlayerCore::SwitchAudioStream(int idx)
{
    return demux_->SwitchAudioStream(idx);
}

int PlayerCore::GetSubtitleStreamCount()
{
    return demux_->GetSubtitleStreamCount();
}

SubtitleStreamInfo PlayerCore::GetSubtitleStreamInfo(int idx)
{
    return demux_->GetSubtitleStreamInfo(idx);
}

bool PlayerCore::LoadSubtitleTrack(int idx, std::vector<SubtitleCueData>& cues)
{
    return demux_->LoadSubtitleTrack(idx, cues);
}

bool PlayerCore::SetFeatureEnabled(const AiFeatureConfig& config, bool enable, std::string* error)
{
    return demux_->SetAiCapabilityEnabled(config, enable, error);
}

bool PlayerCore::SetFeatureEnabled(AiCapability capability, bool enable, std::string* error)
{
    return demux_->SetAiCapabilityEnabled(capability, enable, error);
}

bool PlayerCore::IsFeatureEnabled(AiCapability capability) const
{
    return demux_->IsAiCapabilityEnabled(capability);
}

AiModelRecord PlayerCore::GetAiModelRecord(AiCapability capability) const
{
    return demux_->GetAiModelRecord(capability);
}

StreamEpoch PlayerCore::GetFeatureEpoch(AiCapability capability) const
{
    return demux_->GetAiCapabilityEpoch(capability);
}

void PlayerCore::UpdateAiRuntimeHints(
    AiPriorityTier priorityTier,
    bool focusRoute,
    bool alarmRoute,
    bool fullscreenRoute,
    int detectorMinimumSkipFrames)
{

    demux_->UpdateAiRuntimeHints(
        priorityTier,
        focusRoute,
        alarmRoute,
        fullscreenRoute,
        detectorMinimumSkipFrames);
}

bool PlayerCore::StartRecording(const RecordingConfiguration& configuration, std::string* error)
{
    return demux_->StartRecording(configuration, error);
}

void PlayerCore::StopRecording()
{
    demux_->StopRecording();
}

bool PlayerCore::IsRecording() const
{
    return demux_->IsRecording();
}

RecordingRuntimeSnapshot PlayerCore::GetRecordingSnapshot() const
{
    return demux_->GetRecordingSnapshot();
}

bool PlayerCore::RecordArchiveEvent(ArchiveEventRecord* record, std::string* error)
{
    return demux_->RecordArchiveEvent(record, error);
}

void PlayerCore::ClearAsrOutput()
{

    const QPointer<WhisperThread> whisper = demux_->GetWhisperThread();

    if (whisper)
        whisper->Clear();
}

void PlayerCore::BindAsrSubtitleHandler(QObject* context, AsrSubtitleHandler handler)
{

    if (!context || !handler)
        return;

    const QPointer<WhisperThread> whisper = demux_->GetWhisperThread();
    if (!whisper)
        return;

    QObject::disconnect(whisper, &WhisperThread::SubtitleReady, context, nullptr);

    QObject::connect(whisper, &WhisperThread::SubtitleReady,
        context,

        [handler = std::move(handler)](
            const QString& text, long long startMs, long long endMs, quint64 generation, quint64 serial)
        {

            handler(text, startMs, endMs, generation, serial);
        },

        Qt::QueuedConnection);
}

void PlayerCore::BindDetectorResultHandler(QObject* context, DetectorResultHandler handler)
{
    if (!context || !handler)
        return;

    const QPointer<DetectorThread> detector = demux_->GetDetectorThread();
    if (!detector)
        return;

    QObject::disconnect(detector, &DetectorThread::DetectionsReady, context, nullptr);

    QObject::connect(detector, &DetectorThread::DetectionsReady,
        context,
        [handler = std::move(handler)](DetectionResult result)
        {

            handler(std::move(result));
        },
        Qt::QueuedConnection);
}

void PlayerCore::BindDetectorModelReadyHandler(
    QObject* context,
    DetectorModelReadyHandler handler)
{
    if (!context || !handler)
        return;

    const QPointer<DetectorThread> detector = demux_->GetDetectorThread();
    if (!detector)
        return;

    QObject::disconnect(detector, &DetectorThread::ModelReady, context, nullptr);

    QObject::connect(detector, &DetectorThread::ModelReady,
        context,
        [handler = std::move(handler)](bool success)
        {
            handler(success);
        },
        Qt::QueuedConnection);
}
