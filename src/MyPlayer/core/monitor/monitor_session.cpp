

#include "monitor_session.h"

#include "../archive/archive_path_policy.h"
#include "../media/demux.h"
#include "../player/player_core.h"
#include "../video/video_callback.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

#include <algorithm>

namespace
{

bool IsNetworkSource(const QString& sourceUrl)
{
    return sourceUrl.contains("://");
}
}

MonitorSession::MonitorSession(const QString& sessionId, const MonitorSourceDescriptor& source)
    : sessionId_(sessionId)
    , source_(source)
    , core_(std::make_unique<PlayerCore>())
    , detectorEnabled_(source.enableDetector)
    , asrRequested_(source.enableAsr)
    , recordingEnabled_(source.enableRecording)
{
    core_->Start();
    started_ = true;
    aiPolicy_.priorityTier = AiPriorityTier::Background;
    ApplyAudioState();
    ApplyAiRuntimeHints();
}

MonitorSession::~MonitorSession()
{
    Close();
}

QString MonitorSession::SessionId() const
{
    return sessionId_;
}

QString MonitorSession::CameraId() const
{
    return source_.cameraId;
}

const MonitorSourceDescriptor& MonitorSession::Source() const
{
    return source_;
}

void MonitorSession::UpdateSource(const MonitorSourceDescriptor& source)
{
    source_ = source;
    detectorEnabled_ = source.enableDetector;
    asrRequested_ = source.enableAsr;
    recordingEnabled_ = source.enableRecording;
}

bool MonitorSession::Open(const std::shared_ptr<VideoCallback>& callback)
{
    if (!core_ || !source_.IsValid())
        return false;

    if (!started_)
    {
        core_->Start();
        started_ = true;
    }

    if (IsNetworkSource(source_.sourceUrl))
        core_->SetNetworkOpenOptions(source_.openOptions);

    const QByteArray urlBytes = source_.sourceUrl.toUtf8();
    const bool opened = core_->Open(urlBytes.constData(), callback);
    if (opened)
    {
        core_->SetPause(false);
        ApplyAudioState();
        ApplyAiRuntimeHints();
        ApplyDetectorState(detectorEnabled_, nullptr);
        ApplyAsrState(nullptr);
        ApplyRecordingState(recordingEnabled_, recordingConfiguration_, nullptr);
    }
    return opened;
}

bool MonitorSession::OpenPrepared(
    Demux* preparedDemux,
    const StreamOpenOptions& options,
    const std::shared_ptr<VideoCallback>& callback,
    int measuredOpenLatencyMs)
{
    if (!core_ || !preparedDemux || !source_.IsValid())
    {
        delete preparedDemux;
        return false;
    }

    if (!started_)
    {
        core_->Start();
        started_ = true;
    }

    const QByteArray urlBytes = source_.sourceUrl.toUtf8();
    const bool opened = core_->OpenPrepared(
        preparedDemux,
        urlBytes.constData(),
        options,
        callback,
        measuredOpenLatencyMs);
    if (opened)
    {
        core_->SetPause(false);
        ApplyAudioState();
        ApplyAiRuntimeHints();
        ApplyDetectorState(detectorEnabled_, nullptr);
        ApplyAsrState(nullptr);
        ApplyRecordingState(recordingEnabled_, recordingConfiguration_, nullptr);
    }
    return opened;
}

void MonitorSession::Close()
{
    if (!core_ || !started_)
        return;

    core_->Close();
    asrActive_ = false;
    started_ = false;
}

void MonitorSession::SetSelected(bool selected)
{
    selected_ = selected;
}

void MonitorSession::SetAudioOwner(bool audioOwner)
{
    audioOwner_ = audioOwner;
    ApplyAudioState();
}

void MonitorSession::SetMuted(bool muted)
{
    muted_ = muted;
    ApplyAudioState();
}

void MonitorSession::SetVolume(double volume)
{
    volume_ = volume;
    ApplyAudioState();
}

bool MonitorSession::SetDetectorEnabled(bool enabled, std::string* error)
{
    detectorEnabled_ = enabled;
    source_.enableDetector = enabled;
    return ApplyDetectorState(enabled, error);
}

bool MonitorSession::SetAsrEnabled(bool enabled, std::string* error)
{
    asrRequested_ = enabled;
    source_.enableAsr = enabled;
    return ApplyAsrState(error);
}

