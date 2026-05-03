

#include "monitor_wall_controller.h"

#include "../../service/config_service.h"
#include "../../view/qt_ui_theme.h"
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

namespace
{

bool SameSource(const MonitorSourceDescriptor& lhs, const MonitorSourceDescriptor& rhs)
{
    return lhs.cameraId == rhs.cameraId
        && lhs.displayName == rhs.displayName
        && lhs.groupName == rhs.groupName
        && lhs.sourceUrl == rhs.sourceUrl
        && lhs.preferLowLatency == rhs.preferLowLatency
        && lhs.enableDetector == rhs.enableDetector
        && lhs.enableAsr == rhs.enableAsr
        && lhs.enableRecording == rhs.enableRecording;
}

struct PendingMonitorOpenResult
{
    std::unique_ptr<Demux> demux;
    StreamOpenOptions effectiveOptions = StreamOpenOptions::DefaultNetwork();
    int openLatencyMs = 0;
    QString openError;
};

PendingMonitorOpenResult PrepareMonitorOpen(const MonitorSourceDescriptor& source)
{
    PendingMonitorOpenResult result;
    const QString trimmedUrl = source.sourceUrl.trimmed();
    if (trimmedUrl.isEmpty())
    {
        result.openError = QStringLiteral("Source URL is empty.");
        return result;
    }

    const QByteArray urlBytes = trimmedUrl.toUtf8();
    const auto openStart = std::chrono::steady_clock::now();
    auto demux = std::make_unique<Demux>();
    const bool ok = demux->Open(urlBytes.constData(), source.openOptions);
    const auto openEnd = std::chrono::steady_clock::now();
    result.openLatencyMs = static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(openEnd - openStart).count());
    if (!ok)
    {
        result.openError = QString::fromStdString(demux->GetLastError());
        if (result.openError.isEmpty())
            result.openError = QStringLiteral("Failed to open source.");
        return result;
    }

    result.effectiveOptions = demux->GetOpenOptions();
    result.demux = std::move(demux);
    return result;
}
}

MonitorWallController::MonitorWallController(
    QWidget* host,
    ConfigService* configService,
    std::function<void(const QString&)> playArchiveSegmentHandler)
    : host_(host)
    , config_(configService)
    , window_(std::make_unique<MonitorWallWindow>())
    , playArchiveSegmentHandler_(std::move(playArchiveSegmentHandler))
{
    const QDate today = QDate::currentDate();
    playbackSelectedDate_ = today;
    playbackVisibleMonth_ = QDate(today.year(), today.month(), 1);

    toggleButton_ = new QPushButton("MON", host_);
    toggleButton_->setFixedSize(68, 36);
    toggleButton_->setCheckable(true);

    const ArchivePreferences archivePreferences = config_ ? config_->LoadArchivePreferences() : ArchivePreferences{};
    ApplyArchivePreferences(archivePreferences);

    QObject::connect(toggleButton_, &QPushButton::clicked, [this]() { ToggleWindow(); });
    QObject::connect(window_.get(), &MonitorWallWindow::WindowVisibilityChanged, [this](bool visible) {
        if (toggleButton_)
            toggleButton_->setChecked(visible);
        if (visible)
            RefreshUi();
        SyncSecondaryWalls();
        SaveSettings();
    });
    QObject::connect(window_.get(), &MonitorWallWindow::AddOrUpdateRequested, [this]() { AddOrUpdateSource(); });
    QObject::connect(window_.get(), &MonitorWallWindow::OpenSelectedRequested, [this]() { OpenSelected(); });
    QObject::connect(window_.get(), &MonitorWallWindow::OpenAllRequested, [this]() { OpenAll(); });
    QObject::connect(window_.get(), &MonitorWallWindow::CloseAllRequested, [this]() { CloseAll(); });
    QObject::connect(window_.get(), &MonitorWallWindow::RemoveSelectedRequested, [this]() { RemoveSelected(); });
    QObject::connect(window_.get(), &MonitorWallWindow::ArchiveRootChanged, [this](const QString&) {
        SaveArchivePreferencesFromWindow();
        SaveSettings();
    });
    QObject::connect(window_.get(), &MonitorWallWindow::BrowseArchiveRootRequested, [this]() { BrowseArchiveRoot(); });
    QObject::connect(window_.get(), &MonitorWallWindow::BrowseSourceFileRequested, [this]() { BrowseSourceFile(); });
    QObject::connect(window_.get(), &MonitorWallWindow::ToggleFullscreenRequested, [this]() { ToggleFullscreen(); });
    QObject::connect(window_.get(), &MonitorWallWindow::RecordingContainerChanged, [this](const QString&) {
        SaveArchivePreferencesFromWindow();
    });
    QObject::connect(window_.get(), &MonitorWallWindow::RecordingSegmentDurationChanged, [this](int) {
        SaveArchivePreferencesFromWindow();
    });
    QObject::connect(window_.get(), &MonitorWallWindow::LayoutPresetChanged, [this](MonitorWallLayoutPreset preset) {
        OnLayoutPresetChanged(preset);
    });

    QObject::connect(window_.get(), &MonitorWallWindow::AcknowledgeSelectedEventRequested,
        [this]() { OnAcknowledgeSelectedEvent(); });
    QObject::connect(window_.get(), &MonitorWallWindow::ClearSelectedEventRequested,
        [this]() { OnClearSelectedEvent(); });
    QObject::connect(window_.get(), &MonitorWallWindow::JumpToSelectedEventRequested,
        [this]() { OnJumpToSelectedEvent(); });
    QObject::connect(window_.get(), &MonitorWallWindow::EventActivated,
        [this](const QString& eventId) { OnEventActivated(eventId); });
    QObject::connect(window_.get(), &MonitorWallWindow::CameraSelectionChanged,
        [this](const QString& sessionId) { OnCameraSelectionChanged(sessionId); });
    QObject::connect(window_.get(), &MonitorWallWindow::CameraActivated,
        [this](const QString& sessionId) { OnCameraActivated(sessionId); });
    QObject::connect(window_.get(), &MonitorWallWindow::PlaybackDayChanged,
        [this](const QDate& day) { OnPlaybackDayChanged(day); });
    QObject::connect(window_.get(), &MonitorWallWindow::PlaybackMonthChanged,
        [this](const QDate& month) { OnPlaybackMonthChanged(month); });
    QObject::connect(window_.get(), &MonitorWallWindow::PlaybackSegmentRequested,
        [this](const QString& relativePath) { OnPlaybackSegmentRequested(relativePath); });
    QObject::connect(window_.get(), &MonitorWallWindow::ToggleRecordAllRequested,
        [this](bool enabled) { OnToggleRecordAll(enabled); });
    QObject::connect(window_.get(), &MonitorWallWindow::WallSlotActivated,
        [this](int slotIndex) { OnWallSlotActivated(slotIndex); });
    QObject::connect(window_.get(), &MonitorWallWindow::TileReorderRequested,
        [this](const QString& draggedSessionId, const QString& targetSessionId) {
            OnTileReordered(draggedSessionId, targetSessionId);
        });

    auto* refreshTimer = new QTimer(window_.get());
    refreshTimer->setInterval(250);
    QObject::connect(refreshTimer, &QTimer::timeout, window_.get(), [this]() {
        RefreshUi();
        RefreshSecondaryWalls();
    });
    refreshTimer->start();
}

