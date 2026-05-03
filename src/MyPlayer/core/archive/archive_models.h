
#pragma once

#include <QDate>
#include <QDateTime>
#include <QString>

enum class ArchiveEventSeverity
{
    Info = 0,
    Warning = 1,
    Alarm = 2,
};

struct ArchiveSegmentRecord
{

    QString segmentId;
    QString cameraId;
    QString sourceUrl;

    QDateTime startUtc;
    QDateTime endUtc;

    QString relativePath;
    QString container = "mkv";
    QString videoCodec;
    QString audioCodec;

    qint64 fileSizeBytes = 0;

    bool hasVideo = true;
    bool hasAudio = true;

    QString playbackProxyId;
    QString playbackProxyRelativePath;
    QString playbackProxyCodecProfile;
    QString playbackProxyContainer;
    QString playbackProxyVideoCodec;
    QString playbackProxyAudioCodec;
    qint64 playbackProxyFileSizeBytes = 0;
    bool playbackProxyReady = false;
};

struct ArchivePlaybackProxyRecord
{
    QString proxyId;
    QString sourceSegmentId;
    QString cameraId;
    QDateTime startUtc;
    QDateTime endUtc;
    QString relativePath;

    QString codecProfile = "h264_aac_mp4";
    QString container = "mp4";

    QString videoCodec;
    QString audioCodec;
    qint64 fileSizeBytes = 0;
};

struct ArchiveDaySummary
{
    QString cameraId;
    QDate day;

    bool hasMedia = false;
    bool hasAlarm = false;

    int segmentCount = 0;

    QDateTime firstUtc;
    QDateTime lastUtc;
};

struct ArchiveEventRecord
{
    QString eventId;
    QString cameraId;
    QDateTime occurredAtUtc;

    QString type;

    ArchiveEventSeverity severity = ArchiveEventSeverity::Info;

    QString segmentId;

    QString payloadJson;
};