bool MonitorSession::SetRecordingEnabled(bool enabled, const RecordingConfiguration& configuration, std::string* error)
{
    recordingEnabled_ = enabled;
    source_.enableRecording = enabled;
    recordingConfiguration_ = configuration;
    recordingConfiguration_.archiveRootDir = QDir::cleanPath(recordingConfiguration_.archiveRootDir.trimmed());
    recordingConfiguration_.container = ArchivePathPolicy::NormalizeRecordingContainer(recordingConfiguration_.container);
    recordingConfiguration_.segmentDurationSeconds = std::max(10, recordingConfiguration_.segmentDurationSeconds);
    return ApplyRecordingState(enabled, recordingConfiguration_, error);
}

bool MonitorSession::ApplyAiSessionPolicy(const MonitorAiSessionPolicy& policy, std::string* error)
{
    const bool unchanged =
        aiPolicy_.priorityTier == policy.priorityTier
        && aiPolicy_.focusRoute == policy.focusRoute
        && aiPolicy_.alarmRoute == policy.alarmRoute
        && aiPolicy_.fullscreenRoute == policy.fullscreenRoute
        && aiPolicy_.shouldRunAsr == policy.shouldRunAsr
        && aiPolicy_.detectorMinimumSkipFrames == policy.detectorMinimumSkipFrames;
    if (unchanged)
        return true;

    aiPolicy_ = policy;
    ApplyAiRuntimeHints();
    return ApplyAsrState(error);
}

void MonitorSession::BindDetectorResultHandler(QObject* context, std::function<void(DetectionResult)> handler)
{
    if (!core_ || !handler)
        return;
    core_->BindDetectorResultHandler(
        context,
        [this, handler = std::move(handler)](DetectionResult result) mutable
        {

            if (!detectorEnabled_)
                return;
            handler(std::move(result));
        });
}

void MonitorSession::BindAsrSubtitleHandler(
    QObject* context,
    std::function<void(const QString&, long long, long long, quint64, quint64)> handler)
{
    if (!core_)
        return;
    core_->BindAsrSubtitleHandler(context, std::move(handler));
}

bool MonitorSession::RecordArchiveEvent(ArchiveEventRecord* record, std::string* error)
{
    return core_ && core_->RecordArchiveEvent(record, error);
}

bool MonitorSession::IsSelected() const
{
    return selected_;
}

bool MonitorSession::IsAudioOwner() const
{
    return audioOwner_;
}

bool MonitorSession::IsMuted() const
{
    return muted_;
}

double MonitorSession::Volume() const
{
    return volume_;
}

bool MonitorSession::IsDetectorEnabled() const
{
    return detectorEnabled_;
}

bool MonitorSession::IsAsrEnabled() const
{
    return asrActive_;
}

bool MonitorSession::IsRecordingEnabled() const
{
    return recordingEnabled_;
}

MonitorSessionSnapshot MonitorSession::GetSnapshot() const
{
    MonitorSessionSnapshot snapshot;
    snapshot.sessionId = sessionId_;
    snapshot.cameraId = source_.cameraId;
    snapshot.displayName = source_.displayName;
    snapshot.groupName = source_.groupName;
    snapshot.sourceUrl = source_.sourceUrl;
    snapshot.selected = selected_;
    snapshot.audioOwner = audioOwner_;
    snapshot.muted = muted_;
    snapshot.volume = volume_;
    snapshot.detectorEnabled = detectorEnabled_;
    snapshot.asrRequested = asrRequested_;
    snapshot.asrEnabled = asrActive_;
    snapshot.asrEligible = aiPolicy_.shouldRunAsr;
    snapshot.recordingEnabled = recordingEnabled_;
    if (core_)
    {
        snapshot.playback = core_->GetSessionSnapshot();
        snapshot.media = core_->GetMediaSnapshot();
        snapshot.stats = core_->GetStreamStats();
        const RecordingRuntimeSnapshot recordingSnapshot = core_->GetRecordingSnapshot();
        snapshot.recordingActive = recordingSnapshot.active;
        snapshot.recordingState = recordingSnapshot.state;
        snapshot.recordingLastError = recordingSnapshot.lastError;
        snapshot.recordingSegmentRelativePath = recordingSnapshot.currentSegmentRelativePath;
        snapshot.recordingSegmentStartUtc = recordingSnapshot.currentSegmentStartUtc;
        snapshot.recordingSegmentPlannedEndUtc = recordingSnapshot.currentSegmentPlannedEndUtc;
        snapshot.lastRecordedSegmentRelativePath = recordingSnapshot.lastCompletedSegmentRelativePath;
        snapshot.lastRecordedSegmentEndUtc = recordingSnapshot.lastCompletedSegmentEndUtc;
        snapshot.lastRecordedSegmentFileSizeBytes = recordingSnapshot.lastCompletedFileSizeBytes;
    }
    return snapshot;
}

