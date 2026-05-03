

#include "session_recorder.h"

#include "segment_writer.h"
#include "../../common/diagnostics/logger.h"
#include "../media/demux.h"

SessionRecorder::SessionRecorder() = default;
SessionRecorder::~SessionRecorder() = default;

bool SessionRecorder::StartRecording(const RecordingConfiguration& configuration, std::string* errorMessage)
{
    std::lock_guard<std::mutex> lock(mux_);
    if (!configuration.IsValid())
    {
        SetRuntimeErrorLocked(QStringLiteral("Recording configuration is incomplete."));
        if (errorMessage)
            *errorMessage = "Recording configuration is incomplete.";
        return false;
    }

    ArchivePathPolicy policy;
    policy.archiveRootDir = configuration.archiveRootDir;
    policy.recordingContainer = ArchivePathPolicy::NormalizeRecordingContainer(configuration.container);
    policy.segmentDurationSeconds = configuration.segmentDurationSeconds;

    QString qtError;
    if (!service_.Initialize(policy, &qtError))
    {
        SetRuntimeErrorLocked(qtError);
        if (errorMessage)
            *errorMessage = qtError.toStdString();
        return false;
    }

    configuration_ = configuration;
    runtimeSnapshot_.configured = true;
    runtimeSnapshot_.active = false;
    runtimeSnapshot_.state = QStringLiteral("armed");
    runtimeSnapshot_.lastError.clear();
    ClearCurrentSegmentLocked();
    if (currentDemux_)
        return StartSegmentLocked(*currentDemux_, currentSourceUrl_, QDateTime::currentDateTimeUtc(), errorMessage);
    return true;
}

void SessionRecorder::StopRecording()
{
    std::lock_guard<std::mutex> lock(mux_);
    FinalizeSegmentLocked(QDateTime::currentDateTimeUtc());
    configuration_ = RecordingConfiguration{};
    service_.Shutdown();
    runtimeSnapshot_.configured = false;
    runtimeSnapshot_.active = false;
    ClearCurrentSegmentLocked();
    if (runtimeSnapshot_.lastError.trimmed().isEmpty())
        runtimeSnapshot_.state = QStringLiteral("idle");
}

bool SessionRecorder::IsRecording() const
{
    std::lock_guard<std::mutex> lock(mux_);
    return writer_ && writer_->IsOpen();
}

RecordingRuntimeSnapshot SessionRecorder::GetRuntimeSnapshot() const
{
    std::lock_guard<std::mutex> lock(mux_);
    return runtimeSnapshot_;
}

bool SessionRecorder::OnSessionOpened(Demux& demux, const std::string& sourceUrl, std::string* errorMessage)
{
    std::lock_guard<std::mutex> lock(mux_);
    currentDemux_ = &demux;
    currentSourceUrl_ = sourceUrl;
    if (!configuration_.IsValid())
        return true;

    return StartSegmentLocked(demux, sourceUrl, QDateTime::currentDateTimeUtc(), errorMessage);
}

void SessionRecorder::OnSessionClosed()
{
    std::lock_guard<std::mutex> lock(mux_);
    FinalizeSegmentLocked(QDateTime::currentDateTimeUtc());
    currentDemux_ = nullptr;
    currentSourceUrl_.clear();
    runtimeSnapshot_.active = false;
    ClearCurrentSegmentLocked();
    if (configuration_.IsValid() && runtimeSnapshot_.lastError.trimmed().isEmpty())
        runtimeSnapshot_.state = QStringLiteral("armed");
}

void SessionRecorder::OnPacket(const AVPacket* packet, bool isAudio)
{
    std::lock_guard<std::mutex> lock(mux_);
    if (!configuration_.IsValid() || !packet)
        return;

    const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
    if ((!writer_ || !writer_->IsOpen()) && currentDemux_)
    {
        std::string error;
        if (!StartSegmentLocked(*currentDemux_, currentSourceUrl_, nowUtc, &error))
        {
            SetRuntimeErrorLocked(QString::fromStdString(error.empty()
                ? "Failed to start recording segment."
                : error));
            Logger::Instance().Log(
                LogLevel::Error,
                "recording",
                "segment.start_fail",
                error.empty() ? "Failed to start recording segment" : error,
                { { "camera_id", configuration_.cameraId.toStdString() } });
            return;
        }
    }

    if (activeSegment_ && nowUtc >= activeSegment_->plannedEndUtc)
    {
        FinalizeSegmentLocked(nowUtc);
        if (currentDemux_)
        {
            std::string error;
            if (!StartSegmentLocked(*currentDemux_, currentSourceUrl_, nowUtc, &error))
            {
                SetRuntimeErrorLocked(QString::fromStdString(error.empty()
                    ? "Failed to rotate recording segment."
                    : error));
                Logger::Instance().Log(
                    LogLevel::Error,
                    "recording",
                    "segment.rotate_fail",
                    error.empty() ? "Failed to rotate recording segment" : error,
                    { { "camera_id", configuration_.cameraId.toStdString() } });
                return;
            }
        }
    }

    if (!writer_ || !writer_->IsOpen())
        return;

    std::string error;
    if (!writer_->WritePacket(packet, isAudio, &error))
    {
        SetRuntimeErrorLocked(QString::fromStdString(error.empty()
            ? "Failed to write recording packet."
            : error));
        Logger::Instance().Log(
            LogLevel::Error,
            "recording",
            "packet.write_fail",
            error.empty() ? "Failed to write recording packet" : error,
            {
                { "camera_id", configuration_.cameraId.toStdString() },
                { "audio", isAudio ? "true" : "false" },
            });
    }
}

