#pragma once

#include "../archive/archive_models.h"

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QString>

#include <mutex>

struct MonitorEventCandidate
{
    QString sessionId;
    QString cameraId;
    QString displayName;
    QString type;
    QString title;
    QString dedupKey;
    QString payloadJson;
    QString segmentId;
    QDateTime occurredAtUtc;
    ArchiveEventSeverity severity = ArchiveEventSeverity::Info;
};

struct MonitorEventEntry
{
    QString eventId;
    QString sessionId;
    QString cameraId;
    QString displayName;
    QString type;
    QString title;
    QString dedupKey;
    QString payloadJson;
    QString segmentId;
    QDateTime firstOccurredAtUtc;
    QDateTime lastOccurredAtUtc;
    ArchiveEventSeverity severity = ArchiveEventSeverity::Info;
    int occurrenceCount = 1;
    bool acknowledged = false;
    bool cleared = false;
};

struct MonitorEventSnapshot
{
    QList<MonitorEventEntry> entries;
    QHash<QString, int> activeEventCountsBySession;
    QHash<QString, int> activeAlarmCountsBySession;
    QHash<QString, bool> hasUnacknowledgedAlarmBySession;
};

struct MonitorEventIngestResult
{
    MonitorEventEntry entry;
    bool created = false;
    bool aggregated = false;
    bool raisedAlarm = false;
};

class MonitorEventCenter
{
public:
    MonitorEventIngestResult Ingest(const MonitorEventCandidate& candidate, int throttleMs = 4000);
    bool Acknowledge(const QString& eventId);
    bool Clear(const QString& eventId);
    void ClearSession(const QString& sessionId);
    MonitorEventSnapshot GetSnapshot() const;
    MonitorEventEntry FindEvent(const QString& eventId) const;

private:
    MonitorEventSnapshot BuildSnapshotLocked() const;

    mutable std::mutex mux_;
    QList<MonitorEventEntry> entries_;
};
