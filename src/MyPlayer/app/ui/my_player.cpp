#include "my_player.h"

#include "../controller/feature/feature_manager.h"
#include "../controller/display/display_adjustment_controller.h"
#include "../controller/monitor/monitor_wall_controller.h"
#include "../controller/network/network_controller.h"
#include "../controller/playlist/playlist_manager.h"
#include "../controller/stats/playback_stats_controller.h"
#include "../controller/playback/playback_controller.h"
#include "../controller/subtitle/subtitle_controller.h"

#include "../service/config_service.h"
#include "../service/playback_service.h"

#include "../view/feature_view_qt.h"
#include "../view/network_view_qt.h"
#include "../view/playback_view_qt.h"
#include "../view/stats_view_qt.h"
#include "../view/subtitle_view_qt.h"

#include "my_player_ui_helpers.h"
#include "../../ui/chrome/player_chrome_controller.h"
#include "ui_my_player.h"

#include <QGraphicsOpacityEffect>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QRect>

using namespace MyPlayerUiHelpers;

namespace
{
struct MyPlayerShellWidgets
{
    QWidget* sidebarBackground = nullptr;
    QListWidget* playlist = nullptr;
    QPushButton* playlistButton = nullptr;
    QPushButton* colorButton = nullptr;
    QPushButton* audioTrackButton = nullptr;
    QPushButton* playModeButton = nullptr;
    QPushButton* addFolderButton = nullptr;
    QPushButton* clearListButton = nullptr;
    QPushButton* subtitleTrackButton = nullptr;
    QLabel* subtitleOsdLabel = nullptr;
};

QString SidebarButtonStyle()
{
    return
        "QPushButton { background: rgba(60,60,60,230); color: #F5F5F7; "
        "border: 1px solid rgba(255,255,255,40); border-radius: 4px; "
        "font-size: 12px; font-weight: 600; padding: 0 10px; }"
        "QPushButton:hover { background: rgba(80,80,80,230); }"
        "QPushButton:pressed { background: rgba(40,40,40,230); }";
}

MyPlayerShellWidgets BuildMyPlayerShellWidgets(QWidget* host, Ui::MyPlayerClass* ui)
{
    MyPlayerShellWidgets widgets;
    if (!host || !ui)
        return widgets;

    ui->liveLabel->setVisible(false);

    widgets.sidebarBackground = new QWidget(host);
    widgets.sidebarBackground->setStyleSheet("background: #1a1a1a;");
    widgets.sidebarBackground->setVisible(false);

    widgets.playlist = new QListWidget(host);
    widgets.playlist->setStyleSheet(
        "QListWidget { background: rgba(0,0,0,192); border: none; "
        "color: #F5F5F7; font-size: 13px; outline: none; }"
        "QListWidget::item { padding: 6px 10px; "
        "border-bottom: 1px solid rgba(255,255,255,20); }"
        "QListWidget::item:selected { background: rgba(10,132,255,128); }"
        "QListWidget::item:hover { background: rgba(255,255,255,25); }");
    widgets.playlist->setVisible(false);
    widgets.playlist->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    widgets.playModeButton = new QPushButton("Order", host);
    widgets.playModeButton->setFixedHeight(32);
    widgets.playModeButton->setVisible(false);
    widgets.playModeButton->setStyleSheet(SidebarButtonStyle());
    widgets.playModeButton->setToolTip("Play the list once in order.");

    widgets.addFolderButton = new QPushButton("Add", host);
    widgets.addFolderButton->setFixedSize(72, 32);
    widgets.addFolderButton->setVisible(false);
    widgets.addFolderButton->setStyleSheet(SidebarButtonStyle());
    widgets.addFolderButton->setToolTip("Add all videos from a folder.");

    widgets.clearListButton = new QPushButton("Clear", host);
    widgets.clearListButton->setFixedSize(72, 32);
    widgets.clearListButton->setVisible(false);
    widgets.clearListButton->setStyleSheet(SidebarButtonStyle());
    widgets.clearListButton->setToolTip("Clear the current playlist.");

    widgets.colorButton = new QPushButton("Color", host);
    widgets.colorButton->setFixedSize(72, 36);
    widgets.colorButton->setCheckable(true);
    widgets.colorButton->setVisible(false);

    widgets.audioTrackButton = new QPushButton("A1", host);
    widgets.audioTrackButton->setFixedSize(58, 36);
    widgets.audioTrackButton->setVisible(false);
    widgets.audioTrackButton->setContextMenuPolicy(Qt::CustomContextMenu);

    widgets.playlistButton = new QPushButton("List", host);
    widgets.playlistButton->setFixedSize(60, 36);

    widgets.subtitleTrackButton = new QPushButton("SUB", host);
    widgets.subtitleTrackButton->setFixedSize(64, 36);
    widgets.subtitleTrackButton->setCheckable(true);
    widgets.subtitleTrackButton->setContextMenuPolicy(Qt::CustomContextMenu);

    widgets.subtitleOsdLabel = new QLabel(host);
    widgets.subtitleOsdLabel->setStyleSheet(
        "background: rgba(0,0,0,180); color: #FFD700; "
        "font-family: 'Consolas','Courier New',monospace; font-size: 18px; "
        "font-weight: bold; padding: 10px 20px; border-radius: 6px;");
    widgets.subtitleOsdLabel->setVisible(false);
    widgets.subtitleOsdLabel->setAttribute(Qt::WA_TransparentForMouseEvents);

    return widgets;
}

void AttachAutoHideWidget(
    QList<QWidget*>& autoHideWidgets,
    QList<QGraphicsOpacityEffect*>& controlEffects,
    QWidget* widget)
{
    if (!widget)
        return;

    autoHideWidgets.append(widget);
    (void)controlEffects;
}
}

