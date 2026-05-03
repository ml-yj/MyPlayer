

#include "monitor_wall_controller.h"

#include "../../service/config_service.h"
#include "../../../core/archive/archive_path_policy.h"
#include "../../../core/media/demux.h"
#include "../../../core/recording/recording_service.h"
#include "../../../core/video/video_callback.h"
#include "../../../ui/monitor/monitor_tile_widget.h"
#include "../../../ui/monitor/monitor_wall_window.h"
#include "../../../ui/video/video_widget.h"

#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QObject>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QScreen>
#include <QSet>
#include <QStandardPaths>
#include <QTimer>
#include <QTimeZone>
#include <QWindow>
#include <QWidget>

#include <algorithm>
#include <chrono>
#include <memory>
#include <thread>

void MonitorWallController::LoadWorkspace(const WorkspaceState& workspaceState)
{
    InvalidatePendingOpens(pendingOpenRequests_);
    const QStringList popoutIds = popouts_.keys();
    for (const QString& sessionId : popoutIds)
    {
        ReturnPoppedTileToWall(sessionId);
        if (MonitorTilePopoutWindow* popout = popouts_.take(sessionId))
        {
            popout->blockSignals(true);
            popout->close();
            popout->deleteLater();
        }
    }

    const MonitorWorkspaceSnapshot existingSnapshot = workspace_.GetSnapshot(currentPreset_);
    for (const MonitorSessionSnapshot& session : existingSnapshot.sessions)
    {
        workspace_.RemoveSession(session.sessionId);
        RemoveTile(session.sessionId);
    }

    currentWorkspaceId_ = workspaceState.workspaceId.trimmed();
    currentPreset_ = static_cast<MonitorWallLayoutPreset>(std::clamp(workspaceState.layoutPreset, 0, 4));
    maximizedSessionId_ = workspaceState.maximizedSessionId.trimmed();
    currentScreenName_ = workspaceState.assignedScreen.trimmed();
    currentGroupFilter_.clear();
    currentFavoriteLayoutId_ = workspaceState.activeFavoriteLayoutId.trimmed();
    if (!currentFavoriteLayoutId_.isEmpty() && !FindFavoriteLayout(workspaceState, currentFavoriteLayoutId_))
        currentFavoriteLayoutId_.clear();
    sessionOrder_ = workspaceState.sessionOrder;
    window_->SetArchiveRootDir(workspaceState.archiveRootDir.trimmed());

    for (const MonitorSourceState& state : workspaceState.sources)
    {
        MonitorSourceDescriptor source;
        source.cameraId = state.cameraId;
        source.displayName = state.displayName;
        source.groupName = state.groupName;
        source.sourceUrl = state.sourceUrl;
        source.preferLowLatency = state.preferLowLatency;
        source.enableDetector = state.enableDetector;
        source.enableAsr = state.enableAsr;
        source.enableRecording = state.enableRecording;
        const QString sessionId = workspace_.UpsertSource(NormalizeSource(source));
        if (sessionId.isEmpty())
            continue;
        if (state.muted)
            workspace_.SetSessionMuted(sessionId, true);
        EnsureTile(sessionId);
    }

    MonitorWorkspaceSnapshot snapshot = workspace_.GetSnapshot(currentPreset_);
    sessionOrder_ = NormalizeAssignedSessionOrder(snapshot, sessionOrder_);
    activeWallSlotIndex_ = -1;

    if (!workspaceState.selectedSessionId.trimmed().isEmpty())
        workspace_.SelectSession(workspaceState.selectedSessionId.trimmed());
    else
    {
        for (const QString& sessionId : sessionOrder_)
        {
            if (!sessionId.trimmed().isEmpty())
            {
                workspace_.SelectSession(sessionId);
                break;
            }
        }
    }

    if (!workspaceState.audioSessionId.trimmed().isEmpty())
        workspace_.SetAudioSession(workspaceState.audioSessionId.trimmed());
    else if (!workspaceState.selectedSessionId.trimmed().isEmpty())
        workspace_.SetAudioSession(workspaceState.selectedSessionId.trimmed());
    else
    {
        for (const QString& sessionId : sessionOrder_)
        {
            if (!sessionId.trimmed().isEmpty())
            {
                workspace_.SetAudioSession(sessionId);
                break;
            }
        }
    }

    if (!workspaceState.selectedSessionId.trimmed().isEmpty())
        PopulateEditorFromSession(workspaceState.selectedSessionId.trimmed());
    else
    {
        bool populated = false;
        for (const QString& sessionId : sessionOrder_)
        {
            if (sessionId.trimmed().isEmpty())
                continue;
            PopulateEditorFromSession(sessionId);
            populated = true;
            break;
        }
        if (!populated)
            window_->WriteSourceDescriptor(MonitorSourceDescriptor{});
    }

    if (!workspaceState.selectedSessionId.trimmed().isEmpty())
        activeWallSlotIndex_ = FindAssignedSlotIndex(workspaceState.selectedSessionId.trimmed());

    RefreshWorkspaceSelectors();
    MoveWindowToAssignedScreen();
    RefreshUi();
}