MonitorWallController::~MonitorWallController()
{
    for (auto it = secondaryRuntimes_.begin(); it != secondaryRuntimes_.end(); ++it)
    {
        if (!it.value())
            continue;
        CloseSecondaryPopouts(*it.value());
    }
    qDeleteAll(secondaryRuntimes_);
    secondaryRuntimes_.clear();
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
    workspace_.CloseAll();
}

QPushButton* MonitorWallController::ToggleButton() const
{
    return toggleButton_;
}

QList<QWidget*> MonitorWallController::AutoHideWidgets() const
{
    return { toggleButton_ };
}

void MonitorWallController::InstallEventFilters(QObject* filter) const
{
    if (filter && toggleButton_)
        toggleButton_->installEventFilter(filter);
}

void MonitorWallController::LoadSettings()
{
    const MonitorWorkspacePreferences preferences = config_
        ? config_->LoadMonitorWorkspacePreferences()
        : MonitorWorkspacePreferences{};

    workspaceStates_ = preferences.workspaces;
    currentWorkspaceId_ = preferences.activeWorkspaceId.trimmed();
    if (currentWorkspaceId_.isEmpty() && !workspaceStates_.isEmpty())
        currentWorkspaceId_ = workspaceStates_.first().workspaceId;

    RefreshWorkspaceSelectors();
    if (const WorkspaceState* workspaceState = FindWorkspaceState(currentWorkspaceId_))
        LoadWorkspace(*workspaceState);
    else if (!workspaceStates_.isEmpty())
        LoadWorkspace(workspaceStates_.first());

    RefreshUi();
    if (const WorkspaceState* workspaceState = FindWorkspaceState(currentWorkspaceId_))
    {
        if (workspaceState->windowVisible)
        {
            MoveWindowToAssignedScreen();
            window_->show();
            MoveWindowToAssignedScreen();
            if (workspaceState->fullscreen)
                window_->showFullScreen();
        }
    }
    SyncSecondaryWalls();
}

void MonitorWallController::SaveSettings()
{
    if (!config_)
        return;

    SaveCurrentWorkspaceState();
    for (auto it = secondaryRuntimes_.begin(); it != secondaryRuntimes_.end(); ++it)
    {
        WorkspaceState* workspaceState = FindWorkspaceState(it.key());
        if (!workspaceState)
            continue;
        if (it.value())
            SaveSecondaryRuntimeState(*it.value());
        workspaceState->windowVisible = it.value() && it.value()->window && it.value()->window->isVisible();
    }

    MonitorWorkspacePreferences preferences;
    preferences.activeWorkspaceId = currentWorkspaceId_;
    preferences.workspaces = workspaceStates_;
    if (const WorkspaceState* workspaceState = FindWorkspaceState(currentWorkspaceId_))
    {
        preferences.sources = workspaceState->sources;
        preferences.layoutPreset = workspaceState->layoutPreset;
        preferences.selectedSessionId = workspaceState->selectedSessionId;
        preferences.audioSessionId = workspaceState->audioSessionId;
        preferences.archiveRootDir = workspaceState->archiveRootDir;
        preferences.maximizedSessionId = workspaceState->maximizedSessionId;
        preferences.windowVisible = workspaceState->windowVisible;
        preferences.fullscreen = workspaceState->fullscreen;
    }
    config_->SaveMonitorWorkspacePreferences(preferences);
}

void MonitorWallController::ToggleWindow()
{
    if (!window_)
        return;

    if (window_->isVisible())
        window_->hide();
    else
    {
        MoveWindowToAssignedScreen();
        window_->show();
        MoveWindowToAssignedScreen();
        window_->raise();
        window_->activateWindow();
        RefreshUi();
    }
    SaveSettings();
    SyncSecondaryWalls();
}

void MonitorWallController::AddOrUpdateSource()
{
    MonitorSourceDescriptor source = NormalizeSource(window_->ReadSourceDescriptor());
    if (!source.IsValid())
    {
        window_->SetStatusText("cameraId/source is required.");
        return;
    }

    if (!IsNetworkSource(source.sourceUrl))
    {
        const QFileInfo info(source.sourceUrl);
        if (!info.exists() || !info.isFile())
        {
            window_->SetStatusText("Local file does not exist.");
            return;
        }
        source.sourceUrl = info.absoluteFilePath();
    }

    MonitorSourceDescriptor existing;
    const bool hasExisting = workspace_.GetSessionSource(source.cameraId, &existing);
    if (hasExisting && !SameSource(existing, source))
    {
        const auto answer = QtUiTheme::AskQuestion(
            window_.get(),
            "Update Monitor Source",
            QString("Session %1 already exists. Update it with the new source settings?")
                .arg(source.cameraId));
        if (answer != QMessageBox::Yes)
            return;
    }

    const QString sessionId = workspace_.UpsertSource(source);
    currentFavoriteLayoutId_.clear();
    EnsureTile(sessionId);
    workspace_.SelectSession(sessionId);
    workspace_.SetAudioSession(sessionId);

    PopulateEditorFromSession(sessionId);
    window_->SetStatusText(QString("Saved %1").arg(sessionId));
    RefreshUi();
    SaveSettings();
}

