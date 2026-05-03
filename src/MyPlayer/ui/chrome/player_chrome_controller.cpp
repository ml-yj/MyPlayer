
#include "player_chrome_controller.h"

#include "../../app/service/playback_service_interfaces.h"
#include "../../app/controller/feature/feature_manager.h"
#include "../../app/controller/monitor/monitor_wall_controller.h"
#include "../../app/ui/my_player_ui_helpers.h"
#include "../../app/controller/network/network_controller.h"
#include "../../app/controller/playback/playback_controller.h"
#include "../../app/controller/playlist/playlist_manager.h"
#include "../../app/controller/subtitle/subtitle_controller.h"
#include "ui_my_player.h"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEvent>
#include <QGraphicsOpacityEffect>
#include <QKeyEvent>
#include <QListWidget>
#include <QMouseEvent>
#include <QMimeData>
#include <QPushButton>
#include <QUrl>
#include <QWidget>

#include <algorithm>

using namespace MyPlayerUiHelpers;

PlayerChromeController::PlayerChromeController(
    QWidget* hostWidget,
    Ui::MyPlayerClass* uiForm,
    IPlaybackSessionService* sessionService,
    IPlaybackTrackService* trackService,
    IPlaybackFeatureService* featureService,
    PlaylistManager* playlistManagerPtr,
    SubtitleController* subtitleControllerPtr,
    FeatureManager* featureManagerPtr,
    PlaybackController* playbackControllerPtr,
    NetworkController* networkControllerPtr,
    MonitorWallController* monitorWallControllerPtr,
    QListWidget* playlistWidgetPtr,
    QWidget* sidebarBackgroundWidget,
    QPushButton* playlistButtonWidget,
    QPushButton* colorButtonWidget,
    QPushButton* audioTrackButtonWidget,
    QPushButton* subtitleTrackButtonWidget,
    QPushButton* playModeButtonWidget,
    QPushButton* addFolderButtonWidget,
    QPushButton* clearListButtonWidget,
    QList<QWidget*>& autoHideWidgetsRef,
    QList<QGraphicsOpacityEffect*>& controlEffectsRef,
    bool& controlsShownRef,
    int& hideTimerRef,
    bool& sidebarResizingRef,
    bool& eofHandledRef,
    QString& currentFilePathRef,
    std::function<int()> videoAreaWidthProviderValue,
    std::function<int()> sidebarWidthProviderValue,
    std::function<bool()> playlistVisibleProviderValue,
    std::function<void()> infoOsdToggleHandlerValue,
    std::function<void()> screenshotHandlerValue)
    : host(hostWidget)
    , ui(uiForm)
    , session(sessionService)
    , tracks(trackService)
    , features(featureService)
    , playlistManager(playlistManagerPtr)
    , subtitleController(subtitleControllerPtr)
    , featureManager(featureManagerPtr)
    , playbackController(playbackControllerPtr)
    , networkController(networkControllerPtr)

    , monitorWallController(monitorWallControllerPtr)
    , playlistWidget(playlistWidgetPtr)
    , sidebarBackground(sidebarBackgroundWidget)
    , playlistButton(playlistButtonWidget)
    , colorButton(colorButtonWidget)
    , audioTrackButton(audioTrackButtonWidget)
    , subtitleTrackButton(subtitleTrackButtonWidget)
    , playModeButton(playModeButtonWidget)
    , addFolderButton(addFolderButtonWidget)
    , clearListButton(clearListButtonWidget)
    , autoHideWidgets(autoHideWidgetsRef)
    , controlEffects(controlEffectsRef)
    , controlsShown(controlsShownRef)
    , hideTimer(hideTimerRef)
    , sidebarResizing(sidebarResizingRef)
    , eofHandled(eofHandledRef)
    , currentFilePath(currentFilePathRef)
    , videoAreaWidthProvider(std::move(videoAreaWidthProviderValue))
    , sidebarWidthProvider(std::move(sidebarWidthProviderValue))
    , playlistVisibleProvider(std::move(playlistVisibleProviderValue))
    , infoOsdToggleHandler(std::move(infoOsdToggleHandlerValue))
    , screenshotHandler(std::move(screenshotHandlerValue))
{
}

