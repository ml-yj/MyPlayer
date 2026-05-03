

#include "playback_controller.h"

#include "../../service/config_service.h"
#include "../../service/playback_service_interfaces.h"
#include "../../view/playback_view_qt.h"
#include "../../view/qt_ui_theme.h"
#include "../feature/feature_manager.h"
#include "../network/network_controller.h"
#include "../playlist/playlist_manager.h"
#include "../subtitle/subtitle_controller.h"
#include "../../../common/diagnostics/logger.h"
#include "../../../common/util.h"
#include "../../../core/media/demux.h"
#include "../../../ui/video/video_widget.h"

#include <QByteArray>
#include <QDir>
#include <QFileInfo>
#include <QMenu>
#include <QMetaObject>
#include <QPointer>
#include <QWidget>

#include <algorithm>
#include <chrono>
#include <memory>
#include <thread>

#include <utility>

PlaybackController::PlaybackController(std::unique_ptr<IPlaybackView> viewValue,
    IPlaybackSessionService* sessionService,
    IPlaybackTrackService* trackService,
    IPlaybackFeatureService* featureService,
    PlaylistManager* playlistManagerPtr,
    SubtitleController* subtitleControllerPtr,
    FeatureManager* featureManagerPtr,
    NetworkController* networkControllerPtr,
    ConfigService* configService,
    bool& isSliderPressRef,
    bool& eofHandledRef,
    QString& currentFilePathRef,
    PlaybackControllerCallbacks callbacksValue)
    : view(std::move(viewValue))
    , session(sessionService)
    , tracks(trackService)
    , features(featureService)
    , playlistManager(playlistManagerPtr)
    , subtitleController(subtitleControllerPtr)
    , featureManager(featureManagerPtr)
    , networkController(networkControllerPtr)
    , config(configService)
    , isSliderPress(isSliderPressRef)
    , eofHandled(eofHandledRef)
    , currentFilePath(currentFilePathRef)
    , callbacks(std::move(callbacksValue))
{
}

void PlaybackController::CancelPendingNetworkOpen()
{
    if (networkController)
        networkController->CancelPendingOpen();
}

void PlaybackController::BeginNetworkOpenAsync(const QString& url, const StreamOpenOptions& options,
    bool restorePlaybackState, bool forceResumePlayback,
    const QString& successOsd, const QString& errorTitle)
{
    if (!view)
        return;

    const QString trimmed = url.trimmed();
    if (trimmed.isEmpty())
        return;

    const quint64 requestId = networkController
        ? networkController->BeginPendingOpen(trimmed, options)
        : 0;
    if (networkController)
        networkController->RefreshStatsPanel();
    if (callbacks.showSubtitleOsd)
        callbacks.showSubtitleOsd(QString("Opening network stream...\n%1").arg(trimmed.left(96)));

    struct PendingNetworkOpenResult
    {
        std::unique_ptr<Demux> demux;
        StreamOpenOptions effectiveOptions = StreamOpenOptions::DefaultNetwork();
        int openLatencyMs = 0;
        QString openError;
    };

    const QPointer<QWidget> guardHost(view->HostWidget());
    std::thread([this, guardHost, requestId, trimmed, options,
                 restorePlaybackState, forceResumePlayback, successOsd, errorTitle]()
    {
        auto result = std::make_shared<PendingNetworkOpenResult>();
        const QByteArray urlBytes = trimmed.toUtf8();
        const auto openStart = std::chrono::steady_clock::now();
        result->demux = std::make_unique<Demux>();
        const bool ok = result->demux->Open(urlBytes.constData(), options);
        const auto openEnd = std::chrono::steady_clock::now();
        result->openLatencyMs = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(openEnd - openStart).count());
        if (!ok)
        {
            result->openError = QString::fromStdString(result->demux->GetLastError());
            if (result->openError.isEmpty())
                result->openError = QStringLiteral("Failed to connect to the network stream.");
            result->demux.reset();
        }
        else
        {
            result->effectiveOptions = result->demux->GetOpenOptions();
        }

        if (!guardHost)
            return;

        QMetaObject::invokeMethod(
            guardHost.data(),
            [this, guardHost, result, requestId, trimmed, options,
             restorePlaybackState, forceResumePlayback, successOsd, errorTitle]() mutable
            {
                if (!guardHost)
                    return;

                FinishNetworkOpen(requestId, result->demux.release(), trimmed, result->effectiveOptions,
                    result->openLatencyMs, restorePlaybackState, forceResumePlayback,
                    successOsd, errorTitle, result->openError);
            },
            Qt::QueuedConnection);
    }).detach();
}