void MonitorWallController::SyncSecondaryWalls()
{
    QSet<QString> occupiedScreens;
    if (window_ && window_->isVisible())
    {
        QString currentScreen = currentScreenName_.trimmed();
        if (currentScreen.isEmpty() && window_->screen())
            currentScreen = window_->screen()->name();
        if (!currentScreen.isEmpty())
            occupiedScreens.insert(currentScreen);
    }

    QSet<QString> desiredWorkspaceIds;
    for (const WorkspaceState& workspaceState : workspaceStates_)
    {
        if (workspaceState.workspaceId == currentWorkspaceId_ || !workspaceState.windowVisible)
            continue;

        const QString screenName = ResolveSecondaryScreenName(workspaceState, occupiedScreens);
        if (screenName.isEmpty())
            continue;

        desiredWorkspaceIds.insert(workspaceState.workspaceId);
        occupiedScreens.insert(screenName);

        auto it = secondaryRuntimes_.find(workspaceState.workspaceId);
        if (it == secondaryRuntimes_.end())
        {
            auto* runtime = new SecondaryWallRuntime();
            runtime->workspaceId = workspaceState.workspaceId;
            runtime->workspaceName = workspaceState.workspaceName;
            runtime->screenName = screenName;
            it = secondaryRuntimes_.insert(workspaceState.workspaceId, runtime);
        }

        it.value()->screenName = screenName;
        RebuildSecondaryRuntime(*it.value(), workspaceState);
    }

    for (auto it = secondaryRuntimes_.begin(); it != secondaryRuntimes_.end(); )
    {
        if (desiredWorkspaceIds.contains(it.key()))
        {
            ++it;
            continue;
        }

        if (it.value())
        {
            CloseSecondaryPopouts(*it.value());
            it.value()->workspace.CloseAll();
            for (auto tileIt = it.value()->tiles.begin(); tileIt != it.value()->tiles.end(); ++tileIt)
                delete tileIt.value();
            it.value()->tiles.clear();
            delete it.value();
        }
        it = secondaryRuntimes_.erase(it);
    }
}