class MyPlayerShellStateCoordinator
{
public:
    MyPlayerShellStateCoordinator(
        PlaybackController* playbackController,
        PlaylistManager* playlistManager,
        SubtitleController* subtitleController,
        FeatureManager* featureManager,
        NetworkController* networkController,
        MonitorWallController* monitorWallController,
        ConfigService* configService)
        : playbackController_(playbackController)
        , playlistManager_(playlistManager)
        , subtitleController_(subtitleController)
        , featureManager_(featureManager)
        , networkController_(networkController)
        , monitorWallController_(monitorWallController)
        , configService_(configService)
    {
    }

    void LoadStartupState() const
    {
        if (playlistManager_ && configService_)
            playlistManager_->RestoreState(configService_->LoadPlaylistState());

        if (subtitleController_)
        {
            subtitleController_->LoadSettings();
            subtitleController_->UpdateStyle();
            subtitleController_->UpdateTrackButton();
        }

        if (featureManager_)
            featureManager_->LoadDetectorSettings();

        if (networkController_)
        {
            networkController_->LoadSettings();
            networkController_->RefreshStatsPanel();
        }

        if (monitorWallController_)
            monitorWallController_->LoadSettings();
    }

    void SaveShutdownState() const
    {
        if (playbackController_)
            playbackController_->SavePlaybackState();

        if (playlistManager_ && configService_)
            configService_->SavePlaylistState(playlistManager_->SaveState());

        if (subtitleController_)
            subtitleController_->SaveSettings();
        if (featureManager_)
            featureManager_->SaveDetectorSettings();
        if (networkController_)
            networkController_->SaveSettings();
        if (monitorWallController_)
            monitorWallController_->SaveSettings();
    }

    void ResetPerMediaUiState() const
    {
        if (featureManager_)
            featureManager_->ResetForMedia();
        if (subtitleController_)
            subtitleController_->ResetForMedia();
    }

private:
    PlaybackController* playbackController_ = nullptr;
    PlaylistManager* playlistManager_ = nullptr;
    SubtitleController* subtitleController_ = nullptr;
    FeatureManager* featureManager_ = nullptr;
    NetworkController* networkController_ = nullptr;
    MonitorWallController* monitorWallController_ = nullptr;
    ConfigService* configService_ = nullptr;
};

