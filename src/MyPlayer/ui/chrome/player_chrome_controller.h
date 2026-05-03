#pragma once

#include <QList>
#include <QString>
#include <functional>

class QWidget;
class QObject;
class QEvent;
class QMouseEvent;
class QKeyEvent;
class QDragEnterEvent;
class QDropEvent;
class QPushButton;
class QListWidget;
class QGraphicsOpacityEffect;

class IPlaybackFeatureService;
class IPlaybackSessionService;
class IPlaybackTrackService;
class PlaylistManager;
class SubtitleController;
class FeatureManager;
class PlaybackController;
class NetworkController;
class MonitorWallController;

namespace Ui { class MyPlayerClass; }

class PlayerChromeController
{
public:
    PlayerChromeController(
        QWidget* host,
        Ui::MyPlayerClass* ui,
        IPlaybackSessionService* sessionService,
        IPlaybackTrackService* trackService,
        IPlaybackFeatureService* featureService,
        PlaylistManager* playlistManager,
        SubtitleController* subtitleController,
        FeatureManager* featureManager,
        PlaybackController* playbackController,
        NetworkController* networkController,
        MonitorWallController* monitorWallController,
        QListWidget* playlistWidget,
        QWidget* sidebarBackground,
        QPushButton* playlistButton,
        QPushButton* colorButton,
        QPushButton* audioTrackButton,
        QPushButton* subtitleTrackButton,
        QPushButton* playModeButton,
        QPushButton* addFolderButton,
        QPushButton* clearListButton,
        QList<QWidget*>& autoHideWidgets,
        QList<QGraphicsOpacityEffect*>& controlEffects,
        bool& controlsShown,
        int& hideTimer,
        bool& sidebarResizing,
        bool& eofHandled,
        QString& currentFilePath,
        std::function<int()> videoAreaWidthProvider,
        std::function<int()> sidebarWidthProvider,
        std::function<bool()> playlistVisibleProvider,
        std::function<void()> infoOsdToggleHandler,
        std::function<void()> screenshotHandler);

    void ResetHideTimer();
    void ShowControls(bool show);
    void ResizeLayout();
    void TickUi(bool isPlaying, bool isSliderPress);

    bool HandleMouseMove(QMouseEvent* event);
    bool HandleMousePress(QMouseEvent* event);
    void HandleMouseRelease();
    void HandleMouseDoubleClick();
    bool HandleKeyPress(QKeyEvent* event);
    bool HandleEventFilter(QObject* obj, QEvent* event);
    bool HandleDragEnter(QDragEnterEvent* event);
    bool HandleDrop(QDropEvent* event);

private:
    QWidget* host = nullptr;
    Ui::MyPlayerClass* ui = nullptr;
    IPlaybackSessionService* session = nullptr;
    IPlaybackTrackService* tracks = nullptr;
    IPlaybackFeatureService* features = nullptr;
    PlaylistManager* playlistManager = nullptr;
    SubtitleController* subtitleController = nullptr;
    FeatureManager* featureManager = nullptr;
    PlaybackController* playbackController = nullptr;
    NetworkController* networkController = nullptr;
    MonitorWallController* monitorWallController = nullptr;
    QListWidget* playlistWidget = nullptr;
    QWidget* sidebarBackground = nullptr;
    QPushButton* playlistButton = nullptr;
    QPushButton* colorButton = nullptr;
    QPushButton* audioTrackButton = nullptr;
    QPushButton* subtitleTrackButton = nullptr;
    QPushButton* playModeButton = nullptr;
    QPushButton* addFolderButton = nullptr;
    QPushButton* clearListButton = nullptr;
    QList<QWidget*>& autoHideWidgets;
    QList<QGraphicsOpacityEffect*>& controlEffects;
    bool& controlsShown;
    int& hideTimer;
    bool& sidebarResizing;
    bool& eofHandled;
    QString& currentFilePath;
    std::function<int()> videoAreaWidthProvider;
    std::function<int()> sidebarWidthProvider;
    std::function<bool()> playlistVisibleProvider;
    std::function<void()> infoOsdToggleHandler;
    std::function<void()> screenshotHandler;
};
