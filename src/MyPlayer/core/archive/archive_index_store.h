#pragma once

#include "archive_models.h"
#include "archive_path_policy.h"

#include <QList>
#include <QSqlDatabase>
#include <QString>
#include <optional>

class ArchiveIndexStore
{
public:
    ArchiveIndexStore();
    ~ArchiveIndexStore();

    bool Open(const ArchivePathPolicy& policy, QString* errorMessage = nullptr);
    void Close();
    bool IsOpen() const;

    bool UpsertSegment(const ArchiveSegmentRecord& record, QString* errorMessage = nullptr);
    bool UpsertPlaybackProxy(const ArchivePlaybackProxyRecord& record, QString* errorMessage = nullptr);
    bool RecordEvent(const ArchiveEventRecord& record, QString* errorMessage = nullptr);
    bool ContainsSegmentPath(const QString& relativePath) const;

    QList<ArchiveDaySummary> QueryDays(const QString& cameraId, const QDate& month,
        QString* errorMessage = nullptr) const;
    QList<ArchiveSegmentRecord> QuerySegments(const QString& cameraId, const QDate& day,
        QString* errorMessage = nullptr) const;
    QList<ArchiveSegmentRecord> QueryRecentSegments(const QString& cameraId, int limit,
        QString* errorMessage = nullptr) const;
    QList<ArchiveEventRecord> QueryEvents(const QString& cameraId, const QDate& day,
        QString* errorMessage = nullptr) const;
    std::optional<ArchiveSegmentRecord> FindSegmentByRelativePath(
        const QString& relativePath,
        QString* errorMessage = nullptr) const;

private:
    bool EnsureSchema(QString* errorMessage);
    bool ExecuteStatement(const QString& sql, QString* errorMessage) const;
    bool RefreshDaySummary(const QString& cameraId, const QDate& day, QString* errorMessage);
    static ArchiveSegmentRecord BuildSegmentRecordFromQuery(const QSqlQuery& query, int offset = 0);

    static qint64 ToUtcMs(const QDateTime& value);
    static QDateTime FromUtcMs(qint64 value);
    static int SeverityToInt(ArchiveEventSeverity severity);
    static ArchiveEventSeverity SeverityFromInt(int severity);

    QSqlDatabase database_;
    QString connectionName_;
};