void MonitorSession::ApplyAudioState()
{
    if (!core_)
        return;

    const bool effectiveMute = muted_ || !audioOwner_;
    core_->SetVolume(effectiveMute ? 0.0 : volume_);
}

bool MonitorSession::ApplyDetectorState(bool enabled, std::string* error)
{
    if (!core_)
        return false;

    if (!enabled)
        return core_->SetFeatureEnabled(AiCapability::Detector, false, error);

    const QString modelPath = ResolveDetectorModelPath();
    if (modelPath.isEmpty())
    {
        if (error)
            *error = "Detector model not found.";
        detectorEnabled_ = false;
        source_.enableDetector = false;
        return false;
    }

    AiFeatureConfig config;
    config.capability = AiCapability::Detector;
    config.modelPath = modelPath.toStdString();
    config.auxModelPath = ResolveDetectorLabelsPath(modelPath).toStdString();
    config.preferGpu = true;
    config.allowCpuFallback = true;
    ApplyAiRuntimeHints();
    return core_->SetFeatureEnabled(config, true, error);
}

bool MonitorSession::ApplyAsrState(std::string* error)
{
    if (!core_)
        return false;

    const bool shouldRunAsr = asrRequested_ && aiPolicy_.shouldRunAsr;
    const bool featureRunning = core_->IsFeatureEnabled(AiCapability::Asr);
    if (!shouldRunAsr)
    {
        if (featureRunning)
            core_->SetFeatureEnabled(AiCapability::Asr, false, error);
        asrActive_ = false;
        return true;
    }

    if (featureRunning)
    {
        asrActive_ = true;
        return true;
    }

    const QString modelPath = ResolveWhisperModelPath();
    if (modelPath.isEmpty())
    {
        if (error)
            *error = "Whisper model not found.";
        asrActive_ = false;
        return false;
    }

    AiFeatureConfig config;
    config.capability = AiCapability::Asr;
    config.modelPath = modelPath.toStdString();
    config.preferGpu = true;
    config.allowCpuFallback = true;
    ApplyAiRuntimeHints();
    const bool enabled = core_->SetFeatureEnabled(config, true, error);
    asrActive_ = enabled && core_->IsFeatureEnabled(AiCapability::Asr);
    return asrActive_;
}

bool MonitorSession::ApplyRecordingState(
    bool enabled,
    const RecordingConfiguration& recordingConfiguration,
    std::string* error)
{
    if (!core_)
        return false;

    if (!enabled)
    {
        core_->StopRecording();
        return true;
    }

    const QString cleanedArchiveRoot = QDir::cleanPath(recordingConfiguration.archiveRootDir.trimmed());
    if (cleanedArchiveRoot.isEmpty())
    {
        if (error)
            *error = "Archive root directory is empty.";
        recordingEnabled_ = false;
        source_.enableRecording = false;
        return false;
    }

    RecordingConfiguration effectiveConfiguration = recordingConfiguration;
    effectiveConfiguration.enabled = true;
    effectiveConfiguration.archiveRootDir = cleanedArchiveRoot;
    effectiveConfiguration.cameraId = source_.cameraId;
    effectiveConfiguration.displayName = source_.displayName;
    effectiveConfiguration.container = ArchivePathPolicy::NormalizeRecordingContainer(effectiveConfiguration.container);
    effectiveConfiguration.segmentDurationSeconds = std::max(10, effectiveConfiguration.segmentDurationSeconds);
    return core_->StartRecording(effectiveConfiguration, error);
}

void MonitorSession::ApplyAiRuntimeHints()
{
    if (!core_)
        return;

    core_->UpdateAiRuntimeHints(
        aiPolicy_.priorityTier,
        aiPolicy_.focusRoute,
        aiPolicy_.alarmRoute,
        aiPolicy_.fullscreenRoute,
        aiPolicy_.detectorMinimumSkipFrames);
}

