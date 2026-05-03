#pragma once

#include "../ai/ai_types.h"
#include "../session/stream_config.h"

#include <QDateTime>
#include <QString>
#include <QVector>

struct MonitorSourceDescriptor
{
    QString cameraId;
    QString displayName;
    QString groupName;
    QString sourceUrl;
    StreamOpenOptions openOptions = StreamOpenOptions::DefaultNetwork();
    bool preferLowLatency = true;
    bool enableDetector = false;
    bool enableAsr = false;
    bool enableRecording = false;

    bool IsValid() const
    {
        return !cameraId.trimmed().isEmpty() && !sourceUrl.trimmed().isEmpty();
    }
};

enum class MonitorWallLayoutPreset
{
    Single,
    Grid2x2,
    Grid3x3,
    Grid4x4,
    Custom,
};

struct MonitorTilePlacement
{
    QString sessionId;
    int row = 0;
    int column = 0;
    int rowSpan = 1;
    int columnSpan = 1;
    bool selected = false;
    bool audioOwner = false;
};

struct MonitorLayoutSnapshot
{
    MonitorWallLayoutPreset preset = MonitorWallLayoutPreset::Single;
    int rows = 1;
    int columns = 1;
    QVector<MonitorTilePlacement> placements;
};

struct MonitorSessionSnapshot
{
    QString sessionId;
    QString cameraId;
    QString displayName;
    QString groupName;
    QString sourceUrl;

    bool selected = false;
    bool audioOwner = false;
    bool muted = true;
    double volume = 1.0;

    bool detectorEnabled = false;
    bool asrRequested = false;
    bool asrEnabled = false;
    bool asrEligible = false;

    bool recordingEnabled = false;
    bool recordingActive = false;
    QString recordingState;
    QString recordingLastError;
    QString recordingSegmentRelativePath;
    QDateTime recordingSegmentStartUtc;
    QDateTime recordingSegmentPlannedEndUtc;
    QString lastRecordedSegmentRelativePath;
    QDateTime lastRecordedSegmentEndUtc;
    qint64 lastRecordedSegmentFileSizeBytes = 0;

    int eventCount = 0;
    int alarmCount = 0;
    bool alarmActive = false;
    bool alarmAcknowledged = true;

    PlaybackSessionSnapshot playback;
    PlaybackMediaSnapshot media;
    StreamStatsSnapshot stats;
};

struct MonitorWorkspaceSnapshot
{
    MonitorLayoutSnapshot layout;
    QVector<MonitorSessionSnapshot> sessions;
    QString selectedSessionId;
    QString audioSessionId;
};

struct MonitorAiSessionPolicy
{
    AiPriorityTier priorityTier = AiPriorityTier::Background;
    bool focusRoute = false;
    bool alarmRoute = false;
    bool fullscreenRoute = false;
    bool shouldRunAsr = false;
    int detectorMinimumSkipFrames = 0;
};
