#include "demux_thread.h"

#include "live_stream_controller.h"
#include "media_feature_bridge.h"
#include "packet_pump.h"
#include "stream_session_core.h"
#include "stream_stats_reporter.h"
#include "../recording/session_recorder.h"
#include "../../common/diagnostics/logger.h"

DemuxThread::DemuxThread()
{

    statsReporter = std::make_unique<StreamStatsReporter>(state, resources);

    sessionCore = std::make_unique<StreamSessionCore>(state, resources);

    liveController = std::make_unique<LiveStreamController>(
        state, resources, *sessionCore,
        [this](const std::string& text)
        {

            if (statsReporter)
                statsReporter->PublishStatusEventLocked(text);
        });

    sessionCore->SetLiveTuningCallbacks(
        [this]()
        {
            if (liveController)
                liveController->ResetRuntimeTuningLocked();
        },
        [this](bool force)
        {
            if (liveController)
                liveController->UpdateRuntimeTuningLocked(force);
        });

    featureBridge = std::make_unique<MediaFeatureBridge>(
        state, resources,
        [this](bool force)
        {
            if (liveController)
                liveController->UpdateRuntimeTuningLocked(force);
        });

    recorder = std::make_unique<SessionRecorder>();
    resources.recorder = recorder.get();

    sessionCore->SetAiEpochRebindCallback(
        [this](const StreamEpoch& epoch)
        {
            if (featureBridge)
                featureBridge->RebindEpochLocked(epoch);
        });

    packetPump = std::make_unique<PacketPump>(state, resources, *liveController, *sessionCore);
}

DemuxThread::~DemuxThread()
{

    Close();
}

bool DemuxThread::Open(const char* url, const std::shared_ptr<VideoCallback>& call)
{

    return sessionCore ? sessionCore->Open(url, call) : false;
}

bool DemuxThread::OpenPrepared(Demux* preparedDemux, const char* url,
    const StreamOpenOptions& options, const std::shared_ptr<VideoCallback>& call, int measuredOpenLatencyMs)
{

    return sessionCore
        ? sessionCore->OpenPrepared(preparedDemux, url, options, call, measuredOpenLatencyMs)
        : false;
}

void DemuxThread::Start()
{

    if (sessionCore)
        sessionCore->EnsurePipelineObjects();

    state.isExit.store(false);
    state.isComplete.store(false);
    state.hasError.store(false);

    if (!isRunning())
        QThread::start();

    if (sessionCore)
        sessionCore->StartChildThreads();
}

void DemuxThread::Close()
{

    state.isExit.store(true);

    wait();

    if (sessionCore)
        sessionCore->ShutdownResources();
}

void DemuxThread::DisableAllAiFeatures()
{

    if (featureBridge)
        featureBridge->DisableAll();
}

void DemuxThread::Clear()
{

    if (sessionCore)
        sessionCore->Clear();
}

void DemuxThread::AcknowledgeError()
{

    if (sessionCore)
        sessionCore->AcknowledgeError();
}

void DemuxThread::Seek(double pos)
{

    if (sessionCore)
        sessionCore->Seek(pos);
}

void DemuxThread::SetPause(bool paused)
{
    if (sessionCore)
        sessionCore->SetPause(paused);
}

void DemuxThread::SetVolume(double vol)
{
    if (sessionCore)
        sessionCore->SetVolume(vol);
}

void DemuxThread::SetSpeed(double speed)
{
    if (sessionCore)
        sessionCore->SetSpeed(speed);
}

PlaybackSessionSnapshot DemuxThread::GetSessionSnapshot()
{
    PlaybackSessionSnapshot snapshot;

    snapshot.isPaused = state.isPause.load();
    snapshot.isComplete = state.isComplete.load();
    snapshot.isLiveStream = state.isLiveStream.load();
    snapshot.isBuffering = state.isBuffering.load();
    snapshot.hasError = state.hasError.load();
    snapshot.positionMs = state.pts.load();
    snapshot.totalMs = state.totalMs.load();
    snapshot.state = state.streamState.load();
    snapshot.sourceType = state.sourceType.load();

    snapshot.epoch = sessionCore ? sessionCore->GetEpoch() : StreamEpoch{};

    snapshot.statusEventGeneration = statsReporter ? statsReporter->GetStatusEventGeneration() : 0;
    snapshot.statusEventText = statsReporter ? statsReporter->GetStatusEventText() : std::string();
    snapshot.currentUrl = sessionCore ? sessionCore->GetCurrentUrl() : std::string();
    snapshot.lastError = sessionCore ? sessionCore->GetLastError() : std::string();
    return snapshot;
}

