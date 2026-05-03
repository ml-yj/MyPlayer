

#include "archive_query_service.h"

ArchiveQueryService::ArchiveQueryService(ArchiveIndexStore& indexStore)
    : indexStore_(indexStore)
{
}

QList<ArchiveDaySummary> ArchiveQueryService::QueryCalendarMonth(
    const QString& cameraId, const QDate& month, QString* errorMessage) const
{
    return indexStore_.QueryDays(cameraId, month, errorMessage);
}

QList<ArchiveSegmentRecord> ArchiveQueryService::QueryDaySegments(
    const QString& cameraId, const QDate& day, QString* errorMessage) const
{
    return indexStore_.QuerySegments(cameraId, day, errorMessage);
}

QList<ArchiveSegmentRecord> ArchiveQueryService::QueryRecentSegments(
    const QString& cameraId,
    int limit,
    QString* errorMessage) const
{
    return indexStore_.QueryRecentSegments(cameraId, limit, errorMessage);
}

QList<ArchiveEventRecord> ArchiveQueryService::QueryDayEvents(
    const QString& cameraId, const QDate& day, QString* errorMessage) const
{
    return indexStore_.QueryEvents(cameraId, day, errorMessage);
}

std::optional<ArchiveSegmentRecord> ArchiveQueryService::FindSegmentByRelativePath(
    const QString& relativePath,
    QString* errorMessage) const
{
    return indexStore_.FindSegmentByRelativePath(relativePath, errorMessage);
}