MyPlayer::MyPlayer(QWidget* parent)
    : QWidget(parent)
    , ui(new Ui::MyPlayerClass())
    , playbackService(std::make_unique<PlaybackService>())
    , configService(std::make_unique<ConfigService>())
{

    playbackSession = playbackService.get();
    playbackTracks = playbackService.get();
    playbackStats = playbackService.get();
    playbackFeatures = playbackService.get();
    playbackArchive = playbackService.get();

    ui->setupUi(this);
    ui->playPos->setValue(0);

    connect(ui->openFile, SIGNAL(clicked()), this, SLOT(OpenFile()));
    connect(ui->isplay, SIGNAL(clicked()), this, SLOT(PlayOrPause()));
    connect(ui->playPos, SIGNAL(sliderPressed()), this, SLOT(SliderPress()));
    connect(ui->playPos, SIGNAL(sliderReleased()), this, SLOT(SliderRelease()));
    connect(ui->volumeSlider, SIGNAL(valueChanged(int)), this, SLOT(VolumeChanged(int)));
    connect(ui->speedSlider, SIGNAL(valueChanged(int)), this, SLOT(SpeedChanged(int)));

    connect(ui->openUrl, SIGNAL(clicked()), this, SLOT(OpenUrl()));
    connect(ui->urlInput, SIGNAL(returnPressed()), this, SLOT(OpenUrl()));

    ui->volumeSlider->setValue(configService ? configService->LoadVolume() : 50);

    VolumeChanged(ui->volumeSlider->value());
    SpeedChanged(ui->speedSlider->value());

    const QList<QWidget*> baseAutoHideWidgets = {
        ui->openFile, ui->isplay, ui->speedSlider, ui->speedLabel,
        ui->timeLabel, ui->playPos, ui->urlInput, ui->openUrl,
        ui->liveLabel, ui->volumeSlider
    };

    for (QWidget* w : baseAutoHideWidgets)
        AttachAutoHideWidget(autoHideWidgets, controlEffects, w);

    setMouseTracking(true);
    if (ui->video)
        ui->video->setMouseTracking(true);

    setFocusPolicy(Qt::StrongFocus);
    if (ui->video)
        ui->video->setFocusPolicy(Qt::StrongFocus);

    for (QWidget* child : findChildren<QWidget*>())
        child->installEventFilter(this);

    hideTimer = kControlAutoHideTicks;

    setAcceptDrops(true);

    const MyPlayerShellWidgets shellWidgets = BuildMyPlayerShellWidgets(this, ui);
    sidebarBg = shellWidgets.sidebarBackground;
    playlist = shellWidgets.playlist;
    playlistBtn = shellWidgets.playlistButton;
    colorBtn = shellWidgets.colorButton;
    audioTrackBtn = shellWidgets.audioTrackButton;
    playModeBtn = shellWidgets.playModeButton;
    addFolderBtn = shellWidgets.addFolderButton;
    clearListBtn = shellWidgets.clearListButton;
    subtitleTrackBtn = shellWidgets.subtitleTrackButton;
    subtitleOsdLabel = shellWidgets.subtitleOsdLabel;

    connect(audioTrackBtn, &QPushButton::clicked, this, [this]() {
        if (!audioTrackBtn) return;

        ShowAudioTrackMenu(QPoint(audioTrackBtn->width() / 2, audioTrackBtn->height()));
        });
    connect(audioTrackBtn, &QPushButton::customContextMenuRequested, this, &MyPlayer::ShowAudioTrackMenu);
    AttachAutoHideWidget(autoHideWidgets, controlEffects, audioTrackBtn);
    audioTrackBtn->installEventFilter(this);

    playlistManager = std::make_unique<PlaylistManager>(
        this, playlist, sidebarBg, playlistBtn, playModeBtn, addFolderBtn, clearListBtn);
    playlistManager->SetPlayRequestedHandler(
        [this](const QString& path) { PlayFile(path); });
    playlistManager->SetLayoutChangedHandler(
        [this]() { resizeEvent(nullptr); });
    AttachAutoHideWidget(autoHideWidgets, controlEffects, playlistBtn);
    AttachAutoHideWidget(autoHideWidgets, controlEffects, colorBtn);

    playlistBtn->installEventFilter(this);
    colorBtn->installEventFilter(this);
    playlist->installEventFilter(this);
    playModeBtn->installEventFilter(this);
    addFolderBtn->installEventFilter(this);
    clearListBtn->installEventFilter(this);
    sidebarBg->installEventFilter(this);

    connect(subtitleTrackBtn, &QPushButton::clicked, this, &MyPlayer::CycleSubtitleTrack);
    connect(subtitleTrackBtn, &QPushButton::customContextMenuRequested, this, &MyPlayer::ShowSubtitleMenu);
    AttachAutoHideWidget(autoHideWidgets, controlEffects, subtitleTrackBtn);
    subtitleTrackBtn->installEventFilter(this);

    subtitleController = std::make_unique<SubtitleController>(
        std::make_unique<SubtitleViewQt>(
            this, ui->video, subtitleTrackBtn, subtitleOsdLabel,
            [this]() { return VideoAreaWidth(); }),
        playbackSession,
        playbackTracks,
        configService.get(),
        [this]() { return featureManager && featureManager->IsAsrEnabled(); },
        [this]() { ToggleASR(); },
        [this]() { return currentFilePath; });

    featureManager = std::make_unique<FeatureManager>(
        std::make_unique<FeatureViewQt>(this, ui->video),
        playbackFeatures,
        playbackArchive,
        configService.get(),
        subtitleController.get(),
        [this]() { return VideoAreaWidth(); },
        [this]() { return height(); });

    for (QWidget* widget : featureManager->AutoHideWidgets())
        AttachAutoHideWidget(autoHideWidgets, controlEffects, widget);
    featureManager->InstallEventFilters(this);

    auto networkView = std::make_unique<NetworkViewQt>(this);
    QLabel* networkStatsLabel = networkView->StatsLabel();
    networkController = std::make_unique<NetworkController>(
        std::move(networkView), playbackSession, configService.get(), nullptr);
    networkController->SetApplyHandler([this]() { ApplyNetworkSettings(); });
    networkController->SetLayoutChangedHandler([this]() { resizeEvent(nullptr); });

    for (QWidget* widget : networkController->AutoHideWidgets())
        AttachAutoHideWidget(autoHideWidgets, controlEffects, widget);
    networkController->InstallEventFilters(this);

    playbackStatsController = std::make_unique<PlaybackStatsController>(
        playbackSession, playbackStats,
        std::make_unique<StatsViewQt>(networkStatsLabel, ui->video));
    networkController->SetStatsController(playbackStatsController.get());

    monitorWallController = std::make_unique<MonitorWallController>(
        this, configService.get(), [this](const QString& path) { OpenMediaPath(path); });
    for (QWidget* widget : monitorWallController->AutoHideWidgets())
        AttachAutoHideWidget(autoHideWidgets, controlEffects, widget);
    monitorWallController->InstallEventFilters(this);

    PlaybackControllerCallbacks playbackCallbacks;
    playbackCallbacks.resetPerMediaFeatures = [this]() {
        if (shellStateCoordinator) shellStateCoordinator->ResetPerMediaUiState();
        };
    playbackCallbacks.resetDebugOsd = [this]() {
        if (playbackStatsController) playbackStatsController->ResetDebugOsd();
        };

    playbackCallbacks.updateSubtitleTrackButton = [this]() { UpdateSubtitleTrackButton(); };
    playbackCallbacks.updateSubtitleDisplay = [this]() { UpdateSubtitleDisplay(); };
    playbackCallbacks.autoLoadEmbeddedSubtitle = [this](const QString& path) { AutoLoadEmbeddedSubtitle(path); };
    playbackCallbacks.autoLoadExternalSubtitle = [this](const QString& path) { AutoLoadExternalSubtitle(path); };
    playbackCallbacks.showSubtitleOsd = [this](const QString& text) { ShowSubtitleOsd(text); };
    playbackCallbacks.resetHideTimer = [this]() { ResetHideTimer(); };
    playbackCallbacks.controlsShownProvider = [this]() { return controlsShown; };

    playbackController = std::make_unique<PlaybackController>(
        std::make_unique<PlaybackViewQt>(this, ui, audioTrackBtn),
        playbackSession, playbackTracks, playbackFeatures,
        playlistManager.get(), subtitleController.get(), featureManager.get(),
        networkController.get(), configService.get(),
        isSliderPress, eofHandled, currentFilePath,
        std::move(playbackCallbacks));

    displayAdjustmentController = std::make_unique<DisplayAdjustmentController>(
        this, ui, [this]() { return VideoAreaWidth(); }, [this]() { return height(); });

    for (QWidget* widget : displayAdjustmentController->AutoHideWidgets())
        AttachAutoHideWidget(autoHideWidgets, controlEffects, widget);
    displayAdjustmentController->InstallEventFilters(this);

    if (colorBtn)
    {
        colorBtn->setToolTip("Show brightness, contrast and saturation controls.");
        connect(colorBtn, &QPushButton::toggled, this, [this](bool checked) {
            if (!displayAdjustmentController) return;
            displayAdjustmentController->SetPanelVisible(checked);
            displayAdjustmentController->LayoutPanel(colorBtn ? colorBtn->geometry() : QRect{});
            });
        colorBtn->setChecked(displayAdjustmentController->IsPanelVisible());
        displayAdjustmentController->LayoutPanel(colorBtn->geometry());
    }

    shellStateCoordinator = std::make_unique<MyPlayerShellStateCoordinator>(
        playbackController.get(), playlistManager.get(), subtitleController.get(),
        featureManager.get(), networkController.get(),
        monitorWallController.get(), configService.get());

    playerChromeController = std::make_unique<PlayerChromeController>(
        this, ui, playbackSession, playbackTracks, playbackFeatures,
        playlistManager.get(), subtitleController.get(), featureManager.get(),
        playbackController.get(), networkController.get(),
        monitorWallController.get(), playlist, sidebarBg, playlistBtn, colorBtn,
        audioTrackBtn, subtitleTrackBtn, playModeBtn, addFolderBtn, clearListBtn,
        autoHideWidgets, controlEffects, controlsShown, hideTimer, sidebarResizing,
        eofHandled, currentFilePath,
        [this]() { return VideoAreaWidth(); },
        [this]() { return PlaylistSidebarWidth(); },
        [this]() { return IsPlaylistVisible(); },
        [this]() { if (playbackStatsController) playbackStatsController->ToggleDebugOsd(); },
        [this]() { if (displayAdjustmentController) displayAdjustmentController->TakeScreenshot(); });

    if (shellStateCoordinator)
        shellStateCoordinator->LoadStartupState();

    if (playbackSession)
        playbackSession->Start();

    startTimer(40);
}