PlaybackMediaSnapshot DemuxThread::GetMediaSnapshot()
{
    PlaybackMediaSnapshot snapshot;

    snapshot.videoWidth = state.videoWidth.load();
    snapshot.videoHeight = state.videoHeight.load();
    snapshot.videoFpsNum = state.videoFpsNum.load();
    snapshot.videoFpsDen = state.videoFpsDen.load();
    snapshot.bitrate = state.bitrate.load();
    snapshot.audioSampleRate = state.audioSampleRate.load();
    snapshot.audioChannels = state.audioChannels.load();
    return snapshot;
}

void DemuxThread::run()
{

    Logger::Instance().Log(
        LogLevel::Info,
        "session",
        "thread.run",
        "Demux thread started");

    if (packetPump)
        packetPump->Run();
}

int DemuxThread::FetchRenderedFrames()
{

    return statsReporter ? statsReporter->FetchRenderedFrames() : 0;
}

std::string DemuxThread::GetOsdDetail()
{

    return statsReporter ? statsReporter->GetOsdDetail() : std::string();
}

void DemuxThread::SetNetworkOpenOptions(const StreamOpenOptions& options)
{

    if (sessionCore)
        sessionCore->SetNetworkOpenOptions(options);
}

StreamOpenOptions DemuxThread::GetNetworkOpenOptions()
{

    return sessionCore ? sessionCore->GetNetworkOpenOptions() : StreamOpenOptions::DefaultNetwork();
}

StreamOpenOptions DemuxThread::GetActiveOpenOptions()
{

    return sessionCore ? sessionCore->GetActiveOpenOptions() : StreamOpenOptions::DefaultFile();
}

StreamStatsSnapshot DemuxThread::GetStreamStats()
{
    return statsReporter ? statsReporter->GetStreamStats() : StreamStatsSnapshot{};
}

int DemuxThread::GetAudioStreamCount()
{
    std::lock_guard<std::mutex> lock(resources.mux);
    return resources.demux ? resources.demux->GetAudioStreamCount() : 0;
}

AudioStreamInfo DemuxThread::GetAudioStreamInfo(int idx)
{
    std::lock_guard<std::mutex> lock(resources.mux);
    return resources.demux ? resources.demux->GetAudioStreamInfo(idx) : AudioStreamInfo{};
}

int DemuxThread::GetCurrentAudioIndex()
{
    std::lock_guard<std::mutex> lock(resources.mux);
    return resources.demux ? resources.demux->GetCurrentAudioIndex() : -1;
}

bool DemuxThread::SwitchAudioStream(int idx)
{
    return sessionCore ? sessionCore->SwitchAudioStream(idx) : false;
}

int DemuxThread::GetSubtitleStreamCount()
{
    std::lock_guard<std::mutex> lock(resources.mux);
    return resources.demux ? resources.demux->GetSubtitleStreamCount() : 0;
}

SubtitleStreamInfo DemuxThread::GetSubtitleStreamInfo(int idx)
{
    std::lock_guard<std::mutex> lock(resources.mux);
    return resources.demux ? resources.demux->GetSubtitleStreamInfo(idx) : SubtitleStreamInfo{};
}

bool DemuxThread::LoadSubtitleTrack(int idx, std::vector<SubtitleCueData>& cues)
{
    Demux* localDemux = nullptr;
    {
        std::lock_guard<std::mutex> lock(resources.mux);
        localDemux = resources.demux;
    }
    return localDemux ? localDemux->LoadSubtitleTrack(idx, cues) : false;
}

bool DemuxThread::SetAiCapabilityEnabled(const AiFeatureConfig& config, bool enable, std::string* error)
{

    return featureBridge ? featureBridge->SetCapabilityEnabled(config, enable, error) : false;
}