QString MonitorSession::ResolveDetectorModelPath()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QDir(appDir).filePath("models/rtdetr-l.onnx"),
        QDir(appDir).filePath("../common/models/rtdetr-l.onnx"),
        QDir(QDir::currentPath()).filePath("models/rtdetr-l.onnx"),
        QDir(QDir::currentPath()).filePath("../common/models/rtdetr-l.onnx"),
        QDir(appDir).filePath("../../../../bin/models/rtdetr-l.onnx"),
        QDir(appDir).filePath("../../../../bin/common/models/rtdetr-l.onnx"),
        QDir(appDir).filePath("../../../bin/models/rtdetr-l.onnx"),
        QDir(appDir).filePath("../../../bin/common/models/rtdetr-l.onnx"),
        QDir(appDir).filePath("../../bin/models/rtdetr-l.onnx"),
        QDir(appDir).filePath("../../bin/common/models/rtdetr-l.onnx"),
        QDir(appDir).filePath("../bin/models/rtdetr-l.onnx")
    };

    for (const QString& candidate : candidates)
    {
        QFileInfo info(QDir::cleanPath(candidate));
        if (info.exists() && info.isFile())
            return info.absoluteFilePath();
    }

    return {};
}

QString MonitorSession::ResolveDetectorLabelsPath(const QString& modelPath)
{
    const QFileInfo modelInfo(modelPath);
    const QString baseName = modelInfo.completeBaseName();
    const QString appDir = QCoreApplication::applicationDirPath();

    QStringList candidates;
    if (modelInfo.exists())
    {
        const QDir modelDir = modelInfo.dir();
        candidates
            << modelDir.filePath(baseName + ".labels.txt")
            << modelDir.filePath(baseName + ".txt")
            << modelDir.filePath("labels.txt")
            << modelDir.filePath("classes.txt");
    }

    const QStringList labelRoots = {
        QDir(appDir).filePath("labels"),
        QDir(appDir).filePath("../common/labels"),
        QDir(QDir::currentPath()).filePath("labels"),
        QDir(QDir::currentPath()).filePath("../common/labels"),
        QDir(appDir).filePath("../../../../bin/labels"),
        QDir(appDir).filePath("../../../../bin/common/labels"),
        QDir(appDir).filePath("../../../bin/labels"),
        QDir(appDir).filePath("../../../bin/common/labels"),
        QDir(appDir).filePath("../../bin/labels"),
        QDir(appDir).filePath("../../bin/common/labels"),
        QDir(appDir).filePath("../bin/labels")
    };

    for (const QString& root : labelRoots)
    {
        const QDir dir(QDir::cleanPath(root));
        candidates
            << dir.filePath(baseName + ".labels.txt")
            << dir.filePath(baseName + ".txt")
            << dir.filePath("coco80.txt")
            << dir.filePath("labels.txt")
            << dir.filePath("classes.txt");
    }

    candidates.removeDuplicates();
    for (const QString& candidate : candidates)
    {
        QFileInfo info(QDir::cleanPath(candidate));
        if (info.exists() && info.isFile())
            return info.absoluteFilePath();
    }

    return {};
}

QString MonitorSession::ResolveWhisperModelPath()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QDir(appDir).filePath("models/ggml-small.bin"),
        QDir(appDir).filePath("../common/models/ggml-small.bin"),
        QDir(QDir::currentPath()).filePath("models/ggml-small.bin"),
        QDir(QDir::currentPath()).filePath("../common/models/ggml-small.bin"),
        QDir(appDir).filePath("../../../../bin/models/ggml-small.bin"),
        QDir(appDir).filePath("../../../../bin/common/models/ggml-small.bin"),
        QDir(appDir).filePath("../../../bin/models/ggml-small.bin"),
        QDir(appDir).filePath("../../../bin/common/models/ggml-small.bin"),
        QDir(appDir).filePath("../../bin/models/ggml-small.bin"),
        QDir(appDir).filePath("../../bin/common/models/ggml-small.bin"),
        QDir(appDir).filePath("../bin/models/ggml-small.bin")
    };

    for (const QString& candidate : candidates)
    {
        QFileInfo info(QDir::cleanPath(candidate));
        if (info.exists() && info.isFile())
            return info.absoluteFilePath();
    }

    return {};
}