void MonitorWallController::RebuildSecondaryRuntime(
    SecondaryWallRuntime& runtime,
    const WorkspaceState& workspaceState)
{
    if (!runtime.window)
    {
        runtime.window = std::make_unique<MonitorWallWindow>();
        runtime.window->SetPresentationMode(true);
        QObject::connect(runtime.window.get(), &MonitorWallWindow::WindowVisibilityChanged, runtime.window.get(),
            [this, workspaceId = workspaceState.workspaceId](bool visible) {
                if (WorkspaceState* state = FindWorkspaceState(workspaceId))
                    state->windowVisible = visible;
                auto it = secondaryRuntimes_.find(workspaceId);
                if (it != secondaryRuntimes_.end() && it.value() && !visible)
                    it.value()->workspace.CloseAll();
                SaveSettings();
            });
        QObject::connect(runtime.window.get(), &MonitorWallWindow::TileReorderRequested, runtime.window.get(),
            [this, &runtime](const QString& draggedSessionId, const QString& targetSessionId) {
                if (draggedSessionId.isEmpty() || draggedSessionId == targetSessionId)
                    return;

                MonitorWorkspaceSnapshot snapshot = runtime.workspace.GetSnapshot(runtime.preset);
                runtime.sessionOrder = NormalizeAssignedSessionOrder(snapshot, runtime.sessionOrder);
                runtime.sessionOrder.removeAll(draggedSessionId);
                if (targetSessionId.isEmpty())
                {
                    runtime.sessionOrder.append(draggedSessionId);
                }
                else
                {
                    const int targetIndex = runtime.sessionOrder.indexOf(targetSessionId);
                    if (targetIndex < 0)
                        runtime.sessionOrder.append(draggedSessionId);
                    else
                        runtime.sessionOrder.insert(targetIndex, draggedSessionId);
                }

                SaveSecondaryRuntimeState(runtime);
                RefreshSecondaryRuntime(runtime);
                SaveSettings();
            });
    }

    runtime.workspaceId = workspaceState.workspaceId;
    runtime.workspaceName = workspaceState.workspaceName.trimmed().isEmpty()
        ? workspaceState.workspaceId
        : workspaceState.workspaceName.trimmed();
    runtime.preset = static_cast<MonitorWallLayoutPreset>(std::clamp(workspaceState.layoutPreset, 0, 4));
    runtime.maximizedSessionId = workspaceState.maximizedSessionId.trimmed();
    runtime.groupFilter = workspaceState.groupFilter.trimmed();
    runtime.sessionOrder = workspaceState.sessionOrder;
    runtime.fullscreen = workspaceState.fullscreen;
    runtime.window->SetPresentationMode(true);
    runtime.window->setWindowTitle(QString("Monitor Wall - %1").arg(runtime.workspaceName));

    InvalidatePendingOpens(runtime.pendingOpenRequests);
    CloseSecondaryPopouts(runtime);
    runtime.workspace.CloseAll();
    const MonitorWorkspaceSnapshot existingSnapshot = runtime.workspace.GetSnapshot(runtime.preset);
    for (const MonitorSessionSnapshot& session : existingSnapshot.sessions)
        runtime.workspace.RemoveSession(session.sessionId);
    for (auto it = runtime.tiles.begin(); it != runtime.tiles.end(); ++it)
        delete it.value();
    runtime.tiles.clear();

    for (const MonitorSourceState& state : workspaceState.sources)
    {
        MonitorSourceDescriptor source;
        source.cameraId = state.cameraId;
        source.displayName = state.displayName;
        source.groupName = state.groupName;
        source.sourceUrl = state.sourceUrl;
        source.preferLowLatency = state.preferLowLatency;
        source.enableDetector = state.enableDetector;
        source.enableAsr = state.enableAsr;
        source.enableRecording = state.enableRecording;
        const QString sessionId = runtime.workspace.UpsertSource(NormalizeSource(source));
        if (sessionId.isEmpty())
            continue;
        if (state.muted)
            runtime.workspace.SetSessionMuted(sessionId, true);
        EnsureSecondaryTile(runtime, sessionId);
    }

    if (!workspaceState.selectedSessionId.trimmed().isEmpty())
        runtime.workspace.SelectSession(workspaceState.selectedSessionId.trimmed());
    if (!workspaceState.audioSessionId.trimmed().isEmpty())
        runtime.workspace.SetAudioSession(workspaceState.audioSessionId.trimmed());

    if (workspaceState.windowVisible)
        ShowSecondaryRuntime(runtime);
    else if (runtime.window->isVisible())
        runtime.window->hide();
}

void MonitorWallController::ShowSecondaryRuntime(SecondaryWallRuntime& runtime)
{
    if (!runtime.window)
        return;

    const MonitorWorkspaceSnapshot snapshot = runtime.workspace.GetSnapshot(runtime.preset);
    for (const MonitorSessionSnapshot& session : snapshot.sessions)
    {
        EnsureSecondaryTile(runtime, session.sessionId);
        ReopenSecondarySessionAsync(runtime, session.sessionId);
    }

    QScreen* targetScreen = nullptr;
    for (QScreen* screen : QGuiApplication::screens())
    {
        if (screen && screen->name() == runtime.screenName)
        {
            targetScreen = screen;
            break;
        }
    }
    if (!targetScreen)
        targetScreen = QGuiApplication::primaryScreen();
    if (!targetScreen)
        return;

    if (QWindow* handle = runtime.window->windowHandle())
        handle->setScreen(targetScreen);

    const QRect bounds = targetScreen->availableGeometry();
    runtime.window->setGeometry(bounds);
    if (runtime.fullscreen)
        runtime.window->showFullScreen();
    else
        runtime.window->show();
    runtime.window->raise();
    runtime.window->activateWindow();
    RefreshSecondaryRuntime(runtime);
}

void MonitorWallController::RefreshSecondaryWalls()
{
    for (auto it = secondaryRuntimes_.begin(); it != secondaryRuntimes_.end(); ++it)
    {
        if (it.value() && it.value()->window && it.value()->window->isVisible())
            RefreshSecondaryRuntime(*it.value());
    }
}

