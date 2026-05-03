#pragma once

#include <QDate>
#include <QDateTime>
#include <QString>

struct ArchivePathPolicy
{
    QString archiveRootDir;
    QString recordingContainer = "mkv";
    QString databaseFileName = "archive_index.sqlite3";
    int segmentDurationSeconds = 300;

    QString DatabaseFilePath() const;
    QString NormalizedRecordingContainer() const;
    QString RecordingFileExtension() const;
    QString CameraDayDirectory(const QString& cameraId, const QDate& day) const;
    QString EnsureCameraDayDirectory(const QString& cameraId, const QDate& day) const;
    QString BuildSegmentRelativePath(const QString& cameraId, const QDateTime& startUtc,
        const QDateTime& endUtc) const;
    QString BuildSegmentAbsolutePath(const QString& cameraId, const QDateTime& startUtc,
        const QDateTime& endUtc) const;
    QString BuildPlaybackProxyRelativePath(
        const QString& cameraId,
        const QDateTime& startUtc,
        const QDateTime& endUtc,
        const QString& codecProfile = QStringLiteral("h264_aac_mp4")) const;
    QString BuildPlaybackProxyAbsolutePath(
        const QString& cameraId,
        const QDateTime& startUtc,
        const QDateTime& endUtc,
        const QString& codecProfile = QStringLiteral("h264_aac_mp4")) const;

    static QString NormalizeRecordingContainer(QString value);
    static QString RecordingContainerLabel(const QString& value);
    static bool IsFragmentedMp4Container(const QString& value);
    static bool ParseSegmentPath(const QString& path,
        QString* cameraId,
        QDateTime* startUtc,
        QDateTime* endUtc,
        QString* container);
    static QString SanitizePathComponent(QString value);
};