void PlayerChromeController::ResetHideTimer()
{
    hideTimer = kControlAutoHideTicks;
    if (!controlsShown)
        ShowControls(true);
}

bool PlayerChromeController::HandleMouseMove(QMouseEvent* event)
{
    ResetHideTimer();

    if (sidebarResizing)
    {
        if (playlistManager && host)
        {
            const int mx = static_cast<int>(event->position().x());
            playlistManager->SetSidebarWidth(host->width() - mx, host->width());
        }
        return false;
    }

    if (playlistVisibleProvider && playlistVisibleProvider() && host)
    {
        const int mx = static_cast<int>(event->position().x());
        const int edgeX = host->width() - (sidebarWidthProvider ? sidebarWidthProvider() : 0);
        const bool nearEdge = (mx >= edgeX - 4 && mx <= edgeX + 4);
        if (nearEdge)
            host->setCursor(Qt::SizeHorCursor);
        else if (host->cursor().shape() == Qt::SizeHorCursor)
            host->setCursor(controlsShown ? Qt::ArrowCursor : Qt::BlankCursor);
    }

    return true;
}

bool PlayerChromeController::HandleMousePress(QMouseEvent* event)
{
    if (!host)
        return true;

    QWidget* topLevel = host->window();
    if (topLevel && !topLevel->isActiveWindow())
    {
        topLevel->raise();
        topLevel->activateWindow();
    }
    host->setFocus(Qt::MouseFocusReason);
    if (ui && ui->video)
        ui->video->setFocus(Qt::MouseFocusReason);

    ResetHideTimer();

    if ((playlistVisibleProvider && playlistVisibleProvider()) && event->button() == Qt::LeftButton)
    {
        const int mx = static_cast<int>(event->position().x());
        const int edgeX = host->width() - (sidebarWidthProvider ? sidebarWidthProvider() : 0);
        if (mx >= edgeX - 4 && mx <= edgeX + 4)
        {
            sidebarResizing = true;
            host->grabMouse(Qt::SizeHorCursor);
            return false;
        }
    }

    return true;
}

void PlayerChromeController::HandleMouseRelease()
{
    if (!host)
        return;

    if (sidebarResizing)
    {
        sidebarResizing = false;
        host->releaseMouse();
        host->setCursor(controlsShown ? Qt::ArrowCursor : Qt::BlankCursor);
    }
}

void PlayerChromeController::HandleMouseDoubleClick()
{
    if (!host)
        return;
    if (host->isFullScreen())
        host->showNormal();
    else
        host->showFullScreen();
}

bool PlayerChromeController::HandleEventFilter(QObject* obj, QEvent* event)
{
    if (!host || !ui)
        return false;

    if (event->type() == QEvent::MouseMove ||
        event->type() == QEvent::MouseButtonPress)
    {
        if (event->type() == QEvent::MouseButtonPress)
        {
            QWidget* topLevel = host->window();
            if (topLevel && !topLevel->isActiveWindow())
            {
                topLevel->raise();
                topLevel->activateWindow();
            }
            host->setFocus(Qt::MouseFocusReason);
            if (ui->video)
                ui->video->setFocus(Qt::MouseFocusReason);
        }
        ResetHideTimer();
    }

    if ((playlistVisibleProvider && playlistVisibleProvider()) && !sidebarResizing &&
        (event->type() == QEvent::MouseMove || event->type() == QEvent::MouseButtonPress))
    {
        QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
        QWidget* widget = qobject_cast<QWidget*>(obj);
        if (widget)
        {
            const QPoint pos = host->mapFromGlobal(widget->mapToGlobal(mouseEvent->pos()));
            const int edgeX = host->width() - (sidebarWidthProvider ? sidebarWidthProvider() : 0);
            const bool nearEdge = (pos.x() >= edgeX - 4 && pos.x() <= edgeX + 4);

            if (event->type() == QEvent::MouseMove)
            {
                if (nearEdge)
                    widget->setCursor(Qt::SizeHorCursor);
                else if (widget->cursor().shape() == Qt::SizeHorCursor)
                    widget->unsetCursor();
            }
            else if (nearEdge && mouseEvent->button() == Qt::LeftButton)
            {
                sidebarResizing = true;
                host->grabMouse(Qt::SizeHorCursor);
                return true;
            }
        }
    }

    if (event->type() == QEvent::KeyPress)
    {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
        const int key = keyEvent->key();
        const Qt::KeyboardModifiers mods = keyEvent->modifiers();
        const bool subtitleShortcut =
            ((mods & Qt::ControlModifier) &&
             (key == Qt::Key_T || key == Qt::Key_O || key == Qt::Key_E ||
              key == Qt::Key_Minus || key == Qt::Key_Equal || key == Qt::Key_Plus)) ||
            ((mods & Qt::AltModifier) &&
             (key == Qt::Key_Left || key == Qt::Key_Right ||
              key == Qt::Key_Up || key == Qt::Key_Down));

        if (obj == ui->urlInput && !subtitleShortcut)
            return false;

        if (key == Qt::Key_Space || key == Qt::Key_Left ||
            key == Qt::Key_Right || key == Qt::Key_Up ||
            key == Qt::Key_Down || key == Qt::Key_Escape ||
            key == Qt::Key_I || key == Qt::Key_L ||
            key == Qt::Key_A || key == Qt::Key_S ||
            key == Qt::Key_D || key == Qt::Key_P ||
            key == Qt::Key_Delete || subtitleShortcut)
        {
            return HandleKeyPress(keyEvent);
        }
    }

    return false;
}

