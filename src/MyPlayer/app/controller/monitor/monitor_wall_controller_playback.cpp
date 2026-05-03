

#include "monitor_wall_controller.h"

#include "../../../core/archive/archive_path_policy.h"
#include "../../../core/recording/recording_service.h"
#include "../../../ui/monitor/monitor_tile_widget.h"
#include "../../../ui/monitor/monitor_wall_window.h"

#include <QDate>
#include <QDir>
#include <QFileInfo>
#include <QMetaObject>
#include <QPointer>

#include <algorithm>
#include <thread>

void MonitorWallController::RefreshPlaybackPanel(const MonitorWorkspaceSnapshot& snapshot)
{
    if (!window_)
        return;

    const QDate selectedDay = playbackSelectedDate_.isValid() ? playbackSelectedDate_ : QDate::currentDate();
    const QDate visibleMonth = playbackVisibleMonth_.isValid()
        ? playbackVisibleMonth_
        : QDate(selectedDay.year(), selectedDay.month(), 1);
    window_->SetSelectedPlaybackDate(selectedDay);

    const QString sessionId = snapshot.selectedSessionId.trimmed();
    if (sessionId.isEmpty())
    {
        window_->SetPlaybackCalendarHighlights({}, visibleMonth);
        window_->SetPlaybackFiles({}, "Select a camera.", {});
        return;
    }

    MonitorSourceDescriptor source;
    if (!workspace_.GetSessionSource(sessionId, &source))
    {
        window_->SetPlaybackCalendarHighlights({}, visibleMonth);
        window_->SetPlaybackFiles({}, "Camera source missing.", {});
        return;
    }

    QString calendarError;
    const QList<ArchiveDaySummary> days = QueryPlaybackCalendarMonth(source.cameraId, visibleMonth, &calendarError);
    window_->SetPlaybackCalendarHighlights(days, visibleMonth);

    QString segmentError;
    const QList<ArchiveSegmentRecord> segments = QueryPlaybackSegments(source.cameraId, selectedDay, &segmentError);
    QString message;
    if (!calendarError.isEmpty())
        message = calendarError;
    else if (!segmentError.isEmpty())
        message = segmentError;
    else if (segments.isEmpty())
        message = QString("No recordings for %1.").arg(selectedDay.toString("yyyy-MM-dd"));
    window_->SetPlaybackFiles({}, message, segments);
}

QList<ArchiveDaySummary> MonitorWallController::QueryPlaybackCalendarMonth(
    const QString& cameraId,
    const QDate& month,
    QString* errorMessage) const
{
    QList<ArchiveDaySummary> summaries;
    if (cameraId.trimmed().isEmpty() || !month.isValid())
        return summaries;

    const QString archiveRoot = ArchiveRootDir().trimmed();
    if (archiveRoot.isEmpty())
    {
        if (errorMessage)
            *errorMessage = "Archive root is empty.";
        return summaries;
    }

    ArchivePathPolicy policy;
    policy.archiveRootDir = archiveRoot;
    policy.recordingContainer = window_
        ? ArchivePathPolicy::NormalizeRecordingContainer(window_->RecordingContainer())
        : QStringLiteral("mkv");
    policy.segmentDurationSeconds = window_
        ? std::max(10, window_->RecordingSegmentDurationSeconds())
        : 300;

    RecordingService service;
    QString initError;
    if (!service.Initialize(policy, &initError))
    {
        if (errorMessage)
            *errorMessage = initError;
        return summaries;
    }

    QString queryError;
    summaries = service.QueryService().QueryCalendarMonth(cameraId.trimmed(), month, &queryError);
    if (errorMessage && !queryError.isEmpty())
        *errorMessage = queryError;
    return summaries;
}

QList<ArchiveSegmentRecord> MonitorWallController::QueryPlaybackSegments(
    const QString& cameraId,
    QString* errorMessage) const
{
    return QueryPlaybackSegments(
        cameraId,
        playbackSelectedDate_.isValid() ? playbackSelectedDate_ : QDate::currentDate(),
        errorMessage);
}