void PlaybackController::FinishNetworkOpen(quint64 requestId, Demux* preparedDemux, const QString& url,
    const StreamOpenOptions& options, int openLatencyMs, bool restorePlaybackState,
    bool forceResumePlayback, const QString& successOsd, const QString& errorTitle,
    const QString& openError)
{
    std::unique_ptr<Demux> demuxHolder(preparedDemux);
    if (networkController && !networkController->MatchesPendingRequest(requestId))
        return;

    if (networkController)
        networkController->CompletePendingOpen();

    VideoWidget* videoWidget = view ? view->VideoWidgetHandle() : nullptr;
    if (!view || !session || !videoWidget)
    {
        if (networkController)
            networkController->RefreshStatsPanel();
        if (view)
        {
            view->ShowWarningMessage(
                errorTitle.isEmpty() ? QStringLiteral("Error") : errorTitle,
                QStringLiteral("Network open finished, but the video surface is unavailable."));
        }
        return;
    }

    if (!demuxHolder)
    {
        if (networkController)
            networkController->RefreshStatsPanel();
        view->ShowWarningMessage(
            errorTitle.isEmpty() ? QStringLiteral("Error") : errorTitle,
            QString("Failed to connect:\n%1").arg(openError));
        return;
    }

    const QByteArray urlBytes = url.toUtf8();
    if (!session->OpenPrepared(demuxHolder.release(), urlBytes.constData(), options, videoWidget, openLatencyMs))
    {
        if (networkController)
            networkController->RefreshStatsPanel();
        view->ShowWarningMessage(
            errorTitle.isEmpty() ? QStringLiteral("Error") : errorTitle,
            QString("Failed to open stream:\n%1")
                .arg(QString::fromStdString(session->GetSessionSnapshot().lastError)));
        return;
    }

    view->SetWindowTitleText(url);
    currentFilePath = url;
    if (subtitleController)
        subtitleController->SetEpoch(features ? features->GetFeatureEpoch(AiCapability::Asr) : StreamEpoch{});
    if (callbacks.updateSubtitleTrackButton)
        callbacks.updateSubtitleTrackButton();
    if (forceResumePlayback)
    {
        session->SetPause(false);
        SetPause(false);
    }
    else
    {
        SetPause(session->GetSessionSnapshot().isPaused);
    }
    if (callbacks.resetHideTimer)
        callbacks.resetHideTimer();
    UpdateStreamUI(session->GetSessionSnapshot().isLiveStream);
    UpdateAudioTrackButton();
    if (restorePlaybackState)
        RestorePlaybackState();
    if (networkController)
        networkController->RefreshStatsPanel();
    if (!successOsd.isEmpty() && callbacks.showSubtitleOsd)
        callbacks.showSubtitleOsd(successOsd);
}

void PlaybackController::OpenMediaPath(const QString& path)
{
    const QString trimmed = path.trimmed();
    if (trimmed.isEmpty())
        return;

    QFileInfo localFile(trimmed);
    if (localFile.exists() && localFile.isFile())
    {
        if (playlistManager)
            playlistManager->AddFiles(QStringList{ localFile.absoluteFilePath() });
        PlayFile(localFile.absoluteFilePath());
        return;
    }

    if (trimmed.contains("://"))
    {
        if (view)
            view->SetUrlText(trimmed);
        OpenUrl();
    }
}

void PlaybackController::PlayOrPause()
{
    if (!session || !view)
        return;

    const PlaybackSessionSnapshot snapshot = session->GetSessionSnapshot();

    if (snapshot.isComplete)
    {
        if (snapshot.isLiveStream)
        {
            eofHandled = false;
            const std::string url = snapshot.currentUrl;
            if (!url.empty())
            {
                BeginNetworkOpenAsync(QString::fromStdString(url), session->GetActiveOpenOptions(),
                    false, true, QString(), "Network");
            }
            return;
        }

        eofHandled = false;
        session->Seek(0.0);
        if (subtitleController)
            subtitleController->ResetAsrCues(features ? features->GetFeatureEpoch(AiCapability::Asr) : StreamEpoch{});
        SetPause(false);
        session->SetPause(false);
        view->SetSeekValue(0);
        return;
    }

    const bool newPause = !snapshot.isPaused;
    SetPause(newPause);
    session->SetPause(newPause);
}

void PlaybackController::SetPause(bool isPause)
{
    if (!view)
        return;

    view->SetPlayButtonText(isPause ? "Play" : "Pause");
}