void MonitorWallController::RefreshSecondaryRuntime(SecondaryWallRuntime& runtime)
{
    if (!runtime.window)
        return;

    MonitorWorkspaceSnapshot snapshot = runtime.workspace.GetSnapshot(runtime.preset);
    snapshot = ApplySessionOrder(std::move(snapshot), runtime.sessionOrder);

    QSet<QString> activeSessionIds;
    const bool visibleWall = runtime.window->isVisible();
    for (const MonitorSessionSnapshot& session : snapshot.sessions)
    {
        MonitorSourceDescriptor source;
        if (!runtime.workspace.GetSessionSource(session.sessionId, &source))
            continue;

        const bool groupMatch = SessionMatchesGroupFilter(runtime.groupFilter, source);
        MonitorAiSessionPolicy policy;
        policy.focusRoute = session.sessionId == snapshot.selectedSessionId;
        policy.fullscreenRoute = runtime.maximizedSessionId == session.sessionId
            || runtime.popouts.contains(session.sessionId);
        policy.priorityTier = policy.fullscreenRoute
            ? AiPriorityTier::Fullscreen
            : (policy.focusRoute ? AiPriorityTier::Focused : AiPriorityTier::Background);
        const bool tileVisible = (visibleWall && groupMatch) || runtime.popouts.contains(session.sessionId);
        policy.shouldRunAsr = tileVisible && source.enableAsr && (policy.focusRoute || policy.fullscreenRoute);
        policy.detectorMinimumSkipFrames = !groupMatch
            ? 32
            : (source.enableDetector ? (policy.fullscreenRoute ? 0 : (policy.focusRoute ? 4 : 12)) : 0);
        runtime.workspace.ApplySessionAiPolicy(session.sessionId, policy, nullptr);

        if (!groupMatch)
            continue;

        activeSessionIds.insert(session.sessionId);
        EnsureSecondaryTile(runtime, session.sessionId);
        if (MonitorTileWidget* tile = runtime.tiles.value(session.sessionId, nullptr))
        {
            VideoWidget* surface = tile->VideoSurface();
            if (surface)
            {
                surface->setPreferLiveRendering(false);
                surface->setUpdatesEnabled(tileVisible);
                if (tileVisible)
                    surface->update();
            }
            tile->SetSnapshot(session);
        }
    }

    for (auto it = runtime.tiles.begin(); it != runtime.tiles.end(); )
    {
        if (!activeSessionIds.contains(it.key()))
        {
            delete it.value();
            it = runtime.tiles.erase(it);
        }
        else
        {
            ++it;
        }
    }

    MonitorWorkspaceSnapshot filtered = snapshot;
    QVector<MonitorSessionSnapshot> sessions;
    sessions.reserve(filtered.sessions.size());
    for (const MonitorSessionSnapshot& session : filtered.sessions)
    {
        MonitorSourceDescriptor source;
        if (!runtime.workspace.GetSessionSource(session.sessionId, &source)
            || !SessionMatchesGroupFilter(runtime.groupFilter, source))
        {
            continue;
        }
        if (runtime.popouts.contains(session.sessionId))
            continue;
        sessions.push_back(session);
    }
    filtered.sessions = sessions;

    if (!runtime.maximizedSessionId.trimmed().isEmpty())
    {
        MonitorWorkspaceSnapshot maximized = filtered;
        maximized.sessions.clear();
        maximized.layout.placements.clear();
        maximized.layout.preset = MonitorWallLayoutPreset::Single;
        maximized.layout.rows = 1;
        maximized.layout.columns = 1;
        for (const MonitorSessionSnapshot& session : filtered.sessions)
        {
            if (session.sessionId != runtime.maximizedSessionId)
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
            filtered = maximized;
    }

    if (filtered.layout.placements.isEmpty() || filtered.layout.placements.size() != filtered.sessions.size())
    {
        filtered.layout.placements.clear();
        const int columnCount = std::max(1, filtered.layout.columns);
        for (int index = 0; index < filtered.sessions.size(); ++index)
        {
            const MonitorSessionSnapshot& session = filtered.sessions.at(index);
            MonitorTilePlacement placement;
            placement.sessionId = session.sessionId;
            placement.row = index / columnCount;
            placement.column = index % columnCount;
            placement.selected = session.selected;
            placement.audioOwner = session.audioOwner;
            filtered.layout.placements.push_back(placement);
        }
    }

    runtime.window->SetWindowFullscreen(runtime.window->isFullScreen());
    runtime.window->ApplyWorkspaceSnapshot(filtered, runtime.tiles);
}

void MonitorWallController::SaveSecondaryRuntimeState(SecondaryWallRuntime& runtime)
{
    WorkspaceState* workspaceState = FindWorkspaceState(runtime.workspaceId);
    if (!workspaceState)
        return;

    MonitorWorkspaceSnapshot snapshot = runtime.workspace.GetSnapshot(runtime.preset);
    snapshot = ApplySessionOrder(std::move(snapshot), runtime.sessionOrder);

    workspaceState->layoutPreset = static_cast<int>(runtime.preset);
    workspaceState->selectedSessionId = snapshot.selectedSessionId;
    workspaceState->audioSessionId = snapshot.audioSessionId;
    workspaceState->maximizedSessionId = runtime.maximizedSessionId;
    workspaceState->groupFilter = runtime.groupFilter;
    workspaceState->sessionOrder = NormalizeAssignedSessionOrder(snapshot, runtime.sessionOrder);
    workspaceState->windowVisible = runtime.window && runtime.window->isVisible();
    workspaceState->fullscreen = runtime.window && runtime.window->isFullScreen();
    workspaceState->sources.clear();

    for (const MonitorSessionSnapshot& session : snapshot.sessions)
    {
        MonitorSourceDescriptor source;
        if (!runtime.workspace.GetSessionSource(session.sessionId, &source))
            continue;

        MonitorSourceState state;
        state.cameraId = source.cameraId;
        state.displayName = source.displayName;
        state.groupName = source.groupName;
        state.sourceUrl = source.sourceUrl;
        state.preferLowLatency = source.preferLowLatency;
        state.enableDetector = source.enableDetector;
        state.enableAsr = source.enableAsr;
        state.enableRecording = source.enableRecording;
        state.muted = session.muted;
        workspaceState->sources.append(state);
    }
}

void MonitorWallController::EnsureSecondaryTile(SecondaryWallRuntime& runtime, const QString& sessionId)
{
    if (runtime.tiles.contains(sessionId))
        return;

    auto* tile = new MonitorTileWidget(sessionId, runtime.window.get());
    runtime.tiles.insert(sessionId, tile);
    runtime.workspace.BindSessionDetectorResultHandler(sessionId, tile, [tile](DetectionResult result) {
        tile->ApplyDetections(result);
    });
    QObject::connect(tile, &MonitorTileWidget::Activated, runtime.window.get(),
        [this, &runtime](const QString& id) {
            runtime.workspace.SelectSession(id);
            runtime.workspace.SetAudioSession(id);
            SaveSecondaryRuntimeState(runtime);
            RefreshSecondaryRuntime(runtime);
            SaveSettings();
        });
    QObject::connect(tile, &MonitorTileWidget::MaximizeRequested, runtime.window.get(),
        [this, &runtime](const QString& id) {
            runtime.maximizedSessionId = (runtime.maximizedSessionId == id) ? QString{} : id;
            SaveSecondaryRuntimeState(runtime);
            RefreshSecondaryRuntime(runtime);
            SaveSettings();
        });
    QObject::connect(tile, &MonitorTileWidget::ToggleDetectorRequested, runtime.window.get(),
        [this, &runtime](const QString& id, bool enabled) {
            runtime.workspace.SetSessionDetectorEnabled(id, enabled, nullptr);
            SaveSecondaryRuntimeState(runtime);
            RefreshSecondaryRuntime(runtime);
            SaveSettings();
        });
    QObject::connect(tile, &MonitorTileWidget::ToggleAsrRequested, runtime.window.get(),
        [this, &runtime](const QString& id, bool enabled) {
            runtime.workspace.SetSessionAsrEnabled(id, enabled, nullptr);
            SaveSecondaryRuntimeState(runtime);
            RefreshSecondaryRuntime(runtime);
            SaveSettings();
        });
    QObject::connect(tile, &MonitorTileWidget::ToggleRecordingRequested, runtime.window.get(),
        [this, &runtime](const QString& id, bool enabled) {
            WorkspaceState* workspaceState = FindWorkspaceState(runtime.workspaceId);
            RecordingConfiguration configuration = BuildMonitorRecordingConfiguration();
            configuration.archiveRootDir = workspaceState ? workspaceState->archiveRootDir.trimmed() : ArchiveRootDir();
            runtime.workspace.SetSessionRecordingEnabled(id, enabled, configuration, nullptr);
            SaveSecondaryRuntimeState(runtime);
            RefreshSecondaryRuntime(runtime);
            SaveSettings();
        });
    QObject::connect(tile, &MonitorTileWidget::ToggleMuteRequested, runtime.window.get(),
        [this, &runtime](const QString& id, bool muted) {
            runtime.workspace.SetSessionMuted(id, muted);
            if (!muted)
                runtime.workspace.SetAudioSession(id);
            SaveSecondaryRuntimeState(runtime);
            RefreshSecondaryRuntime(runtime);
            SaveSettings();
        });
    QObject::connect(tile, &MonitorTileWidget::ReopenRequested, runtime.window.get(),
        [this, &runtime](const QString& id) {
            ReopenSecondarySessionAsync(runtime, id);
            RefreshSecondaryRuntime(runtime);
        });
    QObject::connect(tile, &MonitorTileWidget::SnapshotRequested, runtime.window.get(),
        [this, &runtime](const QString& id) {
            TakeTileSnapshot(runtime.tiles.value(id, nullptr), QString("%1_%2").arg(runtime.workspaceId, id));
        });
    QObject::connect(tile, &MonitorTileWidget::PopoutRequested, runtime.window.get(),
        [this, &runtime](const QString& id) {
            if (MonitorTilePopoutWindow* existing = runtime.popouts.value(id, nullptr))
            {
                existing->close();
                return;
            }

            MonitorTileWidget* tileWidget = runtime.tiles.value(id, nullptr);
            if (!tileWidget)
                return;

            auto* popout = new MonitorTilePopoutWindow(id);
            runtime.popouts.insert(id, popout);
            QObject::connect(popout, &MonitorTilePopoutWindow::Closing, runtime.window.get(),
                [this, &runtime](const QString& sessionId) {
                    ReturnPoppedTileToSecondaryWall(runtime, sessionId);
                    if (MonitorTilePopoutWindow* window = runtime.popouts.take(sessionId))
                        window->deleteLater();
                    RefreshSecondaryRuntime(runtime);
                    SaveSecondaryRuntimeState(runtime);
                    SaveSettings();
                });
            tileWidget->setParent(popout);
            popout->SetTile(tileWidget);
            popout->show();
            popout->raise();
            popout->activateWindow();
            RefreshSecondaryRuntime(runtime);
        });
    QObject::connect(tile, &MonitorTileWidget::RemoveRequested, runtime.window.get(),
        [this, &runtime](const QString& id) {
            InvalidatePendingOpen(runtime.pendingOpenRequests, id);
            runtime.workspace.RemoveSession(id);
            RemoveSecondaryTile(runtime, id);
            SaveSecondaryRuntimeState(runtime);
            RefreshSecondaryRuntime(runtime);
            SaveSettings();
        });
}

void MonitorWallController::RemoveSecondaryTile(SecondaryWallRuntime& runtime, const QString& sessionId)
{
    if (MonitorTilePopoutWindow* popout = runtime.popouts.take(sessionId))
    {
        ReturnPoppedTileToSecondaryWall(runtime, sessionId);
        popout->blockSignals(true);
        popout->close();
        popout->deleteLater();
    }

    auto it = runtime.tiles.find(sessionId);
    if (it == runtime.tiles.end())
        return;

    delete it.value();
    runtime.tiles.erase(it);
}

void MonitorWallController::CloseSecondaryPopouts(SecondaryWallRuntime& runtime)
{
    const QStringList popoutIds = runtime.popouts.keys();
    for (const QString& sessionId : popoutIds)
    {
        ReturnPoppedTileToSecondaryWall(runtime, sessionId);
        if (MonitorTilePopoutWindow* popout = runtime.popouts.take(sessionId))
        {
            popout->blockSignals(true);
            popout->close();
            popout->deleteLater();
        }
    }
}

void MonitorWallController::ReturnPoppedTileToSecondaryWall(SecondaryWallRuntime& runtime, const QString& sessionId)
{
    MonitorTileWidget* tile = runtime.tiles.value(sessionId, nullptr);
    if (!tile || tile->parentWidget() == runtime.window.get())
        return;

    if (VideoWidget* surface = tile->VideoSurface())
        surface->PrepareForWindowTransfer();
    tile->setParent(runtime.window.get());
    tile->show();
}
