#pragma once

#include <QDateTime>
#include <QString>

struct RecordingConfiguration
{
    bool enabled = false;
    QString archiveRootDir;
    QString cameraId;
    QString displayName;
    QString container = "mkv";
    int segmentDurationSeconds = 300;

    bool IsValid() const
    {
        return enabled
            && !archiveRootDir.trimmed().isEmpty()
            && !cameraId.trimmed().isEmpty()
            && segmentDurationSeconds > 0;
    }
};

struct RecordingSessionDescriptor
{
    QString cameraId;
    QString sourceUrl;
    QString displayName;
    QString container = "mkv";
    QString videoCodec;
    QString audioCodec;
    bool hasVideo = true;
    bool hasAudio = true;
};

struct RecordingActiveSegment
{
    QString segmentId;
    QString cameraId;
    QString sourceUrl;
    QString absolutePath;
    QString relativePath;
    QString container = "mkv";
    QString videoCodec;
    QString audioCodec;
    QDateTime startUtc;
    QDateTime plannedEndUtc;
    bool hasVideo = true;
    bool hasAudio = true;
};

struct RecordingRuntimeSnapshot
{
    bool configured = false;
    bool active = false;
    QString state;
    QString lastError;
    QString currentSegmentRelativePath;
    QDateTime currentSegmentStartUtc;
    QDateTime currentSegmentPlannedEndUtc;
    QString lastCompletedSegmentRelativePath;
    QDateTime lastCompletedSegmentEndUtc;
    qint64 lastCompletedFileSizeBytes = 0;
};