bool PlayerChromeController::HandleKeyPress(QKeyEvent* event)
{
    if (!host || !ui || !session)
        return false;

    const PlaybackSessionSnapshot sessionSnapshot = session->GetSessionSnapshot();
    const Qt::KeyboardModifiers mods = event->modifiers();
    if ((mods & Qt::ControlModifier) && event->key() == Qt::Key_T)
    {
        if (subtitleController)
            subtitleController->CycleTrack();
        ResetHideTimer();
        return true;
    }
    if ((mods & Qt::ControlModifier) && event->key() == Qt::Key_O)
    {
        if (subtitleController)
            subtitleController->OpenSubtitleFile();
        ResetHideTimer();
        return true;
    }
    if ((mods & Qt::ControlModifier) && event->key() == Qt::Key_E)
    {
        if (subtitleController)
            subtitleController->ExportAsrSubtitle();
        ResetHideTimer();
        return true;
    }
    if ((mods & Qt::ControlModifier) &&
        (event->key() == Qt::Key_Minus || event->key() == Qt::Key_Equal || event->key() == Qt::Key_Plus))
    {
        if (subtitleController)
            subtitleController->AdjustFontSize((event->key() == Qt::Key_Minus) ? -2 : 2);
        ResetHideTimer();
        return true;
    }
    if ((mods & Qt::AltModifier) && event->key() == Qt::Key_Left)
    {
        if (subtitleController)
            subtitleController->AdjustOffset(-250);
        ResetHideTimer();
        return true;
    }
    if ((mods & Qt::AltModifier) && event->key() == Qt::Key_Right)
    {
        if (subtitleController)
            subtitleController->AdjustOffset(250);
        ResetHideTimer();
        return true;
    }
    if ((mods & Qt::AltModifier) && event->key() == Qt::Key_Up)
    {
        if (subtitleController)
            subtitleController->AdjustBottomMargin(10);
        ResetHideTimer();
        return true;
    }
    if ((mods & Qt::AltModifier) && event->key() == Qt::Key_Down)
    {
        if (subtitleController)
            subtitleController->AdjustBottomMargin(-10);
        ResetHideTimer();
        return true;
    }

    switch (event->key())
    {
    case Qt::Key_Space:
        if (playbackController)
            playbackController->PlayOrPause();
        break;
    case Qt::Key_Right:
    {
        if (sessionSnapshot.isLiveStream)
            break;
        const long long total = sessionSnapshot.totalMs;
        if (total <= 0)
            break;
        long long cur = sessionSnapshot.positionMs + 5000;
        if (cur > total)
            cur = total;
        eofHandled = false;
        session->Seek(static_cast<double>(cur) / static_cast<double>(total));
        if (subtitleController)
            subtitleController->ResetAsrCues(features ? features->GetFeatureEpoch(AiCapability::Asr) : StreamEpoch{});
        break;
    }
    case Qt::Key_Left:
    {
        if (sessionSnapshot.isLiveStream)
            break;
        const long long total = sessionSnapshot.totalMs;
        if (total <= 0)
            break;
        long long cur = sessionSnapshot.positionMs - 5000;
        if (cur < 0)
            cur = 0;
        eofHandled = false;
        session->Seek(static_cast<double>(cur) / static_cast<double>(total));
        if (subtitleController)
            subtitleController->ResetAsrCues(features ? features->GetFeatureEpoch(AiCapability::Asr) : StreamEpoch{});
        break;
    }
    case Qt::Key_Up:
    {
        int vol = ui->volumeSlider->value() + 5;
        if (vol > 100)
            vol = 100;
        ui->volumeSlider->setValue(vol);
        break;
    }
    case Qt::Key_Down:
    {
        int vol = ui->volumeSlider->value() - 5;
        if (vol < 0)
            vol = 0;
        ui->volumeSlider->setValue(vol);
        break;
    }
    case Qt::Key_Escape:
        if (host->isFullScreen())
            host->showNormal();
        break;
    case Qt::Key_I:
        if (infoOsdToggleHandler)
            infoOsdToggleHandler();
        break;
    case Qt::Key_L:
        if (playlistManager)
            playlistManager->ToggleSidebar();
        break;
    case Qt::Key_A:
        if (playbackController)
            playbackController->CycleAudioTrack();
        break;
    case Qt::Key_S:
        if (featureManager)
            featureManager->ToggleAnime4K();
        break;
    case Qt::Key_D:
        if (featureManager)
            featureManager->ToggleDetector();
        break;
    case Qt::Key_P:
        if (screenshotHandler)
            screenshotHandler();
        break;
    case Qt::Key_Delete:
        if (playlistManager)
            playlistManager->RemoveSelectedItems();
        break;
    default:
        return false;
    }

    ResetHideTimer();
    return true;
}

