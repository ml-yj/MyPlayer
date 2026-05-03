

#include "monitor_wall_controller.h"

#include "../../view/qt_ui_theme.h"
#include "../../../ui/monitor/monitor_tile_widget.h"
#include "../../../ui/monitor/monitor_wall_window.h"

#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QGuiApplication>
#include <QMessageBox>
#include <QObject>
#include <QScreen>
#include <QSet>
#include <QStandardPaths>
#include <QWindow>

#include <algorithm>

void MonitorWallController::SaveCurrentWorkspaceState()
{
    if (currentWorkspaceId_.trimmed().isEmpty())
        return;

    WorkspaceState* workspaceState = FindWorkspaceState(currentWorkspaceId_);
    if (!workspaceState)
        return;

    MonitorWorkspaceSnapshot snapshot = workspace_.GetSnapshot(currentPreset_);
    snapshot = ApplySessionOrder(std::move(snapshot));

    workspaceState->workspaceId = currentWorkspaceId_;
    if (workspaceState->workspaceName.trimmed().isEmpty())
        workspaceState->workspaceName = CreateWorkspaceName();
    workspaceState->layoutPreset = static_cast<int>(currentPreset_);
    workspaceState->selectedSessionId = snapshot.selectedSessionId;
    workspaceState->audioSessionId = snapshot.audioSessionId;
    workspaceState->archiveRootDir = ArchiveRootDir();
    workspaceState->maximizedSessionId = maximizedSessionId_;
    workspaceState->assignedScreen = currentScreenName_;
    workspaceState->sessionOrder = NormalizeAssignedSessionOrder(snapshot, sessionOrder_);
    workspaceState->groupFilter = currentGroupFilter_;
    workspaceState->activeFavoriteLayoutId = currentFavoriteLayoutId_;
    workspaceState->windowVisible = window_ && window_->isVisible();
    workspaceState->fullscreen = window_ && window_->isFullScreen();
    workspaceState->sources.clear();

    for (const MonitorSessionSnapshot& session : snapshot.sessions)
    {
        MonitorSourceDescriptor source;
        if (!workspace_.GetSessionSource(session.sessionId, &source))
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

void MonitorWallController::RefreshWorkspaceSelectors()
{
    if (!window_)
        return;

    if (currentWorkspaceId_.trimmed().isEmpty() && !workspaceStates_.isEmpty())
        currentWorkspaceId_ = workspaceStates_.first().workspaceId;

    currentScreenName_ = ResolveCurrentScreenName();

}

MonitorWallController::WorkspaceState* MonitorWallController::FindWorkspaceState(const QString& workspaceId)
{
    for (WorkspaceState& workspace : workspaceStates_)
    {
        if (workspace.workspaceId == workspaceId)
            return &workspace;
    }
    return nullptr;
}

const MonitorWallController::WorkspaceState* MonitorWallController::FindWorkspaceState(const QString& workspaceId) const
{
    for (const WorkspaceState& workspace : workspaceStates_)
    {
        if (workspace.workspaceId == workspaceId)
            return &workspace;
    }
    return nullptr;
}

MonitorWallController::WorkspaceState::FavoriteLayoutState* MonitorWallController::FindFavoriteLayout(
    WorkspaceState& workspace,
    const QString& layoutId)
{
    for (WorkspaceState::FavoriteLayoutState& layout : workspace.favoriteLayouts)
    {
        if (layout.layoutId == layoutId)
            return &layout;
    }
    return nullptr;
}

const MonitorWallController::WorkspaceState::FavoriteLayoutState* MonitorWallController::FindFavoriteLayout(
    const WorkspaceState& workspace,
    const QString& layoutId) const
{
    for (const WorkspaceState::FavoriteLayoutState& layout : workspace.favoriteLayouts)
    {
        if (layout.layoutId == layoutId)
            return &layout;
    }
    return nullptr;
}

QString MonitorWallController::CreateWorkspaceName() const
{
    int index = 1;
    while (true)
    {
        const QString candidate = QString("Workspace %1").arg(index++);
        bool used = false;
        for (const WorkspaceState& workspace : workspaceStates_)
        {
            if (workspace.workspaceName.compare(candidate, Qt::CaseInsensitive) == 0)
            {
                used = true;
                break;
            }
        }
        if (!used)
            return candidate;
    }
}

QString MonitorWallController::CreateFavoriteLayoutName(const WorkspaceState& workspace) const
{
    int index = 1;
    while (true)
    {
        const QString candidate = QString("Layout %1").arg(index++);
        bool used = false;
        for (const WorkspaceState::FavoriteLayoutState& layout : workspace.favoriteLayouts)
        {
            if (layout.layoutName.compare(candidate, Qt::CaseInsensitive) == 0)
            {
                used = true;
                break;
            }
        }
        if (!used)
            return candidate;
    }
}

QList<QPair<QString, QString>> MonitorWallController::BuildWorkspaceItems() const
{
    QList<QPair<QString, QString>> items;
    for (const WorkspaceState& workspace : workspaceStates_)
    {
        const QString label = workspace.workspaceName.trimmed().isEmpty()
            ? workspace.workspaceId
            : workspace.workspaceName.trimmed();
        items.append(qMakePair(workspace.workspaceId, label));
    }
    return items;
}

QList<QPair<QString, QString>> MonitorWallController::BuildFavoriteLayoutItems() const
{
    QList<QPair<QString, QString>> items;
    const WorkspaceState* workspaceState = FindWorkspaceState(currentWorkspaceId_);
    if (!workspaceState)
        return items;

    for (const WorkspaceState::FavoriteLayoutState& layout : workspaceState->favoriteLayouts)
    {
        const QString label = layout.layoutName.trimmed().isEmpty()
            ? layout.layoutId
            : layout.layoutName.trimmed();
        items.append(qMakePair(layout.layoutId, label));
    }
    return items;
}

QList<QPair<QString, QString>> MonitorWallController::BuildGroupFilterItems(const MonitorWorkspaceSnapshot& snapshot) const
{
    QList<QPair<QString, QString>> items;
    items.append(qMakePair(QString{}, QString("All Groups")));

    QStringList groups;
    for (const MonitorSessionSnapshot& session : snapshot.sessions)
    {
        MonitorSourceDescriptor source;
        if (!workspace_.GetSessionSource(session.sessionId, &source))
            continue;
        const QString groupName = source.groupName.trimmed();
        if (!groupName.isEmpty() && !groups.contains(groupName))
            groups.append(groupName);
    }
    std::sort(groups.begin(), groups.end(), [](const QString& lhs, const QString& rhs) {
        return lhs.compare(rhs, Qt::CaseInsensitive) < 0;
    });
    for (const QString& group : groups)
        items.append(qMakePair(group, group));
    return items;
}

QList<QPair<QString, QString>> MonitorWallController::BuildScreenItems() const
{
    QList<QPair<QString, QString>> items;
    items.append(qMakePair(QString{}, QString("Auto")));
    const QList<QScreen*> screens = QGuiApplication::screens();
    for (QScreen* screen : screens)
    {
        if (!screen)
            continue;
        const QRect geometry = screen->availableGeometry();
        const QString label = QString("%1 (%2x%3)")
            .arg(screen->name())
            .arg(geometry.width())
            .arg(geometry.height());
        items.append(qMakePair(screen->name(), label));
    }
    return items;
}

QString MonitorWallController::ResolveCurrentScreenName() const
{
    if (currentScreenName_.trimmed().isEmpty())
        return {};

    const QList<QScreen*> screens = QGuiApplication::screens();
    for (QScreen* screen : screens)
    {
        if (screen && screen->name() == currentScreenName_.trimmed())
            return currentScreenName_.trimmed();
    }

    return {};
}

QString MonitorWallController::ResolveSecondaryScreenName(
    const WorkspaceState& workspaceState,
    const QSet<QString>& occupiedScreens) const
{
    const QString requestedScreen = workspaceState.assignedScreen.trimmed();
    const QList<QScreen*> screens = QGuiApplication::screens();
    if (!requestedScreen.isEmpty())
    {
        for (QScreen* screen : screens)
        {
            if (screen && screen->name() == requestedScreen && !occupiedScreens.contains(requestedScreen))
                return requestedScreen;
        }
        return {};
    }

    for (QScreen* screen : screens)
    {
        if (screen && !occupiedScreens.contains(screen->name()))
            return screen->name();
    }

    return {};
}

void MonitorWallController::MoveWindowToAssignedScreen()
{
    if (!window_)
        return;

    QScreen* targetScreen = nullptr;
    const QString assignedScreen = ResolveCurrentScreenName();
    for (QScreen* screen : QGuiApplication::screens())
    {
        if (screen && screen->name() == assignedScreen)
        {
            targetScreen = screen;
            break;
        }
    }
    if (!targetScreen && window_->screen())
        targetScreen = window_->screen();
    if (!targetScreen)
        targetScreen = QGuiApplication::primaryScreen();
    if (!targetScreen)
        return;

    if (QWindow* handle = window_->windowHandle())
        handle->setScreen(targetScreen);

    const QRect bounds = targetScreen->availableGeometry();
    if (window_->isFullScreen())
    {
        window_->setGeometry(bounds);
        return;
    }

    QSize size = window_->size();
    size.setWidth(std::min(size.width(), bounds.width()));
    size.setHeight(std::min(size.height(), bounds.height()));
    const QPoint topLeft(
        bounds.left() + std::max(0, (bounds.width() - size.width()) / 2),
        bounds.top() + std::max(0, (bounds.height() - size.height()) / 2));
    window_->resize(size);
    window_->move(topLeft);
}

QStringList MonitorWallController::NormalizeSessionOrder(const MonitorWorkspaceSnapshot& snapshot) const
{
    return NormalizeSessionOrder(snapshot, sessionOrder_);
}

QStringList MonitorWallController::NormalizeAssignedSessionOrder(
    const MonitorWorkspaceSnapshot& snapshot,
    const QStringList& preferredOrder)
{
    QStringList normalized;
    QSet<QString> seen;
    const int capacity = std::max(1, static_cast<int>(snapshot.layout.rows * snapshot.layout.columns));

    for (int index = 0; index < preferredOrder.size() && normalized.size() < capacity; ++index)
    {
        const QString sessionId = preferredOrder.at(index).trimmed();
        if (sessionId.isEmpty())
        {
            normalized.append(QString{});
            continue;
        }

        if (seen.contains(sessionId))
        {
            normalized.append(QString{});
            continue;
        }

        bool matched = false;
        for (const MonitorSessionSnapshot& session : snapshot.sessions)
        {
            if (session.sessionId != sessionId)
                continue;
            normalized.append(sessionId);
            seen.insert(sessionId);
            matched = true;
            break;
        }
        if (!matched)
            normalized.append(QString{});
    }

    while (normalized.size() < capacity)
        normalized.append(QString{});

    return normalized;
}

QStringList MonitorWallController::NormalizeSessionOrder(
    const MonitorWorkspaceSnapshot& snapshot,
    const QStringList& preferredOrder)
{
    QStringList normalized;
    QSet<QString> seen;

    for (const QString& sessionId : preferredOrder)
    {
        if (sessionId.trimmed().isEmpty() || seen.contains(sessionId))
            continue;

        for (const MonitorSessionSnapshot& session : snapshot.sessions)
        {
            if (session.sessionId != sessionId)
                continue;
            normalized.append(sessionId);
            seen.insert(sessionId);
            break;
        }
    }

    for (const MonitorSessionSnapshot& session : snapshot.sessions)
    {
        if (seen.contains(session.sessionId))
            continue;
        normalized.append(session.sessionId);
        seen.insert(session.sessionId);
    }

    return normalized;
}

MonitorWorkspaceSnapshot MonitorWallController::ApplySessionOrder(MonitorWorkspaceSnapshot snapshot) const
{
    return ApplySessionOrder(std::move(snapshot), sessionOrder_);
}

MonitorWorkspaceSnapshot MonitorWallController::ApplySessionOrder(
    MonitorWorkspaceSnapshot snapshot,
    const QStringList& preferredOrder)
{
    const QStringList orderedIds = NormalizeSessionOrder(snapshot, preferredOrder);
    if (orderedIds.isEmpty())
        return snapshot;

    QHash<QString, MonitorSessionSnapshot> sessionsById;
    for (const MonitorSessionSnapshot& session : snapshot.sessions)
        sessionsById.insert(session.sessionId, session);

    QVector<MonitorSessionSnapshot> orderedSessions;
    orderedSessions.reserve(snapshot.sessions.size());
    for (const QString& sessionId : orderedIds)
    {
        const auto it = sessionsById.constFind(sessionId);
        if (it != sessionsById.constEnd())
            orderedSessions.push_back(it.value());
    }
    snapshot.sessions = orderedSessions;

    snapshot.layout.placements.clear();
    const int columnCount = std::max(1, snapshot.layout.columns);
    for (int index = 0; index < snapshot.sessions.size(); ++index)
    {
        const MonitorSessionSnapshot& session = snapshot.sessions.at(index);
        MonitorTilePlacement placement;
        placement.sessionId = session.sessionId;
        placement.row = index / columnCount;
        placement.column = index % columnCount;
        placement.selected = session.selected;
        placement.audioOwner = session.audioOwner;
        snapshot.layout.placements.push_back(placement);
    }

    return snapshot;
}

QSet<QString> MonitorWallController::VisibleSessionIds(const MonitorWorkspaceSnapshot& snapshot) const
{
    QSet<QString> visibleSessionIds;
    const bool wallVisible = window_ && window_->isVisible();

    for (const MonitorSessionSnapshot& session : snapshot.sessions)
    {
        if (MonitorTilePopoutWindow* popout = popouts_.value(session.sessionId, nullptr))
        {
            if (popout->isVisible())
                visibleSessionIds.insert(session.sessionId);
            continue;
        }

        MonitorSourceDescriptor source;
        if (!workspace_.GetSessionSource(session.sessionId, &source))
            continue;
        if (!SessionMatchesCurrentGroupFilter(source))
            continue;

        if (!wallVisible)
            continue;

        MonitorTileWidget* tile = tiles_.value(session.sessionId, nullptr);
        if (!tile || !tile->isVisible() || tile->visibleRegion().isEmpty())
            continue;
        visibleSessionIds.insert(session.sessionId);
    }

    return visibleSessionIds;
}

int MonitorWallController::WallCapacity() const
{
    switch (currentPreset_)
    {
    case MonitorWallLayoutPreset::Single:  return 1;
    case MonitorWallLayoutPreset::Grid2x2: return 4;
    case MonitorWallLayoutPreset::Grid3x3: return 9;
    case MonitorWallLayoutPreset::Grid4x4: return 16;
    case MonitorWallLayoutPreset::Custom:  return 16;
    }
    return 4;
}

bool MonitorWallController::EnsureSessionVisibleOnWall(const QString& sessionId)
{
    const QString normalizedId = sessionId.trimmed();
    if (normalizedId.isEmpty())
        return false;

    return AssignSessionToWallSlot(normalizedId, activeWallSlotIndex_);
}

void MonitorWallController::RemoveSessionFromWall(const QString& sessionId)
{
    const QString normalizedId = sessionId.trimmed();
    if (normalizedId.isEmpty())
        return;

    for (QString& assignedId : sessionOrder_)
    {
        if (assignedId.trimmed() == normalizedId)
            assignedId.clear();
    }
    currentFavoriteLayoutId_.clear();
}

bool MonitorWallController::AssignSessionToWallSlot(const QString& sessionId, int preferredSlotIndex)
{
    const QString normalizedId = sessionId.trimmed();
    if (normalizedId.isEmpty())
        return false;

    const MonitorWorkspaceSnapshot snapshot = workspace_.GetSnapshot(currentPreset_);
    const int capacity = std::max(1, WallCapacity());
    QStringList slotOrder = NormalizeAssignedSessionOrder(snapshot, sessionOrder_);
    while (slotOrder.size() < capacity)
        slotOrder.append(QString{});

    const int existingIndex = slotOrder.indexOf(normalizedId);
    int targetIndex = preferredSlotIndex;
    if (targetIndex < 0 || targetIndex >= capacity)
    {
        targetIndex = existingIndex;
        if (targetIndex < 0)
        {
            for (int index = 0; index < slotOrder.size(); ++index)
            {
                if (slotOrder.at(index).trimmed().isEmpty())
                {
                    targetIndex = index;
                    break;
                }
            }
        }
        if (targetIndex < 0)
            targetIndex = 0;
    }

    if (existingIndex >= 0 && existingIndex != targetIndex)
        slotOrder[existingIndex].clear();

    const QString displacedId = slotOrder.value(targetIndex).trimmed();
    if (!displacedId.isEmpty() && displacedId != normalizedId)
        workspace_.CloseSession(displacedId);

    slotOrder[targetIndex] = normalizedId;
    for (int index = 0; index < slotOrder.size(); ++index)
    {
        if (index != targetIndex && slotOrder.at(index).trimmed() == normalizedId)
            slotOrder[index].clear();
    }

    sessionOrder_ = slotOrder;
    activeWallSlotIndex_ = targetIndex;
    currentFavoriteLayoutId_.clear();
    return true;
}

int MonitorWallController::FindAssignedSlotIndex(const QString& sessionId) const
{
    const QString normalizedId = sessionId.trimmed();
    if (normalizedId.isEmpty())
        return -1;

    const QStringList slotOrder = NormalizeAssignedSessionOrder(workspace_.GetSnapshot(currentPreset_), sessionOrder_);
    for (int index = 0; index < slotOrder.size(); ++index)
    {
        if (slotOrder.at(index).trimmed() == normalizedId)
            return index;
    }
    return -1;
}

bool MonitorWallController::NormalizeAudioRouteForVisibleSessions(
    const MonitorWorkspaceSnapshot& snapshot,
    const QSet<QString>& visibleSessionIds)
{
    if (visibleSessionIds.isEmpty())
        return false;
    if (!snapshot.audioSessionId.isEmpty() && visibleSessionIds.contains(snapshot.audioSessionId))
        return false;

    QString preferredSessionId;
    if (!snapshot.selectedSessionId.isEmpty() && visibleSessionIds.contains(snapshot.selectedSessionId))
    {
        preferredSessionId = snapshot.selectedSessionId;
    }
    else
    {
        for (const MonitorSessionSnapshot& session : snapshot.sessions)
        {
            if (visibleSessionIds.contains(session.sessionId))
            {
                preferredSessionId = session.sessionId;
                break;
            }
        }
    }

    if (preferredSessionId.isEmpty())
        return false;

    workspace_.SetAudioSession(preferredSessionId);
    return true;
}

void MonitorWallController::OnTileActivated(const QString& sessionId)
{
    workspace_.SelectSession(sessionId);
    workspace_.SetAudioSession(sessionId);
    activeWallSlotIndex_ = FindAssignedSlotIndex(sessionId);
    if (window_)
        window_->SetActiveWallSlot(activeWallSlotIndex_);
    PopulateEditorFromSession(sessionId);
    RefreshUi();
    SaveSettings();
}

void MonitorWallController::OnTileMaximize(const QString& sessionId)
{
    maximizedSessionId_ = (maximizedSessionId_ == sessionId) ? QString{} : sessionId;
    currentFavoriteLayoutId_.clear();
    RefreshUi();
    SaveSettings();
}

void MonitorWallController::OnTileToggleDetector(const QString& sessionId, bool enabled)
{
    std::string error;
    if (!workspace_.SetSessionDetectorEnabled(sessionId, enabled, &error))
    {
        window_->SetStatusText(QString("DET toggle failed: %1").arg(QString::fromStdString(error)));
    }
    else if (window_)
    {
        if (!enabled)
            eventCenter_.ClearSession(sessionId);
        BindDetectorHandlers(sessionId);
        window_->SetStatusText(enabled
            ? QString("DET enabled for %1. Use person/car footage; alarm shows as red border and ALM count.")
                .arg(sessionId)
            : QString("DET disabled for %1. Cleared active detector alarms.").arg(sessionId));
    }
    RefreshUi();
    SaveSettings();
}

void MonitorWallController::OnTileToggleAsr(const QString& sessionId, bool enabled)
{
    if (enabled && !sessionId.trimmed().isEmpty())
    {
        workspace_.SelectSession(sessionId);
        workspace_.SetAudioSession(sessionId);
        activeWallSlotIndex_ = FindAssignedSlotIndex(sessionId);
        if (window_)
            window_->SetActiveWallSlot(activeWallSlotIndex_);
        PopulateEditorFromSession(sessionId);
    }

    std::string error;
    bool asrToggleOk = false;
    if (!workspace_.SetSessionAsrEnabled(sessionId, enabled, &error))
    {
        window_->SetStatusText(QString("ASR toggle failed: %1").arg(QString::fromStdString(error)));
    }
    else if (window_)
    {
        asrToggleOk = true;
    }
    RefreshUi();
    if (window_ && asrToggleOk)
    {
        const MonitorWorkspaceSnapshot snapshot = workspace_.GetSnapshot(currentPreset_);
        const auto it = std::find_if(snapshot.sessions.begin(), snapshot.sessions.end(),
            [&sessionId](const MonitorSessionSnapshot& session) { return session.sessionId == sessionId; });
        if (!enabled)
        {
            ClearAsrOutputForSession(sessionId);
            window_->SetStatusText(QString("ASR disabled for %1").arg(sessionId));
        }
        else if (it == snapshot.sessions.end())
        {
            BindAsrHandlers(sessionId);
            window_->SetStatusText(QString("ASR armed for %1").arg(sessionId));
        }
        else if (it->media.audioChannels <= 0)
        {
            BindAsrHandlers(sessionId);
            window_->SetStatusText(QString("ASR waiting for %1: no audio track.").arg(sessionId));
        }
        else if (it->asrEnabled)
        {
            BindAsrHandlers(sessionId);
            window_->SetStatusText(QString("ASR active for %1").arg(sessionId));
        }
        else if (it->asrRequested)
        {
            BindAsrHandlers(sessionId);
            window_->SetStatusText(
                QString("ASR waiting for %1: keep this tile selected/maximized.").arg(sessionId));
        }
        else
        {
            BindAsrHandlers(sessionId);
            window_->SetStatusText(QString("ASR armed for %1").arg(sessionId));
        }
    }
    SaveSettings();
}

void MonitorWallController::OnTileToggleRecording(const QString& sessionId, bool enabled)
{
    if (!sessionId.trimmed().isEmpty())
    {
        workspace_.SelectSession(sessionId);
        workspace_.SetAudioSession(sessionId);
        activeWallSlotIndex_ = FindAssignedSlotIndex(sessionId);
        if (window_)
            window_->SetActiveWallSlot(activeWallSlotIndex_);
        PopulateEditorFromSession(sessionId);
    }
    playbackSelectedDate_ = QDate::currentDate();
    playbackVisibleMonth_ = QDate(playbackSelectedDate_.year(), playbackSelectedDate_.month(), 1);
    if (window_)
        window_->SetSelectedPlaybackDate(playbackSelectedDate_);

    SaveArchivePreferencesFromWindow();
    std::string error;
    if (!workspace_.SetSessionRecordingEnabled(sessionId, enabled, BuildMonitorRecordingConfiguration(), &error))
    {
        window_->SetStatusText(QString("REC toggle failed: %1").arg(QString::fromStdString(error)));
    }
    else if (window_)
    {
        window_->SetStatusText(enabled
            ? QString("Recording started for %1").arg(sessionId)
            : QString("Recording stopped for %1").arg(sessionId));
    }
    RefreshUi();
    SaveSettings();
}

void MonitorWallController::OnTileRecordingMenuRequested(const QString& sessionId, int action)
{
    switch (action)
    {
    case MonitorTileWidget::RecordingMenuDisable:
        OnTileToggleRecording(sessionId, false);
        break;
    case MonitorTileWidget::RecordingMenuSingle:
        OnTileToggleRecording(sessionId, true);
        break;
    default:
        break;
    }
}

void MonitorWallController::OnTileToggleMute(const QString& sessionId, bool muted)
{
    workspace_.SetSessionMuted(sessionId, muted);
    if (!muted)
        workspace_.SetAudioSession(sessionId);
    RefreshUi();
    SaveSettings();
}

void MonitorWallController::OnTileReopen(const QString& sessionId)
{
    EnsureTile(sessionId);
    EnsureSessionVisibleOnWall(sessionId);
    ReopenSessionAsync(sessionId);
    RefreshUi();
}

void MonitorWallController::OnTileSnapshot(const QString& sessionId)
{
    TakeTileSnapshot(tiles_.value(sessionId, nullptr), sessionId);
}

void MonitorWallController::OnTilePopout(const QString& sessionId)
{
    if (MonitorTilePopoutWindow* existing = popouts_.value(sessionId, nullptr))
    {
        existing->close();
        return;
    }

    MonitorTileWidget* tile = tiles_.value(sessionId, nullptr);
    if (!tile)
        return;

    if (maximizedSessionId_ == sessionId)
        maximizedSessionId_.clear();

    auto* popout = new MonitorTilePopoutWindow(sessionId);
    auto* popoutTile = new MonitorTileWidget(sessionId, popout);
    popouts_.insert(sessionId, popout);
    QObject::connect(popout, &MonitorTilePopoutWindow::Closing, window_.get(),
        [this](const QString& id) { OnTilePopoutClosed(id); });
    QObject::connect(popoutTile, &MonitorTileWidget::Activated, window_.get(), [this](const QString& id) {
        OnTileActivated(id);
    });
    QObject::connect(popoutTile, &MonitorTileWidget::MaximizeRequested, window_.get(), [this](const QString& id) {
        OnTileMaximize(id);
    });
    QObject::connect(popoutTile, &MonitorTileWidget::ToggleDetectorRequested, window_.get(),
        [this](const QString& id, bool enabled) { OnTileToggleDetector(id, enabled); });
    QObject::connect(popoutTile, &MonitorTileWidget::ToggleAsrRequested, window_.get(),
        [this](const QString& id, bool enabled) { OnTileToggleAsr(id, enabled); });
    QObject::connect(popoutTile, &MonitorTileWidget::ToggleRecordingRequested, window_.get(),
        [this](const QString& id, bool enabled) { OnTileToggleRecording(id, enabled); });
    QObject::connect(popoutTile, &MonitorTileWidget::RecordingMenuRequested, window_.get(),
        [this](const QString& id, int action) { OnTileRecordingMenuRequested(id, action); });
    QObject::connect(popoutTile, &MonitorTileWidget::ToggleMuteRequested, window_.get(),
        [this](const QString& id, bool muted) { OnTileToggleMute(id, muted); });
    QObject::connect(popoutTile, &MonitorTileWidget::ReopenRequested, window_.get(),
        [this](const QString& id) { OnTileReopen(id); });
    QObject::connect(popoutTile, &MonitorTileWidget::SnapshotRequested, window_.get(),
        [this](const QString& id) { OnTileSnapshot(id); });
    QObject::connect(popoutTile, &MonitorTileWidget::PopoutRequested, window_.get(),
        [this](const QString& id) { OnTilePopout(id); });
    QObject::connect(popoutTile, &MonitorTileWidget::RemoveRequested, window_.get(),
        [this](const QString& id) { OnTileRemove(id); });
    popout->SetTile(popoutTile);
    BindDetectorHandlerForTile(sessionId, popoutTile, false);
    BindAsrHandlerForTile(sessionId, popoutTile);
    tile->hide();
    if (window_)
        window_->InvalidateWallLayout();
    popout->show();
    popout->raise();
    popout->activateWindow();
    ReopenSessionAsyncToTile(sessionId, popoutTile, QString("Opened %1 in popout").arg(sessionId));
    RefreshUi();
}

void MonitorWallController::OnTilePopoutClosed(const QString& sessionId)
{
    if (MonitorTileWidget* tile = tiles_.value(sessionId, nullptr))
        tile->show();
    if (MonitorTilePopoutWindow* popout = popouts_.take(sessionId))
        popout->deleteLater();
    if (window_)
        window_->InvalidateWallLayout();
    ReopenSessionAsync(sessionId, QString("Returned %1 to wall").arg(sessionId));
    RefreshUi();
}

void MonitorWallController::OnTileRemove(const QString& sessionId)
{
    const auto answer = QtUiTheme::AskQuestion(
        window_.get(),
        "Remove From Wall",
        QString("Remove %1 from the wall layout?").arg(sessionId));
    if (answer != QMessageBox::Yes)
        return;

    RemoveSessionFromWall(sessionId);
    workspace_.CloseSession(sessionId);
    currentFavoriteLayoutId_.clear();
    RefreshUi();
    SaveSettings();
}

void MonitorWallController::OnTileReordered(const QString& draggedSessionId, const QString& targetSessionId)
{
    if (draggedSessionId.isEmpty() || draggedSessionId == targetSessionId)
        return;

    sessionOrder_ = NormalizeAssignedSessionOrder(workspace_.GetSnapshot(currentPreset_), sessionOrder_);
    const int draggedIndex = sessionOrder_.indexOf(draggedSessionId);
    if (draggedIndex < 0)
        return;

    int targetIndex = -1;
    if (!targetSessionId.trimmed().isEmpty())
        targetIndex = sessionOrder_.indexOf(targetSessionId);
    if (targetIndex < 0)
    {
        for (int index = 0; index < sessionOrder_.size(); ++index)
        {
            if (sessionOrder_.at(index).trimmed().isEmpty())
            {
                targetIndex = index;
                break;
            }
        }
    }
    if (targetIndex < 0 || targetIndex == draggedIndex)
        return;

    if (!targetSessionId.trimmed().isEmpty() && targetIndex >= 0)
    {
        std::swap(sessionOrder_[draggedIndex], sessionOrder_[targetIndex]);
    }
    else
    {
        sessionOrder_[targetIndex] = draggedSessionId;
        sessionOrder_[draggedIndex].clear();
    }

    activeWallSlotIndex_ = targetIndex;
    currentFavoriteLayoutId_.clear();
    RefreshUi();
    SaveSettings();
}

void MonitorWallController::OnWallSlotActivated(int slotIndex)
{
    const int capacity = std::max(1, WallCapacity());
    if (slotIndex < 0 || slotIndex >= capacity)
        return;

    activeWallSlotIndex_ = slotIndex;
    if (window_)
        window_->SetActiveWallSlot(activeWallSlotIndex_);

    const MonitorWorkspaceSnapshot snapshot = workspace_.GetSnapshot(currentPreset_);
    const QStringList slotOrder = NormalizeAssignedSessionOrder(snapshot, sessionOrder_);
    if (slotIndex < slotOrder.size())
    {
        const QString sessionId = slotOrder.at(slotIndex).trimmed();
        if (!sessionId.isEmpty())
        {
            workspace_.SelectSession(sessionId);
            workspace_.SetAudioSession(sessionId);
            PopulateEditorFromSession(sessionId);
            RefreshUi();
            SaveSettings();
            return;
        }
    }

    const QString selectedCameraId = window_
        ? window_->SelectedCameraId().trimmed()
        : QString{};
    const QString targetSessionId = !selectedCameraId.isEmpty()
        ? selectedCameraId
        : snapshot.selectedSessionId.trimmed();
    if (!targetSessionId.isEmpty())
    {
        if (AssignSessionToWallSlot(targetSessionId, slotIndex))
        {
            EnsureTile(targetSessionId);
            workspace_.SelectSession(targetSessionId);
            workspace_.SetAudioSession(targetSessionId);
            PopulateEditorFromSession(targetSessionId);
            ReopenSessionAsync(targetSessionId);
            if (window_)
            {
                window_->SetStatusText(QString("Opened %1 in wall slot %2")
                    .arg(targetSessionId)
                    .arg(slotIndex + 1));
            }
            RefreshUi();
            SaveSettings();
            return;
        }
    }

    if (window_)
        window_->SetStatusText(QString("Selected wall slot %1. Choose a camera on the left first.")
            .arg(slotIndex + 1));
}

void MonitorWallController::EnsureTile(const QString& sessionId)
{
    if (tiles_.contains(sessionId))
        return;

    auto* tile = new MonitorTileWidget(sessionId, window_.get());
    tiles_.insert(sessionId, tile);

    QObject::connect(tile, &MonitorTileWidget::Activated, window_.get(), [this](const QString& id) {
        OnTileActivated(id);
    });
    QObject::connect(tile, &MonitorTileWidget::MaximizeRequested, window_.get(), [this](const QString& id) {
        OnTileMaximize(id);
    });
    QObject::connect(tile, &MonitorTileWidget::ToggleDetectorRequested, window_.get(),
        [this](const QString& id, bool enabled) { OnTileToggleDetector(id, enabled); });
    QObject::connect(tile, &MonitorTileWidget::ToggleAsrRequested, window_.get(),
        [this](const QString& id, bool enabled) { OnTileToggleAsr(id, enabled); });
    QObject::connect(tile, &MonitorTileWidget::ToggleRecordingRequested, window_.get(),
        [this](const QString& id, bool enabled) { OnTileToggleRecording(id, enabled); });
    QObject::connect(tile, &MonitorTileWidget::RecordingMenuRequested, window_.get(),
        [this](const QString& id, int action) { OnTileRecordingMenuRequested(id, action); });
    QObject::connect(tile, &MonitorTileWidget::ToggleMuteRequested, window_.get(),
        [this](const QString& id, bool muted) { OnTileToggleMute(id, muted); });
    QObject::connect(tile, &MonitorTileWidget::ReopenRequested, window_.get(),
        [this](const QString& id) { OnTileReopen(id); });
    QObject::connect(tile, &MonitorTileWidget::SnapshotRequested, window_.get(),
        [this](const QString& id) { OnTileSnapshot(id); });
    QObject::connect(tile, &MonitorTileWidget::PopoutRequested, window_.get(),
        [this](const QString& id) { OnTilePopout(id); });
    QObject::connect(tile, &MonitorTileWidget::RemoveRequested, window_.get(),
        [this](const QString& id) { OnTileRemove(id); });

    BindDetectorHandlerForTile(sessionId, tile, true);
    BindAsrHandlerForTile(sessionId, tile);
}

void MonitorWallController::BindDetectorHandlers(const QString& sessionId)
{
    if (MonitorTileWidget* tile = tiles_.value(sessionId, nullptr))
        BindDetectorHandlerForTile(sessionId, tile, true);
    if (MonitorTilePopoutWindow* popout = popouts_.value(sessionId, nullptr))
    {
        if (MonitorTileWidget* tile = popout->Tile())
            BindDetectorHandlerForTile(sessionId, tile, false);
    }
}

void MonitorWallController::BindDetectorHandlerForTile(const QString& sessionId, MonitorTileWidget* tile, bool recordEvents)
{
    if (!tile)
        return;

    if (recordEvents)
    {
        workspace_.BindSessionDetectorResultHandler(sessionId, tile, [this, sessionId, tile](DetectionResult result) {
            tile->ApplyDetections(result);
            RecordDetectorEvent(sessionId, result);
        });
    }
    else
    {
        workspace_.BindSessionDetectorResultHandler(sessionId, tile, [tile](DetectionResult result) {
            tile->ApplyDetections(result);
        });
    }
}

void MonitorWallController::BindAsrHandlers(const QString& sessionId)
{
    if (MonitorTileWidget* tile = tiles_.value(sessionId, nullptr))
        BindAsrHandlerForTile(sessionId, tile);
    if (MonitorTilePopoutWindow* popout = popouts_.value(sessionId, nullptr))
    {
        if (MonitorTileWidget* tile = popout->Tile())
            BindAsrHandlerForTile(sessionId, tile);
    }
}

void MonitorWallController::BindAsrHandlerForTile(const QString& sessionId, MonitorTileWidget* tile)
{
    if (!tile)
        return;

    workspace_.BindSessionAsrSubtitleHandler(
        sessionId,
        tile,
        [tile](const QString& text, long long startMs, long long endMs, quint64 generation, quint64 serial)
        {
            tile->ApplyAsrSubtitle(text, startMs, endMs, generation, serial);
        });
}

void MonitorWallController::ClearAsrOutputForSession(const QString& sessionId)
{
    if (MonitorTileWidget* tile = tiles_.value(sessionId, nullptr))
        tile->ClearAsrSubtitle();
    if (MonitorTilePopoutWindow* popout = popouts_.value(sessionId, nullptr))
    {
        if (MonitorTileWidget* tile = popout->Tile())
            tile->ClearAsrSubtitle();
    }
}

void MonitorWallController::RemoveTile(const QString& sessionId)
{
    auto it = tiles_.find(sessionId);
    if (it == tiles_.end())
    {
        if (MonitorTilePopoutWindow* popout = popouts_.take(sessionId))
        {
            popout->blockSignals(true);
            popout->close();
            popout->deleteLater();
        }
        eventCenter_.ClearSession(sessionId);
        return;
    }

    if (MonitorTilePopoutWindow* popout = popouts_.take(sessionId))
    {
        ReturnPoppedTileToWall(sessionId);
        popout->blockSignals(true);
        popout->close();
        popout->deleteLater();
    }

    delete it.value();
    tiles_.erase(it);
    eventCenter_.ClearSession(sessionId);
}

void MonitorWallController::TakeTileSnapshot(MonitorTileWidget* tile, const QString& filePrefix)
{
    if (!tile)
        return;

    QString saveDir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    if (saveDir.trimmed().isEmpty())
        saveDir = QDir::currentPath();
    saveDir = QDir(saveDir).filePath("MyPlayer/monitor");
    QDir().mkpath(saveDir);

    const QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss_zzz");
    const QString safePrefix = filePrefix.trimmed().isEmpty() ? QString("monitor") : filePrefix;
    const QString filePath = QDir(saveDir).filePath(QString("%1_%2.png").arg(safePrefix, timestamp));
    const bool saved = tile->grab().save(filePath);
    if (window_)
        window_->SetStatusText(saved ? QString("Snapshot saved: %1").arg(filePath)
                                     : QString("Snapshot failed: %1").arg(filePath));
}