void PlaybackController::VolumeChanged(int value)
{
    if (session)
        session->SetVolume(static_cast<double>(value) / 100.0);
}

void PlaybackController::SpeedChanged(int value)
{
    if (!view || !session || syncingSpeedUi)
        return;

    const PlaybackSessionSnapshot snapshot = session->GetSessionSnapshot();
    if (snapshot.isLiveStream)
    {
        ApplyLiveSpeedPolicy(true);
        session->SetSpeed(1.0);
        if (callbacks.updateSubtitleDisplay)
            callbacks.updateSubtitleDisplay();
        return;
    }

    double speed = static_cast<double>(value) / 10.0;
    if (speed < 0.1)
        speed = 0.1;

    view->SetSpeedControlsEnabled(true);
    view->SetSpeedText(QString("%1x").arg(speed, 0, 'f', 1));
    session->SetSpeed(speed);
    if (callbacks.updateSubtitleDisplay)
        callbacks.updateSubtitleDisplay();
}

void PlaybackController::OpenFile()
{
    if (!view)
        return;

    const QStringList names = view->SelectVideoFiles();
    if (names.isEmpty())
        return;

    SavePlaybackState();
    if (playlistManager)
        playlistManager->AddFiles(names);
    PlayFile(names.first());
}

void PlaybackController::PlayFile(const QString& path)
{
    if (!view || !session || !view->VideoWidgetHandle())
        return;

    const QString cleanedPath = QDir::cleanPath(path);
    const QFileInfo localFile(cleanedPath);
    if (!localFile.exists() || !localFile.isFile())
    {
        if (playlistManager)
            playlistManager->RemovePath(path);
        view->ShowWarningMessage("Error", QString("File not found:\n%1").arg(cleanedPath));
        return;
    }

    CancelPendingNetworkOpen();
    SavePlaybackState();
    if (callbacks.resetPerMediaFeatures)
        callbacks.resetPerMediaFeatures();

    view->SetWindowTitleText(localFile.fileName());
    eofHandled = false;
    saveCounter = 0;
    if (callbacks.resetDebugOsd)
        callbacks.resetDebugOsd();

    if (!session->Open(cleanedPath.toLocal8Bit().data(), view->VideoWidgetHandle()))
    {
        view->ShowWarningMessage("Error", QString("Open file failed:\n%1").arg(cleanedPath));
        return;
    }

    currentFilePath = cleanedPath;
    if (subtitleController)
        subtitleController->SetEpoch(features ? features->GetFeatureEpoch(AiCapability::Asr) : StreamEpoch{});
    if (callbacks.autoLoadEmbeddedSubtitle)
        callbacks.autoLoadEmbeddedSubtitle(cleanedPath);
    if (callbacks.autoLoadExternalSubtitle)
        callbacks.autoLoadExternalSubtitle(cleanedPath);
    if (callbacks.updateSubtitleTrackButton)
        callbacks.updateSubtitleTrackButton();
    SetPause(false);
    session->SetPause(false);
    if (callbacks.resetHideTimer)
        callbacks.resetHideTimer();
    UpdateStreamUI(false);
    UpdateAudioTrackButton();
    RestorePlaybackState();
    if (networkController)
        networkController->RefreshStatsPanel();
    if (playlistManager)
        playlistManager->SetCurrentPath(path);
}

bool PlaybackController::PlayNext()
{
    QString path;
    if (!playlistManager || !playlistManager->PlayNext(&path))
        return false;

    PlayFile(path);
    return true;
}

void PlaybackController::OpenUrl()
{
    if (!view || !session)
        return;

    const QString url = view->UrlText().trimmed();
    if (url.isEmpty())
        return;

    const StreamOpenOptions options = networkController
        ? networkController->BuildOpenOptionsFromUi()
        : StreamOpenOptions::DefaultNetwork();
    session->SetNetworkOpenOptions(options);
    if (networkController)
        networkController->SaveSettings();

    SavePlaybackState();
    if (callbacks.resetPerMediaFeatures)
        callbacks.resetPerMediaFeatures();

    view->SetWindowTitleText(url);
    eofHandled = false;
    saveCounter = 0;
    if (callbacks.resetDebugOsd)
        callbacks.resetDebugOsd();

    BeginNetworkOpenAsync(url, options, true, true, QString(), "Error");
}

