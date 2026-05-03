

#include "recording_service.h"

#include <QCryptographicHash>
#include <QFileInfo>
#include <QDir>
#include <QDirIterator>
#include <QUuid>

#include <algorithm>

namespace
{

QString BuildRecoveredSegmentId(const QString& relativePath)
{
    const QByteArray hash = QCryptographicHash::hash(
        relativePath.toUtf8(), QCryptographicHash::Sha1).toHex();
    return QString("recovered:%1").arg(QString::fromLatin1(hash));
}

}

RecordingService::RecordingService()
    : queryService_(indexStore_)
{
}

bool RecordingService::Initialize(const ArchivePathPolicy& policy, QString* errorMessage)
{
    policy_ = policy;
    lastRecoverySummary_ = {};
    ready_ = indexStore_.Open(policy_, errorMessage);
    if (ready_)
        RecoverSegments();
    return ready_;
}

void RecordingService::Shutdown()
{
    indexStore_.Close();
    ready_ = false;
}

bool RecordingService::IsReady() const
{
    return ready_;
}

const ArchivePathPolicy& RecordingService::Policy() const
{
    return policy_;
}

ArchiveQueryService& RecordingService::QueryService()
{
    return queryService_;
}

const ArchiveQueryService& RecordingService::QueryService() const
{
    return queryService_;
}

const RecordingRecoverySummary& RecordingService::LastRecoverySummary() const
{
    return lastRecoverySummary_;
}

std::optional<RecordingActiveSegment> RecordingService::BeginSegment(
    const RecordingSessionDescriptor& session, QDateTime startUtc) const
{
    if (!ready_ || session.cameraId.trimmed().isEmpty() || !startUtc.isValid())
        return std::nullopt;

    const QDateTime startUtcValue = startUtc.toUTC();
    const QDateTime plannedEndUtc = startUtcValue.addSecs(std::max(1, policy_.segmentDurationSeconds));
    const QString dayDirectory = policy_.EnsureCameraDayDirectory(session.cameraId, startUtcValue.date());
    if (dayDirectory.isEmpty())
        return std::nullopt;

    RecordingActiveSegment segment;
    segment.segmentId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    segment.cameraId = session.cameraId;
    segment.sourceUrl = session.sourceUrl;
    segment.container = ArchivePathPolicy::NormalizeRecordingContainer(session.container);
    segment.videoCodec = session.videoCodec;
    segment.audioCodec = session.audioCodec;
    segment.startUtc = startUtcValue;
    segment.plannedEndUtc = plannedEndUtc;
    segment.hasVideo = session.hasVideo;
    segment.hasAudio = session.hasAudio;
    segment.relativePath = policy_.BuildSegmentRelativePath(segment.cameraId, segment.startUtc, segment.plannedEndUtc);
    segment.absolutePath = QDir(policy_.archiveRootDir).filePath(segment.relativePath);
    QDir().mkpath(QFileInfo(segment.absolutePath).absolutePath());
    return segment;
}

bool RecordingService::FinalizeSegment(
    const RecordingActiveSegment& segment, qint64 fileSizeBytes, QDateTime endUtc, QString* errorMessage)
{
    if (!ready_)
    {
        if (errorMessage)
            *errorMessage = "Recording service is not initialized.";
        return false;
    }
    if (segment.segmentId.trimmed().isEmpty() || segment.cameraId.trimmed().isEmpty()
        || !segment.startUtc.isValid())
    {
        if (errorMessage)
            *errorMessage = "Recording segment metadata is incomplete.";
        return false;
    }

    ArchiveSegmentRecord record;
    record.segmentId = segment.segmentId;
    record.cameraId = segment.cameraId;
    record.sourceUrl = segment.sourceUrl;
    record.startUtc = segment.startUtc;
    record.endUtc = endUtc.isValid() ? endUtc.toUTC() : segment.plannedEndUtc;
    record.relativePath = segment.relativePath;
    record.container = segment.container;
    record.videoCodec = segment.videoCodec;
    record.audioCodec = segment.audioCodec;
    record.fileSizeBytes = std::max<qint64>(0, fileSizeBytes);
    record.hasVideo = segment.hasVideo;
    record.hasAudio = segment.hasAudio;
    return indexStore_.UpsertSegment(record, errorMessage);
}

bool RecordingService::FinalizePlaybackProxy(const ArchivePlaybackProxyRecord& record, QString* errorMessage)
{
    if (!ready_)
    {
        if (errorMessage)
            *errorMessage = "Recording service is not initialized.";
        return false;
    }
    return indexStore_.UpsertPlaybackProxy(record, errorMessage);
}

bool RecordingService::RecordEvent(const ArchiveEventRecord& record, QString* errorMessage)
{
    if (!ready_)
    {
        if (errorMessage)
            *errorMessage = "Recording service is not initialized.";
        return false;
    }
    return indexStore_.RecordEvent(record, errorMessage);
}

void RecordingService::RecoverSegments()
{
    lastRecoverySummary_ = {};
    if (!ready_ || policy_.archiveRootDir.trimmed().isEmpty())
        return;

    const int recentThresholdSeconds = std::max(10, policy_.segmentDurationSeconds / 2);
    const QDateTime recentCutoffUtc = QDateTime::currentDateTimeUtc().addSecs(-recentThresholdSeconds);
    QDirIterator it(
        policy_.archiveRootDir,
        QStringList() << QStringLiteral("*.mkv") << QStringLiteral("*.mp4"),
        QDir::Files,
        QDirIterator::Subdirectories);

    while (it.hasNext())
    {
        const QString absolutePath = it.next();
        const QFileInfo fileInfo(absolutePath);
        const QString relativePath = QDir(policy_.archiveRootDir).relativeFilePath(absolutePath);
        if (relativePath.trimmed().isEmpty())
            continue;

        const QDateTime lastModifiedUtc = fileInfo.lastModified().toUTC();
        if (!lastModifiedUtc.isValid() || lastModifiedUtc >= recentCutoffUtc)
        {
            ++lastRecoverySummary_.skippedRecentSegments;
            continue;
        }

        if (!TryImportRecoveredSegment(relativePath, fileInfo))
            ++lastRecoverySummary_.failedImports;
    }
}

bool RecordingService::TryImportRecoveredSegment(const QString& relativePath, const QFileInfo& fileInfo)
{
    if (indexStore_.ContainsSegmentPath(relativePath))
    {
        ++lastRecoverySummary_.skippedExistingSegments;
        return true;
    }

    QString cameraId;
    QString container;
    QDateTime startUtc;
    QDateTime endUtc;
    if (!ArchivePathPolicy::ParseSegmentPath(relativePath, &cameraId, &startUtc, &endUtc, &container))
        return true;

    ArchiveSegmentRecord record;
    record.segmentId = BuildRecoveredSegmentId(relativePath);
    record.cameraId = cameraId;
    record.startUtc = startUtc;
    record.endUtc = endUtc;
    record.relativePath = relativePath;
    record.container = container;
    record.fileSizeBytes = std::max<qint64>(0, fileInfo.size());

    QString error;
    if (!indexStore_.UpsertSegment(record, &error))
        return false;

    ++lastRecoverySummary_.importedSegments;
    return true;
}
