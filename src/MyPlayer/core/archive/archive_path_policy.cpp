

#include "archive_path_policy.h"

#include <QByteArray>
#include <QDir>
#include <QRegularExpression>
#include <QTimeZone>

QString ArchivePathPolicy::DatabaseFilePath() const
{
    if (archiveRootDir.trimmed().isEmpty())
        return {};
    return QDir(archiveRootDir).filePath(databaseFileName);
}

QString ArchivePathPolicy::NormalizedRecordingContainer() const
{
    return NormalizeRecordingContainer(recordingContainer);
}

QString ArchivePathPolicy::RecordingFileExtension() const
{
    return IsFragmentedMp4Container(recordingContainer) ? QStringLiteral("mp4") : QStringLiteral("mkv");
}

QString ArchivePathPolicy::CameraDayDirectory(const QString& cameraId, const QDate& day) const
{
    if (archiveRootDir.trimmed().isEmpty() || !day.isValid())
        return {};

    const QString safeCameraId = SanitizePathComponent(cameraId);
    return QDir(archiveRootDir).filePath(
        QString("%1/%2").arg(safeCameraId, day.toString(Qt::ISODate)));
}

QString ArchivePathPolicy::EnsureCameraDayDirectory(const QString& cameraId, const QDate& day) const
{
    const QString directory = CameraDayDirectory(cameraId, day);
    if (directory.isEmpty())
        return {};

    QDir().mkpath(directory);
    return directory;
}

QString ArchivePathPolicy::BuildSegmentRelativePath(
    const QString& cameraId, const QDateTime& startUtc, const QDateTime& endUtc) const
{
    if (!startUtc.isValid() || !endUtc.isValid())
        return {};

    const QString safeCameraId = SanitizePathComponent(cameraId);
    const QString dayPart = startUtc.date().toString(Qt::ISODate);
    const QString startPart = startUtc.toUTC().toString("yyyyMMdd_HHmmss");
    const QString endPart = endUtc.toUTC().toString("yyyyMMdd_HHmmss");
    return QString("%1/%2/%1__%3__%4.%5")
        .arg(safeCameraId, dayPart, startPart, endPart, RecordingFileExtension());
}

QString ArchivePathPolicy::BuildSegmentAbsolutePath(
    const QString& cameraId, const QDateTime& startUtc, const QDateTime& endUtc) const
{
    if (archiveRootDir.trimmed().isEmpty())
        return {};
    return QDir(archiveRootDir).filePath(BuildSegmentRelativePath(cameraId, startUtc, endUtc));
}

QString ArchivePathPolicy::BuildPlaybackProxyRelativePath(
    const QString& cameraId,
    const QDateTime& startUtc,
    const QDateTime& endUtc,
    const QString& codecProfile) const
{
    if (!startUtc.isValid() || !endUtc.isValid())
        return {};

    const QString safeCameraId = SanitizePathComponent(cameraId);
    const QString safeProfile = SanitizePathComponent(codecProfile.trimmed().isEmpty()
        ? QStringLiteral("h264_aac_mp4")
        : codecProfile.trimmed().toLower());
    const QString dayPart = startUtc.date().toString(Qt::ISODate);
    const QString startPart = startUtc.toUTC().toString("yyyyMMdd_HHmmss");
    const QString endPart = endUtc.toUTC().toString("yyyyMMdd_HHmmss");
    return QString("%1/%2/proxy/%1__%3__%4__%5.mp4")
        .arg(safeCameraId, dayPart, startPart, endPart, safeProfile);
}

QString ArchivePathPolicy::BuildPlaybackProxyAbsolutePath(
    const QString& cameraId,
    const QDateTime& startUtc,
    const QDateTime& endUtc,
    const QString& codecProfile) const
{
    if (archiveRootDir.trimmed().isEmpty())
        return {};
    return QDir(archiveRootDir).filePath(
        BuildPlaybackProxyRelativePath(cameraId, startUtc, endUtc, codecProfile));
}

QString ArchivePathPolicy::NormalizeRecordingContainer(QString value)
{
    value = value.trimmed().toLower();
    if (value.isEmpty())
        return QStringLiteral("mkv");
    if (value.startsWith('.'))
        value.remove(0, 1);
    if (value == QLatin1String("matroska"))
        return QStringLiteral("mkv");
    if (value == QLatin1String("fmp4")
        || value == QLatin1String("fragmented_mp4")
        || value == QLatin1String("fragmented-mp4")
        || value == QLatin1String("mp4"))
    {
        return QStringLiteral("mp4");
    }
    return value == QLatin1String("mkv") ? QStringLiteral("mkv") : QStringLiteral("mkv");
}

QString ArchivePathPolicy::RecordingContainerLabel(const QString& value)
{
    return IsFragmentedMp4Container(value) ? QStringLiteral("fMP4") : QStringLiteral("MKV");
}

bool ArchivePathPolicy::IsFragmentedMp4Container(const QString& value)
{
    return NormalizeRecordingContainer(value) == QLatin1String("mp4");
}

bool ArchivePathPolicy::ParseSegmentPath(
    const QString& path,
    QString* cameraId,
    QDateTime* startUtc,
    QDateTime* endUtc,
    QString* container)
{
    const QString normalized = QDir::fromNativeSeparators(path.trimmed());
    if (normalized.isEmpty())
        return false;

    static const QRegularExpression currentPattern(
        R"((?:^|/)([^/]+)/(\d{4}-\d{2}-\d{2})/[^/]+__(\d{8}_\d{6})__(\d{8}_\d{6})\.([^.]+)$)",
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression legacyPattern(
        R"((?:^|/)([^/]+)/(\d{4}-\d{2}-\d{2})/segment_(\d{8}_\d{6})_(\d{8}_\d{6})\.([^.]+)$)",
        QRegularExpression::CaseInsensitiveOption);

    QRegularExpressionMatch match = currentPattern.match(normalized);
    bool currentNaming = true;
    if (!match.hasMatch())
    {
        match = legacyPattern.match(normalized);
        currentNaming = false;
    }
    if (!match.hasMatch())
        return false;

    const QString startText = match.captured(currentNaming ? 3 : 3);
    const QString endText = match.captured(currentNaming ? 4 : 4);
    QDateTime parsedStart = QDateTime::fromString(startText, "yyyyMMdd_HHmmss");
    QDateTime parsedEnd = QDateTime::fromString(endText, "yyyyMMdd_HHmmss");
    if (!parsedStart.isValid() || !parsedEnd.isValid())
        return false;

    parsedStart.setTimeZone(QTimeZone(QByteArrayLiteral("UTC")));
    parsedEnd.setTimeZone(QTimeZone(QByteArrayLiteral("UTC")));

    if (cameraId)
        *cameraId = match.captured(1);
    if (startUtc)
        *startUtc = parsedStart;
    if (endUtc)
        *endUtc = parsedEnd;
    if (container)
        *container = NormalizeRecordingContainer(match.captured(currentNaming ? 5 : 5));
    return true;
}

QString ArchivePathPolicy::SanitizePathComponent(QString value)
{
    value = value.trimmed();
    if (value.isEmpty())
        return "unknown";

    static const QString forbidden = "\\/:*?\"<>|";
    for (const QChar ch : forbidden)
        value.replace(ch, '_');
    value.replace(' ', '_');
    return value;
}