bool PlayerChromeController::HandleDragEnter(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls())
    {
        event->acceptProposedAction();
        return true;
    }
    return false;
}

bool PlayerChromeController::HandleDrop(QDropEvent* event)
{
    if (!playlistManager || !playbackController || !session)
        return false;

    QStringList files;
    for (const QUrl& url : event->mimeData()->urls())
    {
        const QString path = url.toLocalFile();
        if (!path.isEmpty())
            files.append(path);
    }

    if (files.isEmpty())
        return false;

    playlistManager->AddFiles(files);
    const PlaybackSessionSnapshot sessionSnapshot = session->GetSessionSnapshot();
    if (currentFilePath.isEmpty() || sessionSnapshot.isComplete)
        playbackController->PlayFile(files.first());
    return true;
}

void PlayerChromeController::ShowControls(bool show)
{
    controlsShown = show;

    if (!show)
    {
        for (auto* effect : controlEffects)
            effect->setOpacity(0.0);

        for (QWidget* widget : autoHideWidgets)
            widget->setVisible(false);

        if (!sidebarResizing && host)
            host->setCursor(Qt::BlankCursor);
        return;
    }

    for (QWidget* widget : autoHideWidgets)
        widget->setVisible(LayoutAllowsControl(widget));

    const PlaybackSessionSnapshot sessionSnapshot = session
        ? session->GetSessionSnapshot()
        : PlaybackSessionSnapshot{};
    const bool isLive = sessionSnapshot.isLiveStream;
    if (ui)
    {
        ApplyManagedVisibility(ui->playPos, !isLive, true);
        ApplyManagedVisibility(ui->liveLabel, isLive && LayoutAllowsControl(ui->liveLabel), true);
    }
    if (audioTrackButton && tracks)
    {
        ApplyManagedVisibility(audioTrackButton,
            tracks->GetAudioStreamCount() > 1 && LayoutAllowsControl(audioTrackButton), true);
    }

    for (auto* effect : controlEffects)
        effect->setOpacity(1.0);

    if (host)
        host->setCursor(Qt::ArrowCursor);
    hideTimer = kControlAutoHideTicks;
    ResizeLayout();
}

