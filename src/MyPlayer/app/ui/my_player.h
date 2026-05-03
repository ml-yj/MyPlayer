#pragma once

#include <QtWidgets/QWidget>
#include <QList>
#include <QPoint>
#include <QString>
#include <QStringList>
#include <QtGlobal>
#include <memory>

#include "../../core/session/stream_config.h"

struct StreamOpenOptions;
class Demux;
class IPlaybackFeatureService;
class IPlaybackSessionService;
class IPlaybackStatsService;
class IPlaybackTrackService;
class IPlaybackArchiveService;
class ConfigService;
class PlaybackService;
class SubtitleController;
class FeatureManager;
class PlaybackController;
class PlayerChromeController;
class PlaybackStatsController;
class NetworkController;
class MonitorWallController;
class PlaylistManager;
class DisplayAdjustmentController;
class MyPlayerShellStateCoordinator;

class QGraphicsOpacityEffect;
class QLabel;
class QCheckBox;
class QComboBox;
class QSpinBox;
class QPushButton;
class QListWidget;

QT_BEGIN_NAMESPACE
namespace Ui { class MyPlayerClass; }
QT_END_NAMESPACE

class MyPlayer : public QWidget
{
    Q_OBJECT

public:

    explicit MyPlayer(QWidget* parent = nullptr);

    ~MyPlayer();

    void OpenMediaPath(const QString& path);
    void SetStartupDetectorEnabled(bool enabled);

    PlaybackSessionSnapshot GetPlaybackSessionSnapshot();
    PlaybackMediaSnapshot GetPlaybackMediaSnapshot();
    StreamStatsSnapshot GetPlaybackStatsSnapshot();

    void timerEvent(QTimerEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    bool eventFilter(QObject* obj, QEvent* e) override;
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dropEvent(QDropEvent* e) override;

    void SetPause(bool isPause);

public slots:

    void OpenFile();
    void PlayOrPause();
    void SliderPress();
    void SliderRelease();
    void VolumeChanged(int value);
    void SpeedChanged(int value);
    void OpenUrl();

private:

    void UpdateStreamUI(bool isLive);
    void ShowControls(bool show);
    void ResetHideTimer();

    void ApplyNetworkSettings();
    void BeginNetworkOpenAsync(const QString& url, const StreamOpenOptions& options,
        bool restorePlaybackState, bool forceResumePlayback,
        const QString& successOsd, const QString& errorTitle);
    void FinishNetworkOpen(quint64 requestId, Demux* preparedDemux, const QString& url,
        const StreamOpenOptions& options, int openLatencyMs, bool restorePlaybackState,
        bool forceResumePlayback, const QString& successOsd, const QString& errorTitle,
        const QString& openError);
    void CancelPendingNetworkOpen();

    void PlayFile(const QString& path);
    bool PlayNext();
    void AddToPlaylist(const QStringList& files);
    void TogglePlaylist();

    void CycleAudioTrack();
    void SelectAudioTrack(int idx);
    void UpdateAudioTrackBtn();
    void ShowAudioTrackMenu(const QPoint& pos);

    void ClearSubtitleState();
    void UpdateSubtitleDisplay();
    void UpdateSubtitleStyle();
    void SyncSubtitleRenderer();
    void UpdateSubtitleTrackButton();
    void ShowSubtitleOsd(const QString& text);
    void AutoLoadEmbeddedSubtitle(const QString& mediaPath);
    void AutoLoadExternalSubtitle(const QString& mediaPath);
    void OpenSubtitleFile();
    void CycleSubtitleTrack();
    void ShowSubtitleMenu(const QPoint& pos);
    void AdjustSubtitleOffset(int deltaMs);
    void AdjustSubtitleFontSize(int deltaPt);
    void AdjustSubtitleBottomMargin(int deltaPx);
    void ExportAsrSubtitle();
    void ToggleASR();
    void OnSubtitleReady(
        const QString& text, long long startMs, long long endMs, quint64 generation, quint64 serial);

    void ToggleAnime4K();
    void ToggleDetector();
    void ShowDetectorMenu(const QPoint& pos);

private:

    bool IsPlaylistVisible() const;
    int PlaylistSidebarWidth() const;
    int VideoAreaWidth() const;

    Ui::MyPlayerClass* ui = nullptr;
    bool isSliderPress = false;

    std::unique_ptr<PlaybackService> playbackService;
    std::unique_ptr<ConfigService> configService;

    IPlaybackSessionService* playbackSession = nullptr;
    IPlaybackTrackService* playbackTracks = nullptr;
    IPlaybackStatsService* playbackStats = nullptr;
    IPlaybackFeatureService* playbackFeatures = nullptr;
    IPlaybackArchiveService* playbackArchive = nullptr;

    bool eofHandled = false;
    QString currentFilePath;

    QList<QWidget*> autoHideWidgets;
    QList<QGraphicsOpacityEffect*> controlEffects;
    int hideTimer = 0;
    bool controlsShown = true;

    QListWidget* playlist = nullptr;
    QWidget* sidebarBg = nullptr;
    QPushButton* playlistBtn = nullptr;
    QPushButton* colorBtn = nullptr;
    QPushButton* audioTrackBtn = nullptr;
    QPushButton* playModeBtn = nullptr;
    QPushButton* addFolderBtn = nullptr;
    QPushButton* clearListBtn = nullptr;
    bool sidebarResizing = false;

    std::unique_ptr<PlaylistManager> playlistManager;
    std::unique_ptr<SubtitleController> subtitleController;
    QPushButton* subtitleTrackBtn = nullptr;
    QLabel* subtitleOsdLabel = nullptr;

    std::unique_ptr<FeatureManager> featureManager;
    std::unique_ptr<NetworkController> networkController;
    std::unique_ptr<MonitorWallController> monitorWallController;

    std::unique_ptr<PlaybackController> playbackController;
    std::unique_ptr<PlayerChromeController> playerChromeController;
    std::unique_ptr<PlaybackStatsController> playbackStatsController;
    std::unique_ptr<DisplayAdjustmentController> displayAdjustmentController;
    std::unique_ptr<MyPlayerShellStateCoordinator> shellStateCoordinator;
};