void MonitorWallController::OpenSelected()
{
    const MonitorWorkspaceSnapshot snapshot = workspace_.GetSnapshot(currentPreset_);
    if (snapshot.selectedSessionId.isEmpty())
    {
        window_->SetStatusText("No monitor session selected.");
        return;
    }

    EnsureSessionVisibleOnWall(snapshot.selectedSessionId);
    ReopenSessionAsync(snapshot.selectedSessionId);
    RefreshUi();
}

void MonitorWallController::OpenAll()
{
    const MonitorWorkspaceSnapshot snapshot = workspace_.GetSnapshot(currentPreset_);
    const QStringList assignedSlots = NormalizeAssignedSessionOrder(snapshot, sessionOrder_);
    const bool hasAssignedSession = std::any_of(
        assignedSlots.begin(),
        assignedSlots.end(),
        [](const QString& sessionId) { return !sessionId.trimmed().isEmpty(); });
    if (!hasAssignedSession)
    {
        const int capacity = std::max(1, WallCapacity());
        int assigned = 0;
        for (const MonitorSessionSnapshot& session : snapshot.sessions)
        {
            if (!EnsureSessionVisibleOnWall(session.sessionId))
                continue;
            ++assigned;
            if (assigned >= capacity)
                break;
        }
    }

    for (const QString& sessionId : NormalizeAssignedSessionOrder(snapshot, sessionOrder_))
    {
        if (sessionId.trimmed().isEmpty())
            continue;
        ReopenSessionAsync(sessionId);
    }
    window_->SetStatusText(!hasAssignedSession && snapshot.sessions.isEmpty()
        ? QStringLiteral("No monitor sessions to open.")
        : QStringLiteral("Opening wall sessions in background..."));
    RefreshUi();
}

void MonitorWallController::CloseAll()
{
    InvalidatePendingOpens(pendingOpenRequests_);
    for (const QString& sessionId : NormalizeAssignedSessionOrder(workspace_.GetSnapshot(currentPreset_), sessionOrder_))
    {
        if (sessionId.trimmed().isEmpty())
            continue;
        workspace_.CloseSession(sessionId);
    }
    window_->SetStatusText("Wall sessions closed.");
    RefreshUi();
}

void MonitorWallController::RemoveSelected()
{
    const MonitorWorkspaceSnapshot snapshot = workspace_.GetSnapshot(currentPreset_);
    if (snapshot.selectedSessionId.isEmpty())
    {
        window_->SetStatusText("No monitor session selected.");
        return;
    }

    const auto answer = QtUiTheme::AskQuestion(
        window_.get(),
        "Remove Monitor Source",
        QString("Remove monitor session %1?").arg(snapshot.selectedSessionId));
    if (answer != QMessageBox::Yes)
        return;

    InvalidatePendingOpen(pendingOpenRequests_, snapshot.selectedSessionId);
    RemoveSessionFromWall(snapshot.selectedSessionId);
    workspace_.RemoveSession(snapshot.selectedSessionId);
    RemoveTile(snapshot.selectedSessionId);
    currentFavoriteLayoutId_.clear();
    window_->SetStatusText(QString("Removed %1").arg(snapshot.selectedSessionId));
    RefreshUi();
    SaveSettings();
}

void MonitorWallController::BrowseArchiveRoot()
{
    const QString currentDir = ArchiveRootDir();
    const QString selected = QtUiTheme::GetExistingDirectory(
        host_, "Select Monitor Archive Root", currentDir);
    if (selected.isEmpty())
        return;

    window_->SetArchiveRootDir(selected);
    SaveArchivePreferencesFromWindow();
    window_->SetStatusText(QString("Archive root: %1").arg(selected));
    SaveSettings();
}

void MonitorWallController::BrowseSourceFile()
{
    const QString selected = QtUiTheme::GetOpenFileName(
        host_,
        "Select Monitor Source File",
        QDir::currentPath(),
        "Media Files (*.mp4 *.mkv *.mov *.avi *.flv *.ts *.m4v);;All Files (*.*)");
    if (selected.isEmpty())
        return;

    MonitorSourceDescriptor source = window_->ReadSourceDescriptor();
    source.sourceUrl = QDir::cleanPath(selected);
    if (source.displayName.trimmed().isEmpty())
        source.displayName = QFileInfo(selected).completeBaseName();
    if (source.cameraId.trimmed().isEmpty())
        source.cameraId = QFileInfo(selected).completeBaseName();
    window_->WriteSourceDescriptor(source);
    window_->SetStatusText(QString("Selected file: %1").arg(QFileInfo(selected).fileName()));
}

void MonitorWallController::ToggleFullscreen()
{
    if (!window_)
        return;

    if (window_->isFullScreen())
        window_->showNormal();
    else
    {
        MoveWindowToAssignedScreen();
        window_->showFullScreen();
    }
    window_->SetWindowFullscreen(window_->isFullScreen());
    SaveSettings();
}

void MonitorWallController::OnLayoutPresetChanged(MonitorWallLayoutPreset preset)
{
    currentPreset_ = preset;
    const int capacity = std::max(1, WallCapacity());
    while (sessionOrder_.size() > capacity)
    {
        const QString removedId = sessionOrder_.takeLast();
        if (!removedId.isEmpty())
            workspace_.CloseSession(removedId);
    }
    while (sessionOrder_.size() < capacity)
        sessionOrder_.append(QString{});
    sessionOrder_ = NormalizeAssignedSessionOrder(workspace_.GetSnapshot(currentPreset_), sessionOrder_);
    if (activeWallSlotIndex_ >= capacity)
        activeWallSlotIndex_ = capacity - 1;
    currentFavoriteLayoutId_.clear();
    RefreshUi();
    SaveSettings();
}