bool SessionRecorder::RecordEvent(ArchiveEventRecord* record, std::string* errorMessage)
{
    if (!record)
    {
        if (errorMessage)
            *errorMessage = "Archive event record is null.";
        return false;
    }

    std::lock_guard<std::mutex> lock(mux_);
    if (!configuration_.IsValid() || !service_.IsReady())
    {
        if (errorMessage)
            *errorMessage = "Recording service is not active.";
        return false;
    }

    ArchiveEventRecord stored = *record;
    if (stored.cameraId.trimmed().isEmpty())
        stored.cameraId = configuration_.cameraId;
    if (!stored.occurredAtUtc.isValid())
        stored.occurredAtUtc = QDateTime::currentDateTimeUtc();
    if (stored.segmentId.trimmed().isEmpty() && activeSegment_)
        stored.segmentId = activeSegment_->segmentId;

    QString qtError;
    const bool ok = service_.RecordEvent(stored, &qtError);
    if (ok)
        *record = stored;
    if (!ok && errorMessage)
        *errorMessage = qtError.toStdString();
    return ok;
}

bool SessionRecorder::StartSegmentLocked(
    Demux& demux, const std::string& sourceUrl, const QDateTime& nowUtc, std::string* errorMessage)
{
    if (!configuration_.IsValid())
        return false;

    if (writer_ && writer_->IsOpen())
        return true;

    RecordingSessionDescriptor descriptor;
    descriptor.cameraId = configuration_.cameraId;
    descriptor.displayName = configuration_.displayName;
    descriptor.sourceUrl = QString::fromStdString(sourceUrl);
    descriptor.container = ArchivePathPolicy::NormalizeRecordingContainer(configuration_.container);

    auto segment = service_.BeginSegment(descriptor, nowUtc);
    if (!segment.has_value())
    {
        if (errorMessage)
            *errorMessage = "Failed to allocate archive segment metadata.";
        return false;
    }

    auto writer = std::make_unique<SegmentWriter>();
    if (!writer->Open(*segment, demux, errorMessage))
    {
        SetRuntimeErrorLocked(QString::fromStdString(
            errorMessage && !errorMessage->empty()
                ? *errorMessage
                : std::string("Failed to open recording segment.")));
        return false;
    }

    segment->videoCodec = writer->VideoCodecName();
    segment->audioCodec = writer->AudioCodecName();
    writer_ = std::move(writer);
    activeSegment_ = std::move(segment);
    runtimeSnapshot_.configured = true;
    runtimeSnapshot_.active = true;
    runtimeSnapshot_.state = QStringLiteral("recording");
    runtimeSnapshot_.lastError.clear();
    runtimeSnapshot_.currentSegmentRelativePath = activeSegment_->relativePath;
    runtimeSnapshot_.currentSegmentStartUtc = activeSegment_->startUtc;
    runtimeSnapshot_.currentSegmentPlannedEndUtc = activeSegment_->plannedEndUtc;
    Logger::Instance().Log(
        LogLevel::Info,
        "recording",
        "segment.start",
        "Recording segment started",
        {
            { "camera_id", configuration_.cameraId.toStdString() },
            { "path", activeSegment_->absolutePath.toStdString() },
        });
    return true;
}

void SessionRecorder::FinalizeSegmentLocked(const QDateTime& nowUtc)
{
    if (!writer_ || !activeSegment_)
        return;

    const QString completedRelativePath = activeSegment_->relativePath;
    const QDateTime completedEndUtc = nowUtc.toUTC();
    const qint64 fileSizeBytes = writer_->Close();
    QString errorMessage;
    if (!service_.FinalizeSegment(*activeSegment_, fileSizeBytes, nowUtc, &errorMessage))
    {
        SetRuntimeErrorLocked(errorMessage);
        Logger::Instance().Log(
            LogLevel::Error,
            "recording",
            "segment.finalize_fail",
            errorMessage.toStdString(),
            {
                { "camera_id", activeSegment_->cameraId.toStdString() },
                { "path", activeSegment_->absolutePath.toStdString() },
            });
    }
    else
    {
        runtimeSnapshot_.lastCompletedSegmentRelativePath = completedRelativePath;
        runtimeSnapshot_.lastCompletedSegmentEndUtc = completedEndUtc;
        runtimeSnapshot_.lastCompletedFileSizeBytes = std::max<qint64>(0, fileSizeBytes);
        runtimeSnapshot_.lastError.clear();
        runtimeSnapshot_.active = false;
        runtimeSnapshot_.state = configuration_.IsValid()
            ? QStringLiteral("armed")
            : QStringLiteral("idle");
        Logger::Instance().Log(
            LogLevel::Info,
            "recording",
            "segment.finalize",
            "Recording segment finalized",
            {
                { "camera_id", activeSegment_->cameraId.toStdString() },
                { "path", activeSegment_->absolutePath.toStdString() },
                { "file_size_bytes", std::to_string(fileSizeBytes) },
            });
    }

    ClearCurrentSegmentLocked();
    writer_.reset();
    activeSegment_.reset();
}

void SessionRecorder::SetRuntimeErrorLocked(const QString& errorMessage)
{
    runtimeSnapshot_.active = false;
    runtimeSnapshot_.state = QStringLiteral("error");
    runtimeSnapshot_.lastError = errorMessage.trimmed().isEmpty()
        ? QStringLiteral("Recording failed.")
        : errorMessage.trimmed();
    ClearCurrentSegmentLocked();
}

void SessionRecorder::ClearCurrentSegmentLocked()
{
    runtimeSnapshot_.currentSegmentRelativePath.clear();
    runtimeSnapshot_.currentSegmentStartUtc = {};
    runtimeSnapshot_.currentSegmentPlannedEndUtc = {};
}