void PlayerChromeController::ResizeLayout()
{
    if (!host || !ui)
        return;

    const PlaybackSessionSnapshot sessionSnapshot = session
        ? session->GetSessionSnapshot()
        : PlaybackSessionSnapshot{};
    const int hostHeight = host->height();
    int videoWidth = videoAreaWidthProvider ? videoAreaWidthProvider() : host->width();
    videoWidth = std::max(220, videoWidth);

    if (ui->video)
    {
        ui->video->resize(videoWidth, hostHeight);
        ui->video->lower();
    }

    const int edgeInset =
        (videoWidth >= 1800) ? 36 :
        (videoWidth >= 1500) ? 24 :
        (videoWidth >= 1200) ? 12 : 4;
    const int bottomY = hostHeight - 50;
    const int ctrlY = hostHeight - 90;
    const int stackTopY = ctrlY - 2;
    const int leftMargin = 10 + (edgeInset / 2);
    const int controlGap = 5;
    const int rightMargin = 12 + edgeInset;
    const int buttonGap = 6;

    if (ui->playPos)
    {
        ApplyManagedVisibility(ui->playPos, !sessionSnapshot.isLiveStream, controlsShown);
        const int playPosInset = 50 + (edgeInset / 2);
        ui->playPos->move(playPosInset, bottomY);
        ui->playPos->resize(std::max(120, videoWidth - playPosInset * 2), ui->playPos->height());
    }

    int nextLeft = leftMargin;

    if (ui->openFile)
    {
        ApplyManagedVisibility(ui->openFile, true, controlsShown);
        ui->openFile->move(nextLeft, ctrlY);
        nextLeft = ui->openFile->x() + ui->openFile->width() + controlGap;
    }

    if (ui->isplay)
    {
        ApplyManagedVisibility(ui->isplay, true, controlsShown);
        ui->isplay->move(nextLeft, ctrlY);
        nextLeft = ui->isplay->x() + ui->isplay->width() + 15;
    }

    const bool showSpeedLabel = videoWidth >= 760;
    const int speedSliderWidth = videoWidth >= 680 ? 76 : 56;
    const int speedSliderHeight = 16;
    const int sliderRowGap = 4;
    const int sliderBlockWidth = speedSliderWidth + (showSpeedLabel && ui->speedLabel ? (ui->speedLabel->sizeHint().width() + 10) : 0);
    if (ui->speedSlider)
    {
        ui->speedSlider->setOrientation(Qt::Horizontal);
        ApplyManagedVisibility(ui->speedSlider, true, controlsShown);
        ui->speedSlider->move(nextLeft, stackTopY + 2);
        ui->speedSlider->resize(speedSliderWidth, speedSliderHeight);
    }
    if (ui->speedLabel)
    {
        ApplyManagedVisibility(ui->speedLabel, showSpeedLabel, controlsShown);
        if (showSpeedLabel)
        {
            ui->speedLabel->move(nextLeft + speedSliderWidth + 8, stackTopY);
        }
    }
    if (ui->volumeSlider)
    {
        ui->volumeSlider->setOrientation(Qt::Horizontal);
        ApplyManagedVisibility(ui->volumeSlider, true, controlsShown);
        ui->volumeSlider->move(nextLeft, stackTopY + speedSliderHeight + sliderRowGap + 2);
        ui->volumeSlider->resize(speedSliderWidth, speedSliderHeight);
    }
    nextLeft += sliderBlockWidth + 18;

    if (ui->timeLabel)
    {
        ApplyManagedVisibility(ui->timeLabel, true, controlsShown);
        ui->timeLabel->move(nextLeft, ctrlY + 5);
        nextLeft = ui->timeLabel->x() + ui->timeLabel->width() + 20;
    }

    const bool isLive = sessionSnapshot.isLiveStream;
    const int minUrlWidth =
        (videoWidth >= 1080) ? 240 :
        (videoWidth >= 860) ? 160 :
        (videoWidth >= 720) ? 110 : 0;
    bool showUrlBlock = (minUrlWidth > 0);
    bool showLiveBadge = showUrlBlock && isLive;

    const int openUrlWidth = ui->openUrl ? ui->openUrl->width() : 0;
    const int liveLabelWidth = ui->liveLabel
        ? std::max(ui->liveLabel->sizeHint().width(), ui->liveLabel->width())
        : 0;

    struct ManagedButton
    {
        QPushButton* button;
        bool visible;
    };

    QList<ManagedButton> managedButtons = {
        { playlistButton, true },
        { colorButton, true },
        { audioTrackButton, tracks && tracks->GetAudioStreamCount() > 1 },
        { subtitleTrackButton, true },
        { featureManager ? featureManager->AsrButton() : nullptr, true },
        { featureManager ? featureManager->Anime4KButton() : nullptr, true },
        { featureManager ? featureManager->DetectorButton() : nullptr, true },
        { networkController ? networkController->StatsButton() : nullptr, true },
        { networkController ? networkController->ToggleButton() : nullptr, true },
        { monitorWallController ? monitorWallController->ToggleButton() : nullptr, true }
    };

    auto buttonRowWidth = [&]() -> int
    {
        int total = 0;
        bool hasVisibleButton = false;
        for (const ManagedButton& item : managedButtons)
        {
            if (!item.button || !item.visible)
                continue;
            if (hasVisibleButton)
                total += buttonGap;
            total += item.button->width();
            hasVisibleButton = true;
        }
        return total;
    };

    auto reservedUrlWidth = [&]() -> int
    {
        if (!showUrlBlock)
            return 0;

        int total = minUrlWidth + openUrlWidth + controlGap;
        if (showLiveBadge)
            total += liveLabelWidth + 10;
        return total + 12;
    };

    auto hideButton = [&](QPushButton* button) -> bool
    {
        for (ManagedButton& item : managedButtons)
        {
            if (item.button == button && item.visible)
            {
                item.visible = false;
                return true;
            }
        }
        return false;
    };

    auto fitRightControls = [&](int availableWidth)
    {
        const QList<QPushButton*> hidePriority = {
            colorButton,
            subtitleTrackButton,
            audioTrackButton,
            networkController ? networkController->StatsButton() : nullptr,
            networkController ? networkController->ToggleButton() : nullptr,
            playlistButton,
            monitorWallController ? monitorWallController->ToggleButton() : nullptr,
            featureManager ? featureManager->AsrButton() : nullptr,
            featureManager ? featureManager->Anime4KButton() : nullptr,
            featureManager ? featureManager->DetectorButton() : nullptr
        };

        while (buttonRowWidth() > availableWidth)
        {
            bool changed = false;
            for (QPushButton* button : hidePriority)
            {
                if (hideButton(button))
                {
                    changed = true;
                    break;
                }
            }
            if (!changed)
                break;
        }
    };

    int availableForButtons = std::max(0, videoWidth - rightMargin - nextLeft - reservedUrlWidth());
    if (showUrlBlock && availableForButtons < 120)
    {
        showUrlBlock = false;
        showLiveBadge = false;
        availableForButtons = std::max(0, videoWidth - rightMargin - nextLeft);
    }
    fitRightControls(availableForButtons);

    int nextRight = videoWidth - rightMargin;

    auto placeRightButton = [&](QPushButton* button, bool visible = true)
    {
        if (!button || !visible)
            return;
        nextRight -= button->width();
        button->move(nextRight, ctrlY);
        nextRight -= buttonGap;
    };

    for (const ManagedButton& item : managedButtons)
        ApplyManagedVisibility(item.button, item.visible, controlsShown);

    placeRightButton(playlistButton, LayoutAllowsControl(playlistButton));
    placeRightButton(audioTrackButton, LayoutAllowsControl(audioTrackButton));
    placeRightButton(subtitleTrackButton, LayoutAllowsControl(subtitleTrackButton));
    placeRightButton(featureManager ? featureManager->AsrButton() : nullptr,
        LayoutAllowsControl(featureManager ? featureManager->AsrButton() : nullptr));
    placeRightButton(featureManager ? featureManager->Anime4KButton() : nullptr,
        LayoutAllowsControl(featureManager ? featureManager->Anime4KButton() : nullptr));
    placeRightButton(featureManager ? featureManager->DetectorButton() : nullptr,
        LayoutAllowsControl(featureManager ? featureManager->DetectorButton() : nullptr));
    placeRightButton(networkController ? networkController->StatsButton() : nullptr,
        LayoutAllowsControl(networkController ? networkController->StatsButton() : nullptr));
    placeRightButton(networkController ? networkController->ToggleButton() : nullptr,
        LayoutAllowsControl(networkController ? networkController->ToggleButton() : nullptr));
    placeRightButton(monitorWallController ? monitorWallController->ToggleButton() : nullptr,
        LayoutAllowsControl(monitorWallController ? monitorWallController->ToggleButton() : nullptr));
    placeRightButton(colorButton, LayoutAllowsControl(colorButton));

    ApplyManagedVisibility(ui->urlInput, showUrlBlock, controlsShown);
    ApplyManagedVisibility(ui->openUrl, showUrlBlock, controlsShown);
    ApplyManagedVisibility(ui->liveLabel, showLiveBadge, controlsShown);

    if (showUrlBlock && ui->urlInput && ui->openUrl)
    {
        const int urlX = nextLeft;
        const int trailingWidth = openUrlWidth + controlGap + (showLiveBadge ? (liveLabelWidth + 10) : 0);
        const int availableUrlWidth = nextRight - urlX - trailingWidth;

        if (availableUrlWidth < minUrlWidth)
        {
            ApplyManagedVisibility(ui->urlInput, false, controlsShown);
            ApplyManagedVisibility(ui->openUrl, false, controlsShown);
            ApplyManagedVisibility(ui->liveLabel, false, controlsShown);
        }
        else
        {
            const int urlW = std::min(300, availableUrlWidth);
            ui->urlInput->move(urlX, ctrlY);
            ui->urlInput->resize(urlW, 35);
            ui->openUrl->move(ui->urlInput->x() + ui->urlInput->width() + 5, ctrlY);

            if (showLiveBadge && ui->liveLabel)
                ui->liveLabel->move(ui->openUrl->x() + ui->openUrl->width() + 10, ctrlY + 3);
        }
    }

    if (playlistVisibleProvider && playlistVisibleProvider())
    {
        const int sideX = videoWidth;
        const int headerY = 6;
        const int sidebarWidth = sidebarWidthProvider ? sidebarWidthProvider() : 250;

        if (sidebarBackground)
        {
            sidebarBackground->setGeometry(sideX, 0, sidebarWidth, hostHeight);
            sidebarBackground->setVisible(true);
            sidebarBackground->lower();
        }

        const int margin = 5;
        const int addW = 72;
        const int clrW = 72;
        const int gap = 8;
        const int headerHeight = 32;
        int modeW = sidebarWidth - margin * 2 - addW - clrW - gap * 2;
        if (modeW < 86)
            modeW = 86;
        if (playModeButton)
            playModeButton->setGeometry(sideX + margin, headerY, modeW, headerHeight);
        if (addFolderButton)
            addFolderButton->setGeometry(sideX + margin + modeW + gap, headerY, addW, headerHeight);
        if (clearListButton)
            clearListButton->setGeometry(sideX + margin + modeW + gap + addW + gap, headerY, clrW, headerHeight);
        if (playlistWidget)
            playlistWidget->setGeometry(sideX, headerY + headerHeight + 8, sidebarWidth, hostHeight - headerY - headerHeight - 8);
    }
    else if (sidebarBackground)
    {
        sidebarBackground->setVisible(false);
    }

    if (subtitleController)
        subtitleController->Relayout();
    if (featureManager)
        featureManager->Relayout();
    if (networkController)
        networkController->LayoutPanels(videoWidth, hostHeight);
}

void PlayerChromeController::TickUi(bool isPlaying, bool isSliderPress)
{
    if (isPlaying && !isSliderPress)
    {
        if (hideTimer > 0)
            hideTimer--;

        if (hideTimer > 0 && hideTimer <= 12 && controlsShown)
        {
            const double opacity = static_cast<double>(hideTimer) / 12.0;
            for (auto* effect : controlEffects)
                effect->setOpacity(opacity);
        }

        if (hideTimer == 0 && controlsShown)
            ShowControls(false);
        return;
    }

    if (!controlsShown)
        ShowControls(true);
    hideTimer = kControlAutoHideTicks;
}