void MonitorWallController::OnWorkspaceChanged(const QString& workspaceId)
{
    if (workspaceId.trimmed().isEmpty() || workspaceId == currentWorkspaceId_)
        return;

    SaveCurrentWorkspaceState();
    if (const WorkspaceState* workspaceState = FindWorkspaceState(workspaceId))
    {
        LoadWorkspace(*workspaceState);
        SyncSecondaryWalls();
        SaveSettings();
    }
}

void MonitorWallController::OnPreviousWorkspace()
{
    if (workspaceStates_.isEmpty())
        return;

    int currentIndex = 0;
    for (int index = 0; index < workspaceStates_.size(); ++index)
    {
        if (workspaceStates_.at(index).workspaceId == currentWorkspaceId_)
        {
            currentIndex = index;
            break;
        }
    }

    const int previousIndex = (currentIndex - 1 + workspaceStates_.size()) % workspaceStates_.size();
    OnWorkspaceChanged(workspaceStates_.at(previousIndex).workspaceId);
}

void MonitorWallController::OnNextWorkspace()
{
    if (workspaceStates_.isEmpty())
        return;

    int currentIndex = 0;
    for (int index = 0; index < workspaceStates_.size(); ++index)
    {
        if (workspaceStates_.at(index).workspaceId == currentWorkspaceId_)
        {
            currentIndex = index;
            break;
        }
    }

    const int nextIndex = (currentIndex + 1) % workspaceStates_.size();
    OnWorkspaceChanged(workspaceStates_.at(nextIndex).workspaceId);
}

void MonitorWallController::OnSaveWorkspace()
{
    SaveCurrentWorkspaceState();
    RefreshWorkspaceSelectors();
    window_->SetStatusText(QString("Saved %1").arg(currentWorkspaceId_));
    SaveSettings();
}

void MonitorWallController::OnNewWorkspace()
{
    SaveCurrentWorkspaceState();

    WorkspaceState workspaceState;
    int workspaceIndex = workspaceStates_.size() + 1;
    do
    {
        workspaceState.workspaceId = QString("workspace-%1").arg(workspaceIndex++);
    } while (FindWorkspaceState(workspaceState.workspaceId) != nullptr);
    workspaceState.workspaceName = CreateWorkspaceName();
    workspaceState.layoutPreset = static_cast<int>(currentPreset_);
    workspaceState.archiveRootDir = ArchiveRootDir();
    workspaceState.assignedScreen = currentScreenName_;
    workspaceStates_.append(workspaceState);
    currentWorkspaceId_ = workspaceState.workspaceId;
    LoadWorkspace(workspaceState);
    SyncSecondaryWalls();
    SaveSettings();
}

void MonitorWallController::OnDeleteWorkspace()
{
    if (workspaceStates_.size() <= 1)
    {
        window_->SetStatusText("At least one workspace must remain.");
        return;
    }

    const auto answer = QtUiTheme::AskQuestion(
        window_.get(),
        "Delete Workspace",
        QString("Delete workspace %1?").arg(currentWorkspaceId_));
    if (answer != QMessageBox::Yes)
        return;

    for (int index = 0; index < workspaceStates_.size(); ++index)
    {
        if (workspaceStates_.at(index).workspaceId != currentWorkspaceId_)
            continue;
        workspaceStates_.removeAt(index);
        break;
    }

    currentWorkspaceId_ = workspaceStates_.isEmpty() ? QString{} : workspaceStates_.first().workspaceId;
    if (const WorkspaceState* workspaceState = FindWorkspaceState(currentWorkspaceId_))
        LoadWorkspace(*workspaceState);
    SyncSecondaryWalls();
    SaveSettings();
}

void MonitorWallController::OnScreenAssignmentChanged(const QString& screenName)
{
    currentScreenName_ = screenName.trimmed();
    MoveWindowToAssignedScreen();
    SyncSecondaryWalls();
    SaveSettings();
}

void MonitorWallController::OnGroupFilterChanged(const QString& groupName)
{
    if (groupName == currentGroupFilter_)
        return;

    currentGroupFilter_ = groupName.trimmed();
    currentFavoriteLayoutId_.clear();
    RefreshUi();
    SaveSettings();
}

void MonitorWallController::OnOpenGroup()
{
    const MonitorWorkspaceSnapshot snapshot = workspace_.GetSnapshot(currentPreset_);
    bool matched = false;
    for (const MonitorSessionSnapshot& session : snapshot.sessions)
    {
        MonitorSourceDescriptor source;
        if (!workspace_.GetSessionSource(session.sessionId, &source) || !SessionMatchesCurrentGroupFilter(source))
            continue;

        matched = true;
        EnsureTile(session.sessionId);
        ReopenSessionAsync(session.sessionId);
    }

    if (!matched)
        window_->SetStatusText("No session matched current group filter.");
    else
        window_->SetStatusText("Opening group sessions in background...");
    RefreshUi();
}

void MonitorWallController::OnCloseGroup()
{
    const MonitorWorkspaceSnapshot snapshot = workspace_.GetSnapshot(currentPreset_);
    bool matched = false;
    for (const MonitorSessionSnapshot& session : snapshot.sessions)
    {
        MonitorSourceDescriptor source;
        if (!workspace_.GetSessionSource(session.sessionId, &source) || !SessionMatchesCurrentGroupFilter(source))
            continue;

        matched = true;
        InvalidatePendingOpen(pendingOpenRequests_, session.sessionId);
        workspace_.CloseSession(session.sessionId);
    }

    window_->SetStatusText(matched ? "Group closed." : "No session matched current group filter.");
    RefreshUi();
}

