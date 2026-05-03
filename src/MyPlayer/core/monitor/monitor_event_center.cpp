

#include "monitor_event_center.h"

#include <QUuid>

#include <algorithm>

namespace
{

bool SeverityIsAlarm(ArchiveEventSeverity severity)
{
    return severity >= ArchiveEventSeverity::Alarm;
}
}

MonitorEventIngestResult MonitorEventCenter::Ingest(const MonitorEventCandidate& candidate, int throttleMs)
{
    MonitorEventIngestResult result;
    const QDateTime occurredAtUtc = candidate.occurredAtUtc.isValid()
        ? candidate.occurredAtUtc.toUTC()
        : QDateTime::currentDateTimeUtc();
    const int dedupWindowMs = std::max(250, throttleMs);

    std::lock_guard<std::mutex> lock(mux_);
    for (auto it = entries_.rbegin(); it != entries_.rend(); ++it)
    {
        if (it->cleared
            || it->sessionId != candidate.sessionId
            || it->dedupKey != candidate.dedupKey
            || it->type != candidate.type)
        {
            continue;
        }

        if (it->lastOccurredAtUtc.isValid()
            && it->lastOccurredAtUtc.msecsTo(occurredAtUtc) <= dedupWindowMs)
        {
            it->lastOccurredAtUtc = occurredAtUtc;
            it->title = candidate.title;
            it->payloadJson = candidate.payloadJson;
            if (!candidate.segmentId.trimmed().isEmpty())
                it->segmentId = candidate.segmentId;
            if (static_cast<int>(candidate.severity) > static_cast<int>(it->severity))
                it->severity = candidate.severity;
            it->occurrenceCount += 1;
            it->acknowledged = false;

            result.entry = *it;
            result.aggregated = true;
            result.raisedAlarm = SeverityIsAlarm(it->severity);
            return result;
        }
    }

    MonitorEventEntry entry;
    entry.eventId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    entry.sessionId = candidate.sessionId;
    entry.cameraId = candidate.cameraId;
    entry.displayName = candidate.displayName;
    entry.type = candidate.type;
    entry.title = candidate.title;
    entry.dedupKey = candidate.dedupKey;
    entry.payloadJson = candidate.payloadJson;
    entry.segmentId = candidate.segmentId;
    entry.firstOccurredAtUtc = occurredAtUtc;
    entry.lastOccurredAtUtc = occurredAtUtc;
    entry.severity = candidate.severity;
    entry.acknowledged = !SeverityIsAlarm(candidate.severity);
    entries_.push_front(entry);

    result.entry = entry;
    result.created = true;
    result.raisedAlarm = SeverityIsAlarm(candidate.severity);
    return result;
}

bool MonitorEventCenter::Acknowledge(const QString& eventId)
{
    std::lock_guard<std::mutex> lock(mux_);
    for (MonitorEventEntry& entry : entries_)
    {
        if (entry.eventId == eventId && !entry.cleared)
        {
            entry.acknowledged = true;
            return true;
        }
    }
    return false;
}

bool MonitorEventCenter::Clear(const QString& eventId)
{
    std::lock_guard<std::mutex> lock(mux_);
    for (MonitorEventEntry& entry : entries_)
    {
        if (entry.eventId == eventId && !entry.cleared)
        {
            entry.cleared = true;
            entry.acknowledged = true;
            return true;
        }
    }
    return false;
}

void MonitorEventCenter::ClearSession(const QString& sessionId)
{
    std::lock_guard<std::mutex> lock(mux_);
    for (MonitorEventEntry& entry : entries_)
    {
        if (entry.sessionId == sessionId)
        {
            entry.cleared = true;
            entry.acknowledged = true;
        }
    }
}

MonitorEventSnapshot MonitorEventCenter::GetSnapshot() const
{
    std::lock_guard<std::mutex> lock(mux_);
    return BuildSnapshotLocked();
}

MonitorEventEntry MonitorEventCenter::FindEvent(const QString& eventId) const
{
    std::lock_guard<std::mutex> lock(mux_);
    for (const MonitorEventEntry& entry : entries_)
    {
        if (entry.eventId == eventId)
            return entry;
    }
    return {};
}

MonitorEventSnapshot MonitorEventCenter::BuildSnapshotLocked() const
{
    MonitorEventSnapshot snapshot;
    for (const MonitorEventEntry& entry : entries_)
    {
        snapshot.entries.push_back(entry);
        if (entry.cleared)
            continue;

        snapshot.activeEventCountsBySession[entry.sessionId] += 1;
        if (SeverityIsAlarm(entry.severity))
        {
            snapshot.activeAlarmCountsBySession[entry.sessionId] += 1;
            if (!entry.acknowledged)
                snapshot.hasUnacknowledgedAlarmBySession[entry.sessionId] = true;
        }
    }

    std::sort(snapshot.entries.begin(), snapshot.entries.end(),
        [](const MonitorEventEntry& lhs, const MonitorEventEntry& rhs)
        {
            return lhs.lastOccurredAtUtc > rhs.lastOccurredAtUtc;
        });
    return snapshot;
}
