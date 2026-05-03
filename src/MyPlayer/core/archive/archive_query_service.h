#pragma once

#include "archive_index_store.h"
#include <optional>

class ArchiveQueryService
{
public:
    explicit ArchiveQueryService(ArchiveIndexStore& indexStore);

    QList<ArchiveDaySummary> QueryCalendarMonth(const QString& cameraId, const QDate& month,
        QString* errorMessage = nullptr) const;
    QList<ArchiveSegmentRecord> QueryDaySegments(const QString& cameraId, const QDate& day,
        QString* errorMessage = nullptr) const;
    QList<ArchiveSegmentRecord> QueryRecentSegments(const QString& cameraId, int limit,
        QString* errorMessage = nullptr) const;
    QList<ArchiveEventRecord> QueryDayEvents(const QString& cameraId, const QDate& day,
        QString* errorMessage = nullptr) const;
    std::optional<ArchiveSegmentRecord> FindSegmentByRelativePath(
        const QString& relativePath,
        QString* errorMessage = nullptr) const;

private:
    ArchiveIndexStore& indexStore_;
};