void MonitorWallController::OnFavoriteLayoutChanged(const QString& layoutId)
{
    if (layoutId == currentFavoriteLayoutId_)
        return;

    currentFavoriteLayoutId_ = layoutId.trimmed();
    if (currentFavoriteLayoutId_.isEmpty())
    {
        RefreshUi();
        SaveSettings();
        return;
    }

    WorkspaceState* workspaceState = FindWorkspaceState(currentWorkspaceId_);
    if (!workspaceState)
        return;

    const WorkspaceState::FavoriteLayoutState* favorite = FindFavoriteLayout(*workspaceState, currentFavoriteLayoutId_);
    if (!favorite)
    {
        currentFavoriteLayoutId_.clear();
        RefreshUi();
        return;
    }

    currentPreset_ = static_cast<MonitorWallLayoutPreset>(std::clamp(favorite->layoutPreset, 0, 4));
    sessionOrder_ = favorite->sessionOrder;
    maximizedSessionId_ = favorite->maximizedSessionId.trimmed();
    currentGroupFilter_.clear();
    if (!favorite->selectedSessionId.trimmed().isEmpty())
    {
        workspace_.SelectSession(favorite->selectedSessionId.trimmed());
        PopulateEditorFromSession(favorite->selectedSessionId.trimmed());
    }
    RefreshUi();
    SaveSettings();
}

void MonitorWallController::OnSaveFavoriteLayout()
{
    WorkspaceState* workspaceState = FindWorkspaceState(currentWorkspaceId_);
    if (!workspaceState)
        return;

    bool ok = false;
    const WorkspaceState::FavoriteLayoutState* existing =
        currentFavoriteLayoutId_.isEmpty() ? nullptr : FindFavoriteLayout(*workspaceState, currentFavoriteLayoutId_);
    const QString defaultName = existing
        ? existing->layoutName
        : CreateFavoriteLayoutName(*workspaceState);
    const QString name = QInputDialog::getText(
        window_.get(),
        existing ? "Update Favorite Layout" : "Save Favorite Layout",
        "Layout name",
        QLineEdit::Normal,
        defaultName,
        &ok).trimmed();
    if (!ok || name.isEmpty())
        return;

    SaveCurrentWorkspaceState();
    MonitorWorkspaceSnapshot snapshot = workspace_.GetSnapshot(currentPreset_);
    snapshot = ApplySessionOrder(std::move(snapshot));

    WorkspaceState::FavoriteLayoutState* favorite = existing
        ? FindFavoriteLayout(*workspaceState, currentFavoriteLayoutId_)
        : nullptr;
    if (!favorite)
    {
        WorkspaceState::FavoriteLayoutState newLayout;
        int layoutIndex = workspaceState->favoriteLayouts.size() + 1;
        do
        {
            newLayout.layoutId = QString("%1-layout-%2").arg(currentWorkspaceId_).arg(layoutIndex++);
        } while (FindFavoriteLayout(*workspaceState, newLayout.layoutId) != nullptr);
        workspaceState->favoriteLayouts.append(newLayout);
        favorite = &workspaceState->favoriteLayouts.last();
    }

    favorite->layoutName = name;
    favorite->layoutPreset = static_cast<int>(currentPreset_);
    favorite->selectedSessionId = snapshot.selectedSessionId;
    favorite->maximizedSessionId = maximizedSessionId_;
    favorite->sessionOrder = NormalizeAssignedSessionOrder(snapshot, sessionOrder_);
    favorite->groupFilter = currentGroupFilter_;
    currentFavoriteLayoutId_ = favorite->layoutId;

    RefreshUi();
    SaveSettings();
}

void MonitorWallController::OnDeleteFavoriteLayout()
{
    if (currentFavoriteLayoutId_.trimmed().isEmpty())
    {
        window_->SetStatusText("No favorite layout selected.");
        return;
    }

    WorkspaceState* workspaceState = FindWorkspaceState(currentWorkspaceId_);
    if (!workspaceState)
        return;

    for (int index = 0; index < workspaceState->favoriteLayouts.size(); ++index)
    {
        if (workspaceState->favoriteLayouts.at(index).layoutId != currentFavoriteLayoutId_)
            continue;
        workspaceState->favoriteLayouts.removeAt(index);
        currentFavoriteLayoutId_.clear();
        RefreshUi();
        SaveSettings();
        return;
    }
}

void MonitorWallController::OnAcknowledgeSelectedEvent()
{
    if (!window_)
        return;

    const QString eventId = window_->SelectedEventId();
    if (eventId.isEmpty())
        return;

    eventCenter_.Acknowledge(eventId);
    RefreshUi();
}

void MonitorWallController::OnClearSelectedEvent()
{
    if (!window_)
        return;

    const QString eventId = window_->SelectedEventId();
    if (eventId.isEmpty())
        return;

    eventCenter_.Clear(eventId);
    RefreshUi();
}

void MonitorWallController::OnJumpToSelectedEvent()
{
    if (!window_)
        return;

    const QString eventId = window_->SelectedEventId();
    if (!eventId.isEmpty())
        OnEventActivated(eventId);
}

void MonitorWallController::OnEventActivated(const QString& eventId)
{
    const MonitorEventEntry event = eventCenter_.FindEvent(eventId);
    if (event.eventId.isEmpty())
        return;

    if (!event.sessionId.isEmpty())
    {
        workspace_.SelectSession(event.sessionId);
        workspace_.SetAudioSession(event.sessionId);
        PopulateEditorFromSession(event.sessionId);
    }

    QString error;
    const QString archivePath = ResolveArchivePathForEvent(event, &error);
    if (!archivePath.isEmpty() && playArchiveSegmentHandler_)
    {
        playArchiveSegmentHandler_(archivePath);
        eventCenter_.Acknowledge(eventId);
        window_->SetStatusText(QString("Jumped to archive segment for %1").arg(event.displayName));
    }
    else if (!error.isEmpty())
    {
        window_->SetStatusText(error);
    }

    RefreshUi();
}