QList<ArchiveSegmentRecord> MonitorWallController::QueryPlaybackSegments(
    const QString& cameraId,
    const QDate& day,
    QString* errorMessage) const
{
    QList<ArchiveSegmentRecord> segments;
    if (cameraId.trimmed().isEmpty() || !day.isValid())
        return segments;

    const QString archiveRoot = ArchiveRootDir().trimmed();
    if (archiveRoot.isEmpty())
    {
        if (errorMessage)
            *errorMessage = "Archive root is empty.";
        return segments;
    }

    ArchivePathPolicy policy;
    policy.archiveRootDir = archiveRoot;
    policy.recordingContainer = window_
        ? ArchivePathPolicy::NormalizeRecordingContainer(window_->RecordingContainer())
        : QStringLiteral("mkv");
    policy.segmentDurationSeconds = window_
        ? std::max(10, window_->RecordingSegmentDurationSeconds())
        : 300;

    RecordingService service;
    QString initError;
    if (!service.Initialize(policy, &initError))
    {
        if (errorMessage)
            *errorMessage = initError;
        return segments;
    }

    QString queryError;
    segments = service.QueryService().QueryDaySegments(cameraId.trimmed(), day, &queryError);
    if (errorMessage && !queryError.isEmpty())
        *errorMessage = queryError;
    return segments;
}

std::optional<ArchiveSegmentRecord> MonitorWallController::FindPlaybackSegmentByRelativePath(
    const QString& relativePath,
    QString* errorMessage) const
{
    const QString cleanedRelativePath = QDir::fromNativeSeparators(relativePath.trimmed());
    if (cleanedRelativePath.isEmpty())
        return std::nullopt;

    const QString archiveRoot = ArchiveRootDir().trimmed();
    if (archiveRoot.isEmpty())
    {
        if (errorMessage)
            *errorMessage = "Archive root is empty.";
        return std::nullopt;
    }

    ArchivePathPolicy policy;
    policy.archiveRootDir = archiveRoot;
    policy.recordingContainer = window_
        ? ArchivePathPolicy::NormalizeRecordingContainer(window_->RecordingContainer())
        : QStringLiteral("mkv");
    policy.segmentDurationSeconds = window_
        ? std::max(10, window_->RecordingSegmentDurationSeconds())
        : 300;

    RecordingService service;
    QString initError;
    if (!service.Initialize(policy, &initError))
    {
        if (errorMessage)
            *errorMessage = initError;
        return std::nullopt;
    }

    QString queryError;
    const std::optional<ArchiveSegmentRecord> record =
        service.QueryService().FindSegmentByRelativePath(cleanedRelativePath, &queryError);
    if (errorMessage && !queryError.isEmpty())
        *errorMessage = queryError;
    return record;
}