MyPlayer::~MyPlayer()
{
    if (shellStateCoordinator)
        shellStateCoordinator->SaveShutdownState();
    if (playbackSession)
        playbackSession->Close();
    delete ui;
}

void MyPlayer::OpenMediaPath(const QString& path)
{
    QWidget* topLevel = window();
    if (!topLevel)
        topLevel = this;

    if (topLevel)
    {
        if (topLevel->isMinimized())
            topLevel->showNormal();
        else if (!topLevel->isVisible())
            topLevel->show();
        topLevel->raise();
        topLevel->activateWindow();
    }

    if (playbackController)
        playbackController->OpenMediaPath(path);
}

bool MyPlayer::IsPlaylistVisible() const
{
    return playlistManager && playlistManager->IsSidebarVisible();
}

int MyPlayer::PlaylistSidebarWidth() const
{
    return playlistManager ? playlistManager->SidebarWidth() : 250;
}

int MyPlayer::VideoAreaWidth() const
{

    return IsPlaylistVisible() ? (this->width() - PlaylistSidebarWidth()) : this->width();
}

void MyPlayer::SetStartupDetectorEnabled(bool enabled)
{
    if (featureManager)
        featureManager->SetDetectorEnabled(enabled);
}

PlaybackSessionSnapshot MyPlayer::GetPlaybackSessionSnapshot()
{
    return playbackSession ? playbackSession->GetSessionSnapshot() : PlaybackSessionSnapshot{};
}