bool DemuxThread::SetAiCapabilityEnabled(AiCapability capability, bool enable, std::string* error)
{
    return featureBridge ? featureBridge->SetCapabilityEnabled(capability, enable, error) : false;
}

bool DemuxThread::IsAiCapabilityEnabled(AiCapability capability) const
{
    return featureBridge ? featureBridge->IsCapabilityEnabled(capability) : false;
}

StreamEpoch DemuxThread::GetAiCapabilityEpoch(AiCapability capability) const
{
    return featureBridge ? featureBridge->GetCapabilityEpoch(capability) : StreamEpoch{};
}

void DemuxThread::UpdateAiRuntimeHints(
    AiPriorityTier priorityTier,
    bool focusRoute,
    bool alarmRoute,
    bool fullscreenRoute,
    int detectorMinimumSkipFrames)
{
    if (featureBridge)
    {

        featureBridge->UpdateRuntimeHints(
            priorityTier,
            focusRoute,
            alarmRoute,
            fullscreenRoute,
            detectorMinimumSkipFrames);
    }
}

void DemuxThread::EnableWhisper(bool enable, const std::string& modelPath)
{

    AiFeatureConfig config;
    config.capability = AiCapability::Asr;
    config.modelPath = modelPath;
    config.preferGpu = true;
    config.allowCpuFallback = true;
    if (featureBridge)
        featureBridge->SetCapabilityEnabled(config, enable, nullptr);
}

bool DemuxThread::IsWhisperEnabled() const
{
    return IsAiCapabilityEnabled(AiCapability::Asr);
}

AiModelRecord DemuxThread::GetAiModelRecord(AiCapability capability) const
{
    return featureBridge ? featureBridge->GetModelRecord(capability) : AiModelRecord{};
}

QPointer<WhisperThread> DemuxThread::GetWhisperThread() const
{

    return featureBridge ? featureBridge->GetWhisperThread() : QPointer<WhisperThread>();
}

StreamEpoch DemuxThread::GetWhisperEpoch()
{
    return GetAiCapabilityEpoch(AiCapability::Asr);
}

void DemuxThread::EnableDetector(bool enable, const std::string& modelPath, const std::string& labelsPath)
{

    AiFeatureConfig config;
    config.capability = AiCapability::Detector;
    config.modelPath = modelPath;
    config.auxModelPath = labelsPath;
    config.preferGpu = true;
    config.allowCpuFallback = true;
    if (featureBridge)
        featureBridge->SetCapabilityEnabled(config, enable, nullptr);
}

bool DemuxThread::IsDetectorEnabled() const
{
    return IsAiCapabilityEnabled(AiCapability::Detector);
}

QPointer<DetectorThread> DemuxThread::GetDetectorThread() const
{
    return featureBridge ? featureBridge->GetDetectorThread() : QPointer<DetectorThread>();
}

bool DemuxThread::StartRecording(const RecordingConfiguration& configuration, std::string* error)
{
    if (!sessionCore)
        return false;

    std::lock_guard<std::mutex> lock(resources.mux);
    return sessionCore->StartRecordingLocked(configuration, error);
}

void DemuxThread::StopRecording()
{
    if (!sessionCore)
        return;

    std::lock_guard<std::mutex> lock(resources.mux);
    sessionCore->StopRecordingLocked();
}

bool DemuxThread::IsRecording() const
{
    if (!sessionCore)
        return false;

    std::lock_guard<std::mutex> lock(resources.mux);
    return sessionCore->IsRecordingLocked();
}

RecordingRuntimeSnapshot DemuxThread::GetRecordingSnapshot() const
{

    std::lock_guard<std::mutex> lock(resources.mux);
    return recorder ? recorder->GetRuntimeSnapshot() : RecordingRuntimeSnapshot{};
}

bool DemuxThread::RecordArchiveEvent(ArchiveEventRecord* record, std::string* error)
{
    if (!sessionCore)
        return false;

    std::lock_guard<std::mutex> lock(resources.mux);
    return sessionCore->RecordArchiveEventLocked(record, error);
}