void MonitorWallController::OnCameraSelectionChanged(const QString& sessionId)
{
    if (sessionId.trimmed().isEmpty())
        return;

    workspace_.SelectSession(sessionId);
    workspace_.SetAudioSession(sessionId);
    PopulateEditorFromSession(sessionId);
    RefreshUi();
    SaveSettings();
}

void MonitorWallController::OnCameraActivated(const QString& sessionId)
{
    if (sessionId.trimmed().isEmpty())
        return;

    OnCameraSelectionChanged(sessionId);
    EnsureSessionVisibleOnWall(sessionId);
    ReopenSessionAsync(sessionId);
}

void MonitorWallController::OnPlaybackDayChanged(const QDate& day)
{
    if (!day.isValid())
        return;

    playbackSelectedDate_ = day;
    playbackVisibleMonth_ = QDate(day.year(), day.month(), 1);
    RefreshUi();
}

void MonitorWallController::OnPlaybackMonthChanged(const QDate& month)
{
    if (!month.isValid())
        return;

    playbackVisibleMonth_ = QDate(month.year(), month.month(), 1);
    if (!playbackSelectedDate_.isValid()
        || playbackSelectedDate_.year() != month.year()
        || playbackSelectedDate_.month() != month.month())
    {
        playbackSelectedDate_ = playbackVisibleMonth_;
        if (window_)
            window_->SetSelectedPlaybackDate(playbackSelectedDate_);
    }
    RefreshUi();
}

void MonitorWallController::ReopenSessionAsync(const QString& sessionId, const QString& successText)
{
    ReopenSessionAsyncToTile(sessionId, tiles_.value(sessionId, nullptr), successText);
}

void MonitorWallController::ReopenSessionAsyncToTile(
    const QString& sessionId,
    MonitorTileWidget* renderTile,
    const QString& successText)
{
    if (!window_)
        return;

    EnsureTile(sessionId);
    MonitorSourceDescriptor source;
    if (!workspace_.GetSessionSource(sessionId, &source))
    {
        window_->SetStatusText(QString("Monitor session missing: %1").arg(sessionId));
        return;
    }

    const quint64 requestId = BeginPendingOpen(pendingOpenRequests_, nextOpenRequestId_, sessionId);
    window_->SetStatusText(QString("Opening %1...").arg(sessionId));

    const QPointer<MonitorWallWindow> guardWindow(window_.get());
    const QPointer<MonitorTileWidget> guardTile(renderTile);
    std::thread([this, guardWindow, guardTile, sessionId, source, requestId, successText]()
    {
        auto result = std::make_shared<PendingMonitorOpenResult>(PrepareMonitorOpen(source));
        if (!guardWindow)
            return;

        QMetaObject::invokeMethod(
            guardWindow.data(),
            [this, guardWindow, guardTile, sessionId, source, requestId, result, successText]() mutable
            {
                if (!guardWindow || !MatchesPendingOpen(pendingOpenRequests_, sessionId, requestId))
                    return;

                InvalidatePendingOpen(pendingOpenRequests_, sessionId);

                if (!result->demux)
                {
                    const QString detail = result->openError.isEmpty()
                        ? QStringLiteral("Failed to open source.")
                        : result->openError;
                    guardWindow->SetStatusText(QString("Open failed %1: %2").arg(sessionId, detail));
                    RefreshUi();
                    return;
                }

                MonitorTileWidget* tile = guardTile ? guardTile.data() : tiles_.value(sessionId, nullptr);
                if (!tile)
                {
                    RefreshUi();
                    return;
                }

                const std::shared_ptr<VideoCallback> callback = tile->VideoSurface()->CreateCallbackHandle();
                const bool reopened = workspace_.ReopenSessionPrepared(
                    sessionId,
                    source.sourceUrl,
                    result->demux.release(),
                    result->effectiveOptions,
                    callback,
                    result->openLatencyMs);
                if (reopened)
                {
                    BindDetectorHandlers(sessionId);
                    BindAsrHandlers(sessionId);
                }
                guardWindow->SetStatusText(reopened
                    ? (successText.isEmpty() ? QString("Opened %1").arg(sessionId) : successText)
                    : QString("Open failed %1").arg(sessionId));
                RefreshUi();
            },
            Qt::QueuedConnection);
    }).detach();
}

void MonitorWallController::OnPlaybackSegmentRequested(const QString& relativePath)
{
    if (relativePath.trimmed().isEmpty())
        return;

    QString segmentError;
    const std::optional<ArchiveSegmentRecord> segment = FindPlaybackSegmentByRelativePath(relativePath, &segmentError);
    if (!segment.has_value())
    {
        if (window_)
            window_->SetStatusText(segmentError.isEmpty() ? "Playback segment metadata not found." : segmentError);
        return;
    }

    EnsurePlaybackProxyAsync(*segment);
    const QString absolutePath = ResolvePreferredPlaybackAbsolutePath(*segment);
    if (absolutePath.isEmpty())
    {
        if (window_)
            window_->SetStatusText("Playback file not found.");
        return;
    }

    if (playArchiveSegmentHandler_)
    {
        playArchiveSegmentHandler_(absolutePath);
        if (window_)
        {
            window_->SetStatusText(segment->playbackProxyReady
                ? QString("Opened MP4 playback proxy: %1").arg(QFileInfo(absolutePath).fileName())
                : QString("Opened playback: %1").arg(QFileInfo(absolutePath).fileName()));
        }
    }
}

