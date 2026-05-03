#pragma once

#include "../../../core/monitor/monitor_event_center.h"
#include "../../../core/monitor/monitor_workspace_service.h"
#include "../../../core/archive/playback_proxy_transcoder.h"
#include "../../../core/recording/recording_service.h"
#include "../../service/config_service.h"

#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QDate>
#include <QtGlobal>
#include <memory>
#include <optional>

class ConfigService;
class MonitorTileWidget;
class MonitorTilePopoutWindow;
class MonitorWallWindow;
class QObject;
class QPushButton;
class QWidget;

class MonitorWallController
{
public:
    MonitorWallController(
        QWidget* host,
        ConfigService* configService,
        std::function<void(const QString&)> playArchiveSegmentHandler = {});
    ~MonitorWallController();

    QPushButton* ToggleButton() const;
    QList<QWidget*> AutoHideWidgets() const;
    void InstallEventFilters(QObject* filter) const;

    void LoadSettings();
    void SaveSettings();
    void ToggleWindow();

private:
    using WorkspaceState = MonitorWorkspacePreferences::WorkspaceState;

    struct SecondaryWallRuntime
    {
        QString workspaceId;
        QString workspaceName;
        std::unique_ptr<MonitorWallWindow> window;
        MonitorWorkspaceService workspace;
        QMap<QString, MonitorTileWidget*> tiles;
        QMap<QString, MonitorTilePopoutWindow*> popouts;
        MonitorWallLayoutPreset preset = MonitorWallLayoutPreset::Grid2x2;
        QString maximizedSessionId;
        QString screenName;
        QString groupFilter;
        QStringList sessionOrder;
        QHash<QString, quint64> pendingOpenRequests;
        quint64 nextOpenRequestId = 0;
        bool fullscreen = false;
    };