void PlaybackController::UpdateStreamUI(bool isLive)
{
    if (!view)
        return;

    view->SetPreferLiveRendering(isLive);
    view->SetSeekVisible(!isLive);
    view->SetLiveVisible(isLive);
    ApplyLiveSpeedPolicy(isLive);
    if (isLive)
        view->SetTimeText(QString::fromUtf8("LIVE"));
}

void PlaybackController::ApplyLiveSpeedPolicy(bool isLive)
{
    if (!view)
        return;

    if (isLive)
    {
        syncingSpeedUi = true;
        view->SetSpeedControlsEnabled(false);
        view->SetSpeedValue(10);
        view->SetSpeedText(QStringLiteral("1.0x"));
        syncingSpeedUi = false;
        return;
    }

    view->SetSpeedControlsEnabled(true);
}

void PlaybackController::SliderPress()
{
    isSliderPress = true;
}

void PlaybackController::SliderRelease()
{
    isSliderPress = false;

    if (!session || !view)
        return;

    const PlaybackSessionSnapshot snapshot = session->GetSessionSnapshot();
    if (snapshot.isLiveStream)
        return;

    eofHandled = false;

    double pos = 0.0;
    if (view->SeekMaximum() > 0)
        pos = static_cast<double>(view->SeekValue()) / static_cast<double>(view->SeekMaximum());

    session->Seek(pos);
    if (subtitleController)
        subtitleController->ResetAsrCues(features ? features->GetFeatureEpoch(AiCapability::Asr) : StreamEpoch{});
}

void PlaybackController::CycleAudioTrack()
{
    if (!tracks)
        return;

    const int count = tracks->GetAudioStreamCount();
    if (count <= 1)
        return;

    const int cur = tracks->GetCurrentAudioIndex();
    SelectAudioTrack((cur + 1) % count);
}

void PlaybackController::SelectAudioTrack(int idx)
{
    if (!tracks)
        return;

    const int count = tracks->GetAudioStreamCount();
    if (count <= 0 || idx < 0 || idx >= count)
        return;

    const int current = tracks->GetCurrentAudioIndex();
    if (current == idx)
    {
        UpdateAudioTrackButton();
        return;
    }

    if (!tracks->SwitchAudioStream(idx))
    {
        UpdateAudioTrackButton();
        if (callbacks.showSubtitleOsd)
            callbacks.showSubtitleOsd("Audio track switch failed");
        return;
    }

    UpdateAudioTrackButton();

    const AudioStreamInfo info = tracks->GetAudioStreamInfo(idx);
    QString message = QString("Audio track: %1/%2").arg(idx + 1).arg(count);
    if (!info.language.empty())
        message += " | " + QString::fromStdString(info.language);
    if (!info.title.empty())
        message += " | " + QString::fromStdString(info.title);
    if (callbacks.showSubtitleOsd)
        callbacks.showSubtitleOsd(message);
}

void PlaybackController::UpdateAudioTrackButton()
{
    if (!view || !tracks)
        return;

    const int count = tracks->GetAudioStreamCount();
    if (count <= 1)
    {
        view->SetAudioTrackButtonVisible(false);
        return;
    }

    int cur = tracks->GetCurrentAudioIndex();
    if (cur < 0)
        cur = 0;

    const AudioStreamInfo info = tracks->GetAudioStreamInfo(cur);
    QString tip = QString("Track %1/%2").arg(cur + 1).arg(count);
    if (!info.language.empty())
        tip += " | " + QString::fromStdString(info.language);
    if (!info.title.empty())
        tip += " | " + QString::fromStdString(info.title);
    if (!info.codecName.empty())
        tip += " | " + QString::fromStdString(info.codecName);
    tip += "\nClick: choose audio track";

    view->SetAudioTrackButtonText(QString("A%1/%2").arg(cur + 1).arg(count));
    view->SetAudioTrackButtonToolTip(tip);
    const bool controlsShown = callbacks.controlsShownProvider ? callbacks.controlsShownProvider() : true;
    view->SetAudioTrackButtonVisible(controlsShown);
}