void MonitorWallController::EnsurePlaybackProxyAsync(const ArchiveSegmentRecord& segment)
{
    if (segment.segmentId.trimmed().isEmpty()
        || segment.playbackProxyReady
        || pendingPlaybackProxySegments_.contains(segment.segmentId))
    {
        return;
    }

    const QString archiveRoot = ArchiveRootDir().trimmed();
    if (archiveRoot.isEmpty())
        return;

    const QString rawAbsolutePath = ResolvePlaybackSegmentAbsolutePath(segment.relativePath);
    if (rawAbsolutePath.isEmpty())
        return;

    pendingPlaybackProxySegments_.insert(segment.segmentId);
    if (window_)
        window_->SetStatusText(QString("Generating MP4 playback proxy for %1...").arg(segment.cameraId));

    const ArchivePathPolicy policy = [this, archiveRoot]() {
        ArchivePathPolicy value;
        value.archiveRootDir = archiveRoot;
        value.recordingContainer = window_
            ? ArchivePathPolicy::NormalizeRecordingContainer(window_->RecordingContainer())
            : QStringLiteral("mkv");
        value.segmentDurationSeconds = window_
            ? std::max(10, window_->RecordingSegmentDurationSeconds())
            : 300;
        return value;
    }();

    const QPointer<MonitorWallWindow> guardWindow(window_.get());
    std::thread([this, guardWindow, policy, segment, rawAbsolutePath]() {
        PlaybackProxyTranscodeResult transcodeResult;
        QString transcodeError;
        const PlaybackProxyTranscodeRequest request{
            policy,
            segment,
            rawAbsolutePath,
            QStringLiteral("h264_aac_mp4")
        };
        const bool transcoded = PlaybackProxyTranscoder::Transcode(request, &transcodeResult, &transcodeError);

        QString finalizeError;
        if (transcoded)
        {
            RecordingService service;
            if (service.Initialize(policy, &finalizeError))
            {
                if (!service.FinalizePlaybackProxy(transcodeResult.proxyRecord, &finalizeError))
                    finalizeError = finalizeError.trimmed().isEmpty() ? QStringLiteral("Failed to index playback proxy.") : finalizeError;
            }
            else
            {
                finalizeError = finalizeError.trimmed().isEmpty() ? QStringLiteral("Failed to open archive index for playback proxy.") : finalizeError;
            }
        }

        if (!guardWindow)
            return;

        QMetaObject::invokeMethod(
            guardWindow.data(),
            [this, guardWindow, segment, transcoded, transcodeError, finalizeError]() {
                pendingPlaybackProxySegments_.remove(segment.segmentId);
                if (!guardWindow)
                    return;

                if (!transcoded)
                {
                    guardWindow->SetStatusText(QString("MP4 playback proxy failed: %1").arg(transcodeError));
                }
                else if (!finalizeError.trimmed().isEmpty())
                {
                    guardWindow->SetStatusText(QString("MP4 proxy indexed with warning: %1").arg(finalizeError));
                }
                else
                {
                    guardWindow->SetStatusText(QString("MP4 playback proxy ready for %1").arg(segment.cameraId));
                }
                RefreshUi();
            },
            Qt::QueuedConnection);
    }).detach();
}

QString MonitorWallController::ResolvePlaybackSegmentAbsolutePath(const QString& relativePath) const
{
    const QString cleanedRelativePath = QDir::fromNativeSeparators(relativePath.trimmed());
    const QString archiveRoot = ArchiveRootDir().trimmed();
    if (cleanedRelativePath.isEmpty() || archiveRoot.isEmpty())
        return {};

    const QString absolutePath = QDir(archiveRoot).filePath(cleanedRelativePath);
    return QFileInfo::exists(absolutePath) ? QDir::cleanPath(absolutePath) : QString{};
}

QString MonitorWallController::ResolvePreferredPlaybackAbsolutePath(const ArchiveSegmentRecord& segment) const
{
    if (segment.playbackProxyReady && !segment.playbackProxyRelativePath.trimmed().isEmpty())
    {
        const QString proxyPath = ResolvePlaybackSegmentAbsolutePath(segment.playbackProxyRelativePath);
        if (!proxyPath.isEmpty())
            return proxyPath;
    }
    return ResolvePlaybackSegmentAbsolutePath(segment.relativePath);
}

void MonitorWallController::ReturnPoppedTileToWall(const QString& sessionId)
{
    MonitorTileWidget* tile = tiles_.value(sessionId, nullptr);
    if (!tile)
        return;

    tile->show();
}

bool MonitorWallController::SessionMatchesCurrentGroupFilter(const MonitorSourceDescriptor& source) const
{
    (void)source;
    return true;
}

bool MonitorWallController::SessionMatchesGroupFilter(
    const QString& groupFilter,
    const MonitorSourceDescriptor& source)
{
    if (groupFilter.trimmed().isEmpty())
        return true;
    return source.groupName.trimmed() == groupFilter.trimmed();
}

quint64 MonitorWallController::BeginPendingOpen(
    QHash<QString, quint64>& requestMap,
    quint64& nextRequestId,
    const QString& sessionId)
{
    const quint64 requestId = ++nextRequestId;
    requestMap.insert(sessionId, requestId);
    return requestId;
}

void MonitorWallController::InvalidatePendingOpen(QHash<QString, quint64>& requestMap, const QString& sessionId)
{
    requestMap.remove(sessionId);
}

void MonitorWallController::InvalidatePendingOpens(QHash<QString, quint64>& requestMap)
{
    requestMap.clear();
}

bool MonitorWallController::MatchesPendingOpen(
    const QHash<QString, quint64>& requestMap,
    const QString& sessionId,
    quint64 requestId)
{
    return requestMap.value(sessionId, 0) == requestId;
}