PlaybackMediaSnapshot MyPlayer::GetPlaybackMediaSnapshot()
{
    return playbackStats ? playbackStats->GetMediaSnapshot() : PlaybackMediaSnapshot{};
}

StreamStatsSnapshot MyPlayer::GetPlaybackStatsSnapshot()
{
    return playbackStats ? playbackStats->GetStreamStats() : StreamStatsSnapshot{};
}

void MyPlayer::ToggleASR()
{
    if (featureManager)
        featureManager->ToggleASR();
}

void MyPlayer::ToggleAnime4K()
{
    if (featureManager)
        featureManager->ToggleAnime4K();
}

void MyPlayer::ToggleDetector()
{
    if (featureManager)
        featureManager->ToggleDetector();
}

void MyPlayer::ShowDetectorMenu(const QPoint& pos)
{
    if (featureManager)
        featureManager->ShowDetectorMenu(pos);
}

void MyPlayer::ResetHideTimer()
{
    if (playerChromeController)
        playerChromeController->ResetHideTimer();
}

void MyPlayer::ShowControls(bool show)
{
    if (playerChromeController)
        playerChromeController->ShowControls(show);
}

void MyPlayer::resizeEvent(QResizeEvent* e)
{
    Q_UNUSED(e);
    if (playerChromeController)
        playerChromeController->ResizeLayout();
    if (displayAdjustmentController)
        displayAdjustmentController->LayoutPanel(colorBtn ? colorBtn->geometry() : QRect{});
}

