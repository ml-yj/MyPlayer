#pragma once

#include "../archive/archive_index_store.h"
#include "../archive/archive_query_service.h"
#include "../archive/archive_path_policy.h"
#include "recording_models.h"

#include <optional>

class QFileInfo;

struct RecordingRecoverySummary
{
    int importedSegments = 0;
    int skippedExistingSegments = 0;
    int skippedRecentSegments = 0;
    int failedImports = 0;
};

class RecordingService
{
public:
    RecordingService();

    bool Initialize(const ArchivePathPolicy& policy, QString* errorMessage = nullptr);
    void Shutdown();
    bool IsReady() const;

    const ArchivePathPolicy& Policy() const;
    ArchiveQueryService& QueryService();
    const ArchiveQueryService& QueryService() const;
    const RecordingRecoverySummary& LastRecoverySummary() const;

    std::optional<RecordingActiveSegment> BeginSegment(
        const RecordingSessionDescriptor& session, QDateTime startUtc = QDateTime::currentDateTimeUtc()) const;
    bool FinalizeSegment(const RecordingActiveSegment& segment, qint64 fileSizeBytes,
        QDateTime endUtc = QDateTime::currentDateTimeUtc(), QString* errorMessage = nullptr);
    bool FinalizePlaybackProxy(const ArchivePlaybackProxyRecord& record, QString* errorMessage = nullptr);
    bool RecordEvent(const ArchiveEventRecord& record, QString* errorMessage = nullptr);

private:
    void RecoverSegments();
    bool TryImportRecoveredSegment(const QString& relativePath, const QFileInfo& fileInfo);

    ArchivePathPolicy policy_;
    ArchiveIndexStore indexStore_;
    ArchiveQueryService queryService_;
    bool ready_ = false;
    RecordingRecoverySummary lastRecoverySummary_;
};