void MonitorWallController::SyncRecordingStatusFeedback(const MonitorWorkspaceSnapshot& snapshot)
{
    if (!window_)
        return;

    QSet<QString> activeSessionIds;
    for (const MonitorSessionSnapshot& session : snapshot.sessions)
    {
        activeSessionIds.insert(session.sessionId);
        const QString currentError = session.recordingLastError.trimmed();
        const QString previousError = lastRecordingErrors_.value(session.sessionId);
        if (currentError == previousError)
            continue;

        if (currentError.isEmpty())
        {
            lastRecordingErrors_.remove(session.sessionId);
            continue;
        }

        lastRecordingErrors_.insert(session.sessionId, currentError);
        const QString name = session.displayName.trimmed().isEmpty()
            ? session.cameraId.trimmed()
            : session.displayName.trimmed();
        window_->SetStatusText(QString("REC failed %1: %2").arg(name, currentError));
    }

    for (auto it = lastRecordingErrors_.begin(); it != lastRecordingErrors_.end(); )
    {
        if (!activeSessionIds.contains(it.key()))
            it = lastRecordingErrors_.erase(it);
        else
            ++it;
    }
}

void MonitorWallController::ApplyAiPolicies(
    const MonitorWorkspaceSnapshot& snapshot,
    const MonitorEventSnapshot& eventSnapshot,
    const QSet<QString>& visibleSessionIds)
{
    for (const MonitorSessionSnapshot& session : snapshot.sessions)
    {
        MonitorSourceDescriptor source;
        if (!workspace_.GetSessionSource(session.sessionId, &source))
            continue;

        MonitorAiSessionPolicy policy;
        policy.focusRoute = session.sessionId == snapshot.selectedSessionId;
        policy.alarmRoute = eventSnapshot.activeAlarmCountsBySession.value(session.sessionId, 0) > 0;
        policy.fullscreenRoute = maximizedSessionId_ == session.sessionId || popouts_.contains(session.sessionId);
        const bool visible = visibleSessionIds.contains(session.sessionId);
        policy.priorityTier = policy.fullscreenRoute
            ? AiPriorityTier::Fullscreen
            : (policy.alarmRoute
                ? AiPriorityTier::Alarm
                : (policy.focusRoute ? AiPriorityTier::Focused : AiPriorityTier::Background));
        policy.shouldRunAsr = source.enableAsr
            && visible
            && (policy.focusRoute || policy.alarmRoute || policy.fullscreenRoute);
        policy.detectorMinimumSkipFrames = source.enableDetector
            ? (policy.fullscreenRoute
                ? 0
                : (policy.alarmRoute
                    ? 1
                    : (policy.focusRoute
                        ? (visible ? 4 : 8)
                        : (visible ? 12 : 24))))
            : 0;
        workspace_.ApplySessionAiPolicy(session.sessionId, policy, nullptr);
    }
}

QString MonitorWallController::ArchiveRootDir() const
{
    return window_ ? window_->ArchiveRootDir() : QString{};
}

QString MonitorWallController::ResolveArchivePathForEvent(
    const MonitorEventEntry& event, QString* errorMessage) const
{
    if (event.segmentId.trimmed().isEmpty())
    {
        if (errorMessage)
            *errorMessage = "Selected event is not linked to an archive segment.";
        return {};
    }

    const QString archiveRoot = ArchiveRootDir();
    if (archiveRoot.trimmed().isEmpty())
    {
        if (errorMessage)
            *errorMessage = "Archive root directory is empty.";
        return {};
    }

    ArchivePathPolicy policy;
    policy.archiveRootDir = archiveRoot.trimmed();
    policy.recordingContainer = window_
        ? ArchivePathPolicy::NormalizeRecordingContainer(window_->RecordingContainer())
        : QStringLiteral("mkv");
    policy.segmentDurationSeconds = window_
        ? std::max(10, window_->RecordingSegmentDurationSeconds())
        : 300;

    RecordingService service;
    QString initError;
    if (!service.Initialize(policy, &initError))
    {
        if (errorMessage)
            *errorMessage = initError;
        return {};
    }

    QString queryError;
    const QList<ArchiveSegmentRecord> segments = service.QueryService().QueryDaySegments(
        event.cameraId,
        event.lastOccurredAtUtc.isValid() ? event.lastOccurredAtUtc.date() : QDate::currentDate(),
        &queryError);
    if (!queryError.isEmpty() && errorMessage)
        *errorMessage = queryError;

    for (const ArchiveSegmentRecord& segment : segments)
    {
        if (segment.segmentId == event.segmentId)
            return QDir(archiveRoot).filePath(segment.relativePath);
    }

    if (errorMessage && errorMessage->isEmpty())
        *errorMessage = "Archive segment not found for selected event.";
    return {};
}