void MonitorWallController::OnToggleRecordAll(bool enabled)
{
    QStringList assignedIds;
    for (const QString& sessionId : NormalizeAssignedSessionOrder(workspace_.GetSnapshot(currentPreset_), sessionOrder_))
    {
        if (!sessionId.trimmed().isEmpty())
            assignedIds.append(sessionId);
    }
    if (assignedIds.isEmpty())
    {
        if (window_)
            window_->SetStatusText("No wall sessions assigned.");
        RefreshUi();
        return;
    }

    SaveArchivePreferencesFromWindow();
    playbackSelectedDate_ = QDate::currentDate();
    playbackVisibleMonth_ = QDate(playbackSelectedDate_.year(), playbackSelectedDate_.month(), 1);
    if (!assignedIds.isEmpty())
        workspace_.SelectSession(assignedIds.first());
    if (window_)
        window_->SetSelectedPlaybackDate(playbackSelectedDate_);
    int successCount = 0;
    std::string lastError;
    const RecordingConfiguration configuration = BuildMonitorRecordingConfiguration();
    for (const QString& sessionId : assignedIds)
    {
        std::string error;
        if (workspace_.SetSessionRecordingEnabled(sessionId, enabled, configuration, &error))
        {
            ++successCount;
        }
        else if (!error.empty())
        {
            lastError = error;
        }
    }

    if (window_)
    {
        if (successCount == assignedIds.size())
        {
            window_->SetStatusText(enabled
                ? QString("Recording enabled for %1 wall session(s).").arg(successCount)
                : QString("Recording stopped for %1 wall session(s).").arg(successCount));
        }
        else if (!lastError.empty())
        {
            window_->SetStatusText(QString::fromStdString(lastError));
        }
    }
    RefreshUi();
    SaveSettings();
}

void MonitorWallController::ReopenSecondarySessionAsync(SecondaryWallRuntime& runtime, const QString& sessionId)
{
    if (!runtime.window)
        return;

    EnsureSecondaryTile(runtime, sessionId);
    MonitorSourceDescriptor source;
    if (!runtime.workspace.GetSessionSource(sessionId, &source))
        return;

    const quint64 requestId = BeginPendingOpen(runtime.pendingOpenRequests, runtime.nextOpenRequestId, sessionId);
    SecondaryWallRuntime* runtimePtr = &runtime;
    const QPointer<MonitorWallWindow> guardWindow(runtime.window.get());
    std::thread([this, runtimePtr, guardWindow, sessionId, source, requestId]()
    {
        auto result = std::make_shared<PendingMonitorOpenResult>(PrepareMonitorOpen(source));
        if (!guardWindow)
            return;

        QMetaObject::invokeMethod(
            guardWindow.data(),
            [this, runtimePtr, guardWindow, sessionId, source, requestId, result]() mutable
            {
                if (!guardWindow
                    || !runtimePtr
                    || !MatchesPendingOpen(runtimePtr->pendingOpenRequests, sessionId, requestId))
                {
                    return;
                }

                InvalidatePendingOpen(runtimePtr->pendingOpenRequests, sessionId);
                if (!result->demux)
                {
                    RefreshSecondaryRuntime(*runtimePtr);
                    return;
                }

                MonitorTileWidget* tile = runtimePtr->tiles.value(sessionId, nullptr);
                if (!tile)
                {
                    RefreshSecondaryRuntime(*runtimePtr);
                    return;
                }

                const std::shared_ptr<VideoCallback> callback = tile->VideoSurface()->CreateCallbackHandle();
                runtimePtr->workspace.ReopenSessionPrepared(
                    sessionId,
                    source.sourceUrl,
                    result->demux.release(),
                    result->effectiveOptions,
                    callback,
                    result->openLatencyMs);
                RefreshSecondaryRuntime(*runtimePtr);
            },
            Qt::QueuedConnection);
    }).detach();
}

void MonitorWallController::RefreshUi()
{
    if (!window_)
        return;

    MonitorWorkspaceSnapshot baseSnapshot = workspace_.GetSnapshot(currentPreset_);
    baseSnapshot = ApplySessionOrder(std::move(baseSnapshot));
    const MonitorEventSnapshot eventSnapshot = eventCenter_.GetSnapshot();
    for (const MonitorSessionSnapshot& session : baseSnapshot.sessions)
        EnsureTile(session.sessionId);

    const QSet<QString> visibleSessionIds = VisibleSessionIds(baseSnapshot);
    ApplyAiPolicies(baseSnapshot, eventSnapshot, visibleSessionIds);
    if (NormalizeAudioRouteForVisibleSessions(baseSnapshot, visibleSessionIds))
        baseSnapshot = workspace_.GetSnapshot(currentPreset_);

    MonitorWorkspaceSnapshot snapshot = workspace_.GetSnapshot(currentPreset_);
    snapshot = ApplySessionOrder(std::move(snapshot));
    QHash<QString, MonitorSessionSnapshot> sessionsById;
    for (MonitorSessionSnapshot& session : snapshot.sessions)
    {
        session.eventCount = eventSnapshot.activeEventCountsBySession.value(session.sessionId, 0);
        session.alarmCount = eventSnapshot.activeAlarmCountsBySession.value(session.sessionId, 0);
        session.alarmActive = session.alarmCount > 0;
        session.alarmAcknowledged = !eventSnapshot.hasUnacknowledgedAlarmBySession.value(session.sessionId, false);
        sessionsById.insert(session.sessionId, session);
    }

    const MonitorWorkspaceSnapshot wallSnapshot = FilteredSnapshot(snapshot);
    QSet<QString> activeSessionIds;
    for (const QString& sessionId : popouts_.keys())
        activeSessionIds.insert(sessionId);
    for (const MonitorSessionSnapshot& session : wallSnapshot.sessions)
    {
        activeSessionIds.insert(session.sessionId);
        EnsureTile(session.sessionId);
        if (MonitorTileWidget* tile = tiles_.value(session.sessionId, nullptr))
        {
            VideoWidget* surface = tile->VideoSurface();
            const bool visible = visibleSessionIds.contains(session.sessionId);
            if (surface)
            {

                surface->setPreferLiveRendering(false);
                if (surface->updatesEnabled() != visible)
                {
                    surface->setUpdatesEnabled(visible);
                    if (visible)
                        surface->update();
                }
            }
            tile->SetSnapshot(session);
        }
    }

    for (auto it = popouts_.cbegin(); it != popouts_.cend(); ++it)
    {
        if (MonitorTileWidget* tile = it.value() ? it.value()->Tile() : nullptr)
        {
            const auto sessionIt = sessionsById.constFind(it.key());
            if (sessionIt != sessionsById.constEnd())
                tile->SetSnapshot(sessionIt.value());
        }
    }

    for (auto it = tiles_.begin(); it != tiles_.end(); )
    {
        if (!activeSessionIds.contains(it.key()))
        {
            delete it.value();
            it = tiles_.erase(it);
        }
        else
        {
            if (popouts_.contains(it.key()))
                it.value()->hide();
            else
                it.value()->show();
            ++it;
        }
    }

    RefreshWorkspaceSelectors();

    window_->SetCameraSessions(snapshot.sessions, snapshot.selectedSessionId);
    RefreshPlaybackPanel(snapshot);
    window_->SetActiveWallSlot(activeWallSlotIndex_);
    window_->SetWindowFullscreen(window_->isFullScreen());
    window_->SetEvents(eventSnapshot.entries);
    window_->SetRecordAllState(
        !wallSnapshot.sessions.isEmpty()
            && std::all_of(wallSnapshot.sessions.begin(), wallSnapshot.sessions.end(),
                [](const MonitorSessionSnapshot& session) { return session.recordingEnabled; }),
        wallSnapshot.sessions.size());
    QMap<QString, MonitorTileWidget*> wallTiles = tiles_;
    for (auto it = popouts_.cbegin(); it != popouts_.cend(); ++it)
        wallTiles.remove(it.key());
    window_->ApplyWorkspaceSnapshot(wallSnapshot, wallTiles);
    SyncRecordingStatusFeedback(snapshot);
}