void MyPlayer::timerEvent(QTimerEvent* e)
{
    Q_UNUSED(e);

    if (playbackController)
        playbackController->TickUi();

    const bool hasMedia = !currentFilePath.isEmpty();
    const PlaybackSessionSnapshot sessionSnapshot = playbackSession
        ? playbackSession->GetSessionSnapshot()
        : PlaybackSessionSnapshot{};
    const bool isPlaying = playbackSession && !sessionSnapshot.isPaused && hasMedia;
    if (playerChromeController)
        playerChromeController->TickUi(isPlaying, isSliderPress);
    if (subtitleController)
        subtitleController->TickUi();

    if (featureManager)
        featureManager->TickOsd();

    if (displayAdjustmentController)
        displayAdjustmentController->TickUi();

    if (playbackStatsController)
    {
        playbackStatsController->TickDebugOsd();
        const QString statusText = playbackStatsController->TakeStatusEventText();
        if (!statusText.isEmpty())
            ShowSubtitleOsd(statusText);
    }

    if (networkController)
        networkController->TickUi();
}

void MyPlayer::ApplyNetworkSettings()
{
    const StreamOpenOptions options = networkController
        ? networkController->BuildOpenOptionsFromUi()
        : StreamOpenOptions::DefaultNetwork();
    if (playbackSession)
        playbackSession->SetNetworkOpenOptions(options);
    if (networkController)
    {
        networkController->SaveSettings();
        networkController->RefreshStatsPanel();
    }

    const PlaybackSessionSnapshot snapshot = playbackSession
        ? playbackSession->GetSessionSnapshot()
        : PlaybackSessionSnapshot{};
    const std::string activeUrl = snapshot.currentUrl;
    if (!activeUrl.empty() && IsNetworkUrl(activeUrl))
    {
        BeginNetworkOpenAsync(QString::fromStdString(activeUrl), options,
            false, false, "Network settings applied | stream reopened", "Network");
        return;
    }

    ShowSubtitleOsd("Network settings saved");
}

void MyPlayer::CancelPendingNetworkOpen()
{
    if (playbackController)
        playbackController->CancelPendingNetworkOpen();
}

void MyPlayer::BeginNetworkOpenAsync(const QString& url, const StreamOpenOptions& options,
    bool restorePlaybackState, bool forceResumePlayback,
    const QString& successOsd, const QString& errorTitle)
{
    if (playbackController)
    {
        playbackController->BeginNetworkOpenAsync(url, options, restorePlaybackState,
            forceResumePlayback, successOsd, errorTitle);
    }
}

void MyPlayer::FinishNetworkOpen(quint64 requestId, Demux* preparedDemux, const QString& url,
    const StreamOpenOptions& options, int openLatencyMs, bool restorePlaybackState,
    bool forceResumePlayback, const QString& successOsd, const QString& errorTitle,
    const QString& openError)
{
    if (playbackController)
    {
        playbackController->FinishNetworkOpen(requestId, preparedDemux, url, options,
            openLatencyMs, restorePlaybackState, forceResumePlayback,
            successOsd, errorTitle, openError);
    }
}

void MyPlayer::PlayOrPause()
{
    if (playbackController)
        playbackController->PlayOrPause();
}

void MyPlayer::SetPause(bool isPause)
{
    if (playbackController)
        playbackController->SetPause(isPause);
}

void MyPlayer::VolumeChanged(int value)
{
    if (playbackController)
        playbackController->VolumeChanged(value);
}

void MyPlayer::SpeedChanged(int value)
{
    if (playbackController)
        playbackController->SpeedChanged(value);
}

void MyPlayer::OpenFile()
{
    if (playbackController)
        playbackController->OpenFile();
}

void MyPlayer::PlayFile(const QString& path)
{
    if (playbackController)
        playbackController->PlayFile(path);
}

bool MyPlayer::PlayNext()
{
    return playbackController ? playbackController->PlayNext() : false;
}

void MyPlayer::AddToPlaylist(const QStringList& files)
{
    if (playlistManager)
        playlistManager->AddFiles(files);
}

void MyPlayer::TogglePlaylist()
{
    if (playlistManager)
        playlistManager->ToggleSidebar();
}

void MyPlayer::CycleAudioTrack()
{
    if (playbackController)
        playbackController->CycleAudioTrack();
}

void MyPlayer::SelectAudioTrack(int idx)
{
    if (playbackController)
        playbackController->SelectAudioTrack(idx);
}

void MyPlayer::UpdateAudioTrackBtn()
{
    if (playbackController)
        playbackController->UpdateAudioTrackButton();
}

void MyPlayer::ShowAudioTrackMenu(const QPoint& pos)
{
    if (playbackController)
        playbackController->ShowAudioTrackMenu(pos);
}

void MyPlayer::OpenUrl()
{
    if (playbackController)
        playbackController->OpenUrl();
}

void MyPlayer::UpdateStreamUI(bool isLive)
{
    if (playbackController)
        playbackController->UpdateStreamUI(isLive);
}