    void AddOrUpdateSource();
    void OpenSelected();
    void OpenAll();
    void CloseAll();
    void RemoveSelected();
    void BrowseArchiveRoot();
    void BrowseSourceFile();
    void ToggleFullscreen();
    void OnLayoutPresetChanged(MonitorWallLayoutPreset preset);
    void OnWorkspaceChanged(const QString& workspaceId);
    void OnPreviousWorkspace();
    void OnNextWorkspace();
    void OnSaveWorkspace();
    void OnNewWorkspace();
    void OnDeleteWorkspace();
    void OnScreenAssignmentChanged(const QString& screenName);
    void OnGroupFilterChanged(const QString& groupName);
    void OnOpenGroup();
    void OnCloseGroup();
    void OnFavoriteLayoutChanged(const QString& layoutId);
    void OnSaveFavoriteLayout();
    void OnDeleteFavoriteLayout();
    void OnTileActivated(const QString& sessionId);
    void OnTileMaximize(const QString& sessionId);
    void OnTileToggleDetector(const QString& sessionId, bool enabled);
    void OnTileToggleAsr(const QString& sessionId, bool enabled);
    void OnTileToggleRecording(const QString& sessionId, bool enabled);
    void OnTileRecordingMenuRequested(const QString& sessionId, int action);
    void OnTileToggleMute(const QString& sessionId, bool muted);
    void OnTileReopen(const QString& sessionId);
    void OnTileSnapshot(const QString& sessionId);
    void OnTilePopout(const QString& sessionId);
    void OnTilePopoutClosed(const QString& sessionId);
    void OnTileRemove(const QString& sessionId);
    void OnTileReordered(const QString& draggedSessionId, const QString& targetSessionId);
    void OnAcknowledgeSelectedEvent();
    void OnClearSelectedEvent();
    void OnJumpToSelectedEvent();
    void OnEventActivated(const QString& eventId);
    void OnCameraSelectionChanged(const QString& sessionId);
    void OnCameraActivated(const QString& sessionId);
    void OnPlaybackDayChanged(const QDate& day);
    void OnPlaybackMonthChanged(const QDate& month);
    void OnPlaybackSegmentRequested(const QString& relativePath);
    void OnToggleRecordAll(bool enabled);
    void OnWallSlotActivated(int slotIndex);
    void ReopenSessionAsync(const QString& sessionId, const QString& successText = {});
    void ReopenSessionAsyncToTile(
        const QString& sessionId,
        MonitorTileWidget* renderTile,
        const QString& successText = {});
    void ReopenSecondarySessionAsync(SecondaryWallRuntime& runtime, const QString& sessionId);
    void RefreshUi();
    void ApplyArchivePreferences(const ArchivePreferences& preferences);
    void SaveArchivePreferencesFromWindow() const;
    RecordingConfiguration BuildMonitorRecordingConfiguration() const;
    void EnsureTile(const QString& sessionId);
    void BindDetectorHandlers(const QString& sessionId);
    void BindDetectorHandlerForTile(const QString& sessionId, MonitorTileWidget* tile, bool recordEvents);
    void BindAsrHandlers(const QString& sessionId);
    void BindAsrHandlerForTile(const QString& sessionId, MonitorTileWidget* tile);
    void ClearAsrOutputForSession(const QString& sessionId);
    void RemoveTile(const QString& sessionId);
    void PopulateEditorFromSession(const QString& sessionId);
    void RecordDetectorEvent(const QString& sessionId, const DetectionResult& result);
    void LoadWorkspace(const WorkspaceState& workspaceState);
    void SyncSecondaryWalls();
    void RebuildSecondaryRuntime(SecondaryWallRuntime& runtime, const WorkspaceState& workspaceState);
    void ShowSecondaryRuntime(SecondaryWallRuntime& runtime);
    void RefreshSecondaryWalls();
    void RefreshSecondaryRuntime(SecondaryWallRuntime& runtime);
    void SaveSecondaryRuntimeState(SecondaryWallRuntime& runtime);
    void EnsureSecondaryTile(SecondaryWallRuntime& runtime, const QString& sessionId);
    void RemoveSecondaryTile(SecondaryWallRuntime& runtime, const QString& sessionId);
    void CloseSecondaryPopouts(SecondaryWallRuntime& runtime);
    void ReturnPoppedTileToSecondaryWall(SecondaryWallRuntime& runtime, const QString& sessionId);
    void TakeTileSnapshot(MonitorTileWidget* tile, const QString& filePrefix);
    void SaveCurrentWorkspaceState();
    void RefreshWorkspaceSelectors();
    WorkspaceState* FindWorkspaceState(const QString& workspaceId);
    const WorkspaceState* FindWorkspaceState(const QString& workspaceId) const;
    WorkspaceState::FavoriteLayoutState* FindFavoriteLayout(WorkspaceState& workspace, const QString& layoutId);
    const WorkspaceState::FavoriteLayoutState* FindFavoriteLayout(
        const WorkspaceState& workspace,
        const QString& layoutId) const;
    QString CreateWorkspaceName() const;
    QString CreateFavoriteLayoutName(const WorkspaceState& workspace) const;
    QList<QPair<QString, QString>> BuildWorkspaceItems() const;
    QList<QPair<QString, QString>> BuildScreenItems() const;
    QList<QPair<QString, QString>> BuildGroupFilterItems(const MonitorWorkspaceSnapshot& snapshot) const;
    QList<QPair<QString, QString>> BuildFavoriteLayoutItems() const;
    QString ResolveCurrentScreenName() const;
    QString ResolveSecondaryScreenName(const WorkspaceState& workspaceState, const QSet<QString>& occupiedScreens) const;
    void MoveWindowToAssignedScreen();
    QStringList NormalizeSessionOrder(const MonitorWorkspaceSnapshot& snapshot) const;
    MonitorWorkspaceSnapshot ApplySessionOrder(MonitorWorkspaceSnapshot snapshot) const;
    static QStringList NormalizeSessionOrder(
        const MonitorWorkspaceSnapshot& snapshot,
        const QStringList& preferredOrder);
    static MonitorWorkspaceSnapshot ApplySessionOrder(
        MonitorWorkspaceSnapshot snapshot,
        const QStringList& preferredOrder);
    QSet<QString> VisibleSessionIds(const MonitorWorkspaceSnapshot& snapshot) const;
    int WallCapacity() const;
    bool EnsureSessionVisibleOnWall(const QString& sessionId);
    void RemoveSessionFromWall(const QString& sessionId);
    bool AssignSessionToWallSlot(const QString& sessionId, int preferredSlotIndex);
    int FindAssignedSlotIndex(const QString& sessionId) const;
    static QStringList NormalizeAssignedSessionOrder(
        const MonitorWorkspaceSnapshot& snapshot,
        const QStringList& preferredOrder);
    bool NormalizeAudioRouteForVisibleSessions(
        const MonitorWorkspaceSnapshot& snapshot,
        const QSet<QString>& visibleSessionIds);
    void RefreshPlaybackPanel(const MonitorWorkspaceSnapshot& snapshot);
    QList<ArchiveDaySummary> QueryPlaybackCalendarMonth(
        const QString& cameraId,
        const QDate& month,
        QString* errorMessage = nullptr) const;
    QList<ArchiveSegmentRecord> QueryPlaybackSegments(const QString& cameraId, QString* errorMessage = nullptr) const;
    QList<ArchiveSegmentRecord> QueryPlaybackSegments(
        const QString& cameraId,
        const QDate& day,
        QString* errorMessage = nullptr) const;
    std::optional<ArchiveSegmentRecord> FindPlaybackSegmentByRelativePath(
        const QString& relativePath,
        QString* errorMessage = nullptr) const;
    void EnsurePlaybackProxyAsync(const ArchiveSegmentRecord& segment);
    QString ResolvePlaybackSegmentAbsolutePath(const QString& relativePath) const;
    QString ResolvePreferredPlaybackAbsolutePath(const ArchiveSegmentRecord& segment) const;
    void ReturnPoppedTileToWall(const QString& sessionId);
    bool SessionMatchesCurrentGroupFilter(const MonitorSourceDescriptor& source) const;
    static bool SessionMatchesGroupFilter(const QString& groupFilter, const MonitorSourceDescriptor& source);
    static quint64 BeginPendingOpen(
        QHash<QString, quint64>& requestMap,
        quint64& nextRequestId,
        const QString& sessionId);
    static void InvalidatePendingOpen(QHash<QString, quint64>& requestMap, const QString& sessionId);
    static void InvalidatePendingOpens(QHash<QString, quint64>& requestMap);
    static bool MatchesPendingOpen(
        const QHash<QString, quint64>& requestMap,
        const QString& sessionId,
        quint64 requestId);
    void SyncRecordingStatusFeedback(const MonitorWorkspaceSnapshot& snapshot);
    void ApplyAiPolicies(
        const MonitorWorkspaceSnapshot& snapshot,
        const MonitorEventSnapshot& eventSnapshot,
        const QSet<QString>& visibleSessionIds);
    QString ArchiveRootDir() const;
    QString ResolveArchivePathForEvent(const MonitorEventEntry& event, QString* errorMessage = nullptr) const;
    static bool IsNetworkSource(const QString& sourceUrl);
    static MonitorSourceDescriptor NormalizeSource(MonitorSourceDescriptor source);
    static int ReconnectJitterMsForSession(const QString& sessionId);
    MonitorWorkspaceSnapshot FilteredSnapshot(const MonitorWorkspaceSnapshot& snapshot) const;