bool MonitorWallController::IsNetworkSource(const QString& sourceUrl)
{
    return sourceUrl.contains("://");
}

MonitorSourceDescriptor MonitorWallController::NormalizeSource(MonitorSourceDescriptor source)
{
    if (source.displayName.trimmed().isEmpty())
        source.displayName = source.cameraId;

    if (IsNetworkSource(source.sourceUrl))
    {
        source.openOptions = source.preferLowLatency
            ? StreamOpenOptions::LowLatencyNetwork()
            : StreamOpenOptions::DefaultNetwork();
        const int reconnectJitterMs = ReconnectJitterMsForSession(source.cameraId);
        source.openOptions.reconnect.baseDelayMs =
            std::max(250, source.openOptions.reconnect.baseDelayMs + reconnectJitterMs);
        source.openOptions.reconnect.maxDelayMs =
            std::max(source.openOptions.reconnect.maxDelayMs, source.openOptions.reconnect.baseDelayMs * 4);
    }
    else
    {
        source.preferLowLatency = false;
        source.openOptions = StreamOpenOptions::DefaultFile();
    }

    return source;
}

int MonitorWallController::ReconnectJitterMsForSession(const QString& sessionId)
{
    return 250 + static_cast<int>(qHash(sessionId.trimmed()) % 1250U);
}

MonitorWorkspaceSnapshot MonitorWallController::FilteredSnapshot(const MonitorWorkspaceSnapshot& snapshot) const
{
    MonitorWorkspaceSnapshot filtered = snapshot;
    const QStringList orderedIds = NormalizeAssignedSessionOrder(snapshot, sessionOrder_);

    QHash<QString, MonitorSessionSnapshot> sessionsById;
    for (const MonitorSessionSnapshot& session : snapshot.sessions)
        sessionsById.insert(session.sessionId, session);

    filtered.sessions.clear();
    filtered.layout.placements.clear();
    const int columnCount = std::max(1, filtered.layout.columns);
    for (int slotIndex = 0; slotIndex < orderedIds.size(); ++slotIndex)
    {
        const QString sessionId = orderedIds.at(slotIndex).trimmed();
        if (sessionId.isEmpty())
            continue;
        if (popouts_.contains(sessionId))
            continue;
        const auto it = sessionsById.constFind(sessionId);
        if (it != sessionsById.constEnd())
        {
            filtered.sessions.push_back(it.value());
            MonitorTilePlacement placement;
            placement.sessionId = sessionId;
            placement.row = slotIndex / columnCount;
            placement.column = slotIndex % columnCount;
            placement.selected = it.value().selected;
            placement.audioOwner = it.value().audioOwner;
            filtered.layout.placements.push_back(placement);
        }
    }

    if (!maximizedSessionId_.trimmed().isEmpty())
    {
        MonitorWorkspaceSnapshot maximized = filtered;
        maximized.sessions.clear();
        maximized.layout.placements.clear();
        maximized.layout.preset = MonitorWallLayoutPreset::Single;
        maximized.layout.rows = 1;
        maximized.layout.columns = 1;

        for (const MonitorSessionSnapshot& session : filtered.sessions)
        {
            if (session.sessionId != maximizedSessionId_)
                continue;

            maximized.sessions.push_back(session);
            MonitorTilePlacement placement;
            placement.sessionId = session.sessionId;
            placement.selected = session.selected;
            placement.audioOwner = session.audioOwner;
            maximized.layout.placements.push_back(placement);
            break;
        }

        if (!maximized.sessions.isEmpty())
            return maximized;
    }

    return filtered;
}