void PlaybackController::ShowAudioTrackMenu(const QPoint& pos)
{
    if (!view || !tracks)
        return;

    const int count = tracks->GetAudioStreamCount();
    if (count <= 1)
        return;

    const int cur = std::max(0, tracks->GetCurrentAudioIndex());

    QMenu menu(view->HostWidget());
    QtUiTheme::ApplyMenuStyle(menu);
    QAction* statusAction = menu.addAction(QString("Audio Tracks: %1").arg(count));
    statusAction->setEnabled(false);
    menu.addSeparator();

    for (int i = 0; i < count; ++i)
    {
        const AudioStreamInfo info = tracks->GetAudioStreamInfo(i);
        QString label = QString("Track %1").arg(i + 1);
        if (!info.language.empty())
            label += " | " + QString::fromStdString(info.language);
        if (!info.title.empty())
            label += " | " + QString::fromStdString(info.title);
        else if (!info.codecName.empty())
            label += " | " + QString::fromStdString(info.codecName);

        QAction* action = menu.addAction(label);
        action->setCheckable(true);
        action->setChecked(i == cur);
        action->setData(i);
    }

    QAction* chosen = menu.exec(view->MapAudioTrackMenuToGlobal(pos));
    if (!chosen || !chosen->data().isValid())
        return;

    SelectAudioTrack(chosen->data().toInt());
}

void PlaybackController::SavePlaybackState()
{
    if (!view || !config)
        return;

    config->SaveVolume(view->VolumeValue());

    if (!session)
        return;

    const PlaybackSessionSnapshot snapshot = session->GetSessionSnapshot();
    config->SavePlaybackPosition(
        currentFilePath,
        snapshot.positionMs,
        snapshot.totalMs,
        snapshot.isLiveStream);
}

void PlaybackController::RestorePlaybackState()
{
    if (!session || !config || currentFilePath.isEmpty())
        return;

    const PlaybackSessionSnapshot snapshot = session->GetSessionSnapshot();
    if (snapshot.isLiveStream)
        return;

    const long long savedPos = config->LoadPlaybackPosition(currentFilePath);
    Logger::Instance().Log(
        LogLevel::Info,
        "playback",
        "restore.state",
        "Restore playback state evaluated",
        {
            { "path", currentFilePath.toStdString() },
            { "saved_pos_ms", std::to_string(savedPos) },
            { "total_ms", std::to_string(snapshot.totalMs) },
            { "is_live", snapshot.isLiveStream ? "true" : "false" },
        });

    if (savedPos > 5000)
    {
        const long long total = snapshot.totalMs;
        if (total > 0)
        {
            session->Seek(static_cast<double>(savedPos) / static_cast<double>(total));
            if (subtitleController)
                subtitleController->ResetAsrCues(features ? features->GetFeatureEpoch(AiCapability::Asr) : StreamEpoch{});
        }
    }
}

void PlaybackController::TickUi()
{
    if (!session || !view)
        return;

    const PlaybackSessionSnapshot snapshot = session->GetSessionSnapshot();
    const bool isLive = snapshot.isLiveStream;

    if (!isLive && !isSliderPress)
    {
        const long long total = snapshot.totalMs;
        if (total > 0 && view->SeekMaximum() > 0)
        {
            const double pos = static_cast<double>(snapshot.positionMs) / static_cast<double>(total);
            const int seekValue = static_cast<int>(view->SeekMaximum() * pos);
            view->SetSeekValue(seekValue);
        }
    }

    if (isLive)
    {
        view->SetTimeText(snapshot.isBuffering ? QString::fromUtf8("Buffering...") : QString::fromUtf8("LIVE"));
    }
    else
    {
        long long total = snapshot.totalMs;
        long long currentMs = 0;

        if (isSliderPress && total > 0 && view->SeekMaximum() > 0)
        {
            const double pos = static_cast<double>(view->SeekValue()) / static_cast<double>(view->SeekMaximum());
            currentMs = static_cast<long long>(pos * total);
        }
        else
        {
            currentMs = snapshot.positionMs;
        }

        if (currentMs < 0)
            currentMs = 0;
        if (total < 0)
            total = 0;

        view->SetTimeText(QString("%1 / %2")
            .arg(QString::fromStdString(FormatTime(currentMs)))
            .arg(QString::fromStdString(FormatTime(total))));
    }

    if (!isLive && snapshot.totalMs > 0 && snapshot.isComplete && !eofHandled)
    {
        eofHandled = true;
        if (config)
            config->ClearPlaybackPosition(currentFilePath);

        if (!PlayNext())
        {
            SetPause(true);
            session->SetPause(true);
            view->SetSeekValue(view->SeekMaximum());
        }
    }

    if (snapshot.hasError)
    {
        const QString errorText = QString::fromStdString(snapshot.lastError);
        session->ClearError();
        if (isLive)
            view->SetTimeText(errorText);
        else
            view->ShowWarningMessage("Error", errorText);
    }

    saveCounter++;
    if (saveCounter >= 125)
    {
        saveCounter = 0;
        SavePlaybackState();
    }
}