void MonitorWallController::ApplyArchivePreferences(const ArchivePreferences& preferences)
{
    if (!window_)
        return;

    window_->SetArchiveRootDir(preferences.archiveRootDir);
    window_->SetRecordingContainer(preferences.container);
    window_->SetRecordingSegmentDurationSeconds(preferences.segmentDurationSeconds);
}

void MonitorWallController::SaveArchivePreferencesFromWindow() const
{
    if (!config_)
        return;

    ArchivePreferences preferences = config_->LoadArchivePreferences();
    preferences.archiveRootDir = ArchiveRootDir().trimmed();
    preferences.container = window_
        ? ArchivePathPolicy::NormalizeRecordingContainer(window_->RecordingContainer())
        : preferences.container;
    preferences.segmentDurationSeconds = window_
        ? std::max(10, window_->RecordingSegmentDurationSeconds())
        : preferences.segmentDurationSeconds;
    config_->SaveArchivePreferences(preferences);
}

RecordingConfiguration MonitorWallController::BuildMonitorRecordingConfiguration() const
{
    RecordingConfiguration configuration;
    configuration.enabled = true;
    configuration.archiveRootDir = ArchiveRootDir().trimmed();
    configuration.container = window_
        ? ArchivePathPolicy::NormalizeRecordingContainer(window_->RecordingContainer())
        : QStringLiteral("mkv");
    configuration.segmentDurationSeconds = window_
        ? std::max(10, window_->RecordingSegmentDurationSeconds())
        : 300;
    return configuration;
}

void MonitorWallController::PopulateEditorFromSession(const QString& sessionId)
{
    MonitorSourceDescriptor source;
    if (workspace_.GetSessionSource(sessionId, &source))
        window_->WriteSourceDescriptor(source);
}

void MonitorWallController::RecordDetectorEvent(const QString& sessionId, const DetectionResult& result)
{
    if (result.boxes.empty())
        return;

    float maxConfidence = 0.0f;
    QSet<QString> labels;
    for (const DetectionBox& box : result.boxes)
    {
        maxConfidence = std::max(maxConfidence, box.confidence);
        if (!box.className.trimmed().isEmpty())
            labels.insert(box.className);
    }

    QJsonArray labelArray;
    for (const QString& label : labels)
        labelArray.append(label);

    QJsonObject payload;
    payload.insert("count", static_cast<int>(result.boxes.size()));
    payload.insert("frameWidth", result.frameWidth);
    payload.insert("frameHeight", result.frameHeight);
    payload.insert("maxConfidence", maxConfidence);
    payload.insert("labels", labelArray);

    ArchiveEventRecord event;
    event.cameraId = sessionId;
    event.occurredAtUtc = QDateTime::currentDateTimeUtc();
    event.type = "detector";
    event.severity = (maxConfidence >= 0.85f || result.boxes.size() >= 3)
        ? ArchiveEventSeverity::Alarm
        : ArchiveEventSeverity::Warning;
    event.payloadJson = QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact));
    workspace_.RecordSessionEvent(sessionId, &event, nullptr);

    MonitorSourceDescriptor source;
    workspace_.GetSessionSource(sessionId, &source);
    QStringList dedupLabels = labels.values();
    std::sort(dedupLabels.begin(), dedupLabels.end());
    const QString title = dedupLabels.isEmpty()
        ? QString("Detector hit on %1").arg(source.displayName.isEmpty() ? sessionId : source.displayName)
        : QString("%1: %2")
            .arg(source.displayName.isEmpty() ? sessionId : source.displayName)
            .arg(dedupLabels.join(", "));
    MonitorEventCandidate candidate;
    candidate.sessionId = sessionId;
    candidate.cameraId = event.cameraId;
    candidate.displayName = source.displayName.isEmpty() ? sessionId : source.displayName;
    candidate.type = event.type;
    candidate.title = title;
    candidate.dedupKey = QString("detector:%1").arg(dedupLabels.join("|"));
    candidate.payloadJson = event.payloadJson;
    candidate.segmentId = event.segmentId;
    candidate.occurredAtUtc = event.occurredAtUtc;
    candidate.severity = event.severity;
    const MonitorEventIngestResult ingest = eventCenter_.Ingest(candidate);
    if (ingest.raisedAlarm)
    {
        workspace_.SelectSession(sessionId);
        workspace_.SetAudioSession(sessionId);
        PopulateEditorFromSession(sessionId);
        window_->SetStatusText(QString("Alarm: %1").arg(title));
    }
}
