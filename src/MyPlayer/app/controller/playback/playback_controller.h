#pragma once

#include <QPoint>
#include <QString>
#include <functional>
#include <memory>

class Demux;
class IPlaybackView;
class IPlaybackFeatureService;
class IPlaybackSessionService;
class IPlaybackTrackService;
class PlaylistManager;
class SubtitleController;
class FeatureManager;
class NetworkController;
class ConfigService;
struct StreamOpenOptions;

struct PlaybackControllerCallbacks
{
    std::function<void()> resetPerMediaFeatures;
    std::function<void()> resetDebugOsd;
    std::function<void()> updateSubtitleTrackButton;
    std::function<void()> updateSubtitleDisplay;
    std::function<void(const QString&)> autoLoadEmbeddedSubtitle;
    std::function<void(const QString&)> autoLoadExternalSubtitle;
    std::function<void(const QString&)> showSubtitleOsd;
    std::function<void()> resetHideTimer;
    std::function<bool()> controlsShownProvider;
};

class PlaybackController
{
public:

    PlaybackController(std::unique_ptr<IPlaybackView> view,
        IPlaybackSessionService* sessionService,
        IPlaybackTrackService* trackService,
        IPlaybackFeatureService* featureService,
        PlaylistManager* playlistManager,
        SubtitleController* subtitleController,
        FeatureManager* featureManager,
        NetworkController* networkController,
        ConfigService* configService,
        bool& isSliderPress,
        bool& eofHandled,
        QString& currentFilePath,
        PlaybackControllerCallbacks callbacks);

    void OpenMediaPath(const QString& path);
    void SliderPress();
    void SliderRelease();
    void PlayOrPause();
    void SetPause(bool isPause);
    void VolumeChanged(int value);
    void SpeedChanged(int value);

    void OpenFile();
    void PlayFile(const QString& path);
    bool PlayNext();
    void OpenUrl();

    void UpdateStreamUI(bool isLive);
    void SavePlaybackState();
    void RestorePlaybackState();
    void TickUi();

    void CycleAudioTrack();
    void SelectAudioTrack(int idx);
    void UpdateAudioTrackButton();
    void ShowAudioTrackMenu(const QPoint& pos);

    void CancelPendingNetworkOpen();

    void BeginNetworkOpenAsync(const QString& url, const StreamOpenOptions& options,
        bool restorePlaybackState, bool forceResumePlayback,
        const QString& successOsd, const QString& errorTitle);

    void FinishNetworkOpen(quint64 requestId, Demux* preparedDemux, const QString& url,
        const StreamOpenOptions& options, int openLatencyMs, bool restorePlaybackState,
        bool forceResumePlayback, const QString& successOsd, const QString& errorTitle,
        const QString& openError);

private:
    void ApplyLiveSpeedPolicy(bool isLive);

    std::unique_ptr<IPlaybackView> view;

    IPlaybackSessionService* session = nullptr;
    IPlaybackTrackService* tracks = nullptr;
    IPlaybackFeatureService* features = nullptr;
    PlaylistManager* playlistManager = nullptr;
    SubtitleController* subtitleController = nullptr;
    FeatureManager* featureManager = nullptr;
    NetworkController* networkController = nullptr;
    ConfigService* config = nullptr;

    bool& isSliderPress;
    bool& eofHandled;
    QString& currentFilePath;

    PlaybackControllerCallbacks callbacks;
    int saveCounter = 0;
    bool syncingSpeedUi = false;
};