    QWidget* host_ = nullptr;
    ConfigService* config_ = nullptr;
    std::unique_ptr<MonitorWallWindow> window_;
    QPushButton* toggleButton_ = nullptr;
    MonitorWorkspaceService workspace_;
    QMap<QString, MonitorTileWidget*> tiles_;
    QMap<QString, MonitorTilePopoutWindow*> popouts_;
    MonitorEventCenter eventCenter_;
    MonitorWallLayoutPreset currentPreset_ = MonitorWallLayoutPreset::Grid2x2;
    QString maximizedSessionId_;
    QString currentWorkspaceId_;
    QString currentScreenName_;
    QString currentGroupFilter_;
    QString currentFavoriteLayoutId_;
    QStringList sessionOrder_;
    int activeWallSlotIndex_ = -1;
    QDate playbackSelectedDate_;
    QDate playbackVisibleMonth_;
    QSet<QString> pendingPlaybackProxySegments_;
    QHash<QString, QString> lastRecordingErrors_;
    QList<WorkspaceState> workspaceStates_;
    QMap<QString, SecondaryWallRuntime*> secondaryRuntimes_;
    QHash<QString, quint64> pendingOpenRequests_;
    quint64 nextOpenRequestId_ = 0;
    std::function<void(const QString&)> playArchiveSegmentHandler_;
};
