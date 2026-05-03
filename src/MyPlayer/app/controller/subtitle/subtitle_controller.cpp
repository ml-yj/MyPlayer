
#include "subtitle_controller.h"

#include "../../service/config_service.h"
#include "../../service/playback_service_interfaces.h"
#include "../../view/qt_ui_theme.h"
#include "../../view/subtitle_view_qt.h"
#include "../../../ui/video/video_widget.h"

#include <QDebug>
#include <QMenu>
#include <QMetaObject>
#include <QThread>
#include <QTimer>
#include <QWidget>

#include <algorithm>
#include <limits>

namespace
{
constexpr long long kWhisperSubtitleDisplayDelayMs = 1000;
constexpr long long kSubtitleDebugToleranceMs = 120;
}

SubtitleController::SubtitleController(std::unique_ptr<ISubtitleView> viewValue,
    IPlaybackSessionService* sessionService,
    IPlaybackTrackService* trackService,
    ConfigService* configService,
    std::function<bool()> asrEnabledQuery,
    std::function<void()> asrToggleAction,
    std::function<QString()> currentMediaPathQuery)
    : view(std::move(viewValue))
    , session(sessionService)
    , tracks(trackService)
    , config(configService)
    , asrEnabledProvider(std::move(asrEnabledQuery))
    , asrToggleHandler(std::move(asrToggleAction))
    , currentMediaPathProvider(std::move(currentMediaPathQuery))
{
}

void SubtitleController::ClearRenderedTrack()
{
    if (view && view->VideoWidgetHandle())
        view->VideoWidgetHandle()->clearSubtitleTrack();
}

void SubtitleController::ResetForMedia()
{
    activeSubtitleTrackId = 0;
    subtitleEpoch = {};
    subtitleManager.RemoveTracks(SubtitleSourceType::Embedded);
    subtitleManager.RemoveTracks(SubtitleSourceType::ExternalFile);
    subtitleManager.RemoveTracks(SubtitleSourceType::Whisper);
    ClearRenderedTrack();
    UpdateTrackButton();
    HideOsd();
}

void SubtitleController::SyncRenderer()
{
    VideoWidget* video = view ? view->VideoWidgetHandle() : nullptr;
    if (!video)
        return;

    video->setSubtitleStyle(subtitleFontPointSize, subtitleBottomMarginPx);

    const SubtitleTrack* track = subtitleManager.FindTrack(activeSubtitleTrackId);
    if (!track || track->cues.isEmpty())
    {
        video->clearSubtitleTrack();
        return;
    }

    video->setSubtitleTrack(track);
}

void SubtitleController::TickUi()
{
    UpdateDisplay();
}

void SubtitleController::UpdateDisplay()
{
    VideoWidget* video = view ? view->VideoWidgetHandle() : nullptr;
    if (!video || !session)
        return;

    const PlaybackSessionSnapshot snapshot = session->GetSessionSnapshot();
    long long curPts = snapshot.positionMs;
    if (curPts < 0)
        curPts = 0;

    const SubtitleTrack* track = subtitleManager.FindTrack(activeSubtitleTrackId);
    const bool isWhisperTrack = track && track->source == SubtitleSourceType::Whisper;
    const long long effectiveOffsetMs = isWhisperTrack ? 0LL : subtitleOffsetMs;
    if (isWhisperTrack && asrEnabledProvider && asrEnabledProvider())
        curPts = std::max(0LL, curPts - kWhisperSubtitleDisplayDelayMs);

    if (isWhisperTrack)
    {
        const long long adjustedClockMs = curPts - effectiveOffsetMs;
        const SubtitleCue* matchedCue = nullptr;
        for (const SubtitleCue& cue : track->cues)
        {
            if (cue.endMs < adjustedClockMs - kSubtitleDebugToleranceMs)
                continue;
            if (cue.startMs <= adjustedClockMs + kSubtitleDebugToleranceMs &&
                cue.endMs >= adjustedClockMs - kSubtitleDebugToleranceMs)
            {
                matchedCue = &cue;
                break;
            }
            if (cue.startMs > adjustedClockMs + kSubtitleDebugToleranceMs)
                break;
        }

        static long long lastLoggedClockMs = std::numeric_limits<long long>::min();
        static quint64 lastLoggedRevision = 0;
        if (std::llabs(adjustedClockMs - lastLoggedClockMs) >= 1000 ||
            lastLoggedRevision != track->revision)
        {
            lastLoggedClockMs = adjustedClockMs;
            lastLoggedRevision = track->revision;
            if (matchedCue)
            {
                qDebug().noquote()
                    << QString("[subdbg] display track=ASR clock=%1 offset=%2 cues=%3 rev=%4 hit=%5-%6 text=\"%7\"")
                           .arg(adjustedClockMs)
                           .arg(effectiveOffsetMs)
                           .arg(track->cues.size())
                           .arg(track->revision)
                           .arg(matchedCue->startMs)
                           .arg(matchedCue->endMs)
                           .arg(matchedCue->text.left(80));
            }
            else
            {
                qDebug().noquote()
                    << QString("[subdbg] display track=ASR clock=%1 offset=%2 cues=%3 rev=%4 hit=none")
                           .arg(adjustedClockMs)
                           .arg(effectiveOffsetMs)
                           .arg(track->cues.size())
                           .arg(track->revision);
            }
        }
    }

    SyncRenderer();
    video->setSubtitleClock(curPts, effectiveOffsetMs);
}

void SubtitleController::UpdateStyle()
{
    if (view && view->VideoWidgetHandle())
        view->VideoWidgetHandle()->setSubtitleStyle(subtitleFontPointSize, subtitleBottomMarginPx);
}

void SubtitleController::Relayout()
{
    if (!view || !view->IsOsdVisible())
        return;

    const int videoW = view->VideoAreaWidth();
    view->MoveOsd((videoW - view->OsdWidth()) / 2, view->HostHeight() / 3);
    view->RaiseOsd();
}

void SubtitleController::ShowOsd(const QString& text)
{
    if (!view || !view->HostWidget())
        return;

    QWidget* const host = view->HostWidget();
    if (QThread::currentThread() != host->thread())
    {
        QMetaObject::invokeMethod(host, [this, text]() { ShowOsd(text); }, Qt::QueuedConnection);
        return;
    }

    view->SetOsdText(text);
    Relayout();
    view->ShowOsd();
    ++subtitleOsdToken;
    const int osdToken = subtitleOsdToken;
    QTimer::singleShot(2000, view->HostWidget(), [this, osdToken]() {
        if (view && subtitleOsdToken == osdToken)
            view->HideOsd();
    });
}

void SubtitleController::HideOsd()
{
    if (!view || !view->HostWidget())
    {
        ++subtitleOsdToken;
        return;
    }

    QWidget* const host = view->HostWidget();
    if (QThread::currentThread() != host->thread())
    {
        QMetaObject::invokeMethod(host, [this]() { HideOsd(); }, Qt::QueuedConnection);
        return;
    }

    view->HideOsd();
    ++subtitleOsdToken;
}

void SubtitleController::SaveSettings()
{
    if (!config)
        return;

    config->SaveSubtitlePreferences({
        subtitleOffsetMs,
        subtitleFontPointSize,
        subtitleBottomMarginPx
    });
}

void SubtitleController::LoadSettings()
{
    const SubtitlePreferences preferences = config ? config->LoadSubtitlePreferences() : SubtitlePreferences{};
    subtitleOffsetMs = preferences.offsetMs;
    subtitleFontPointSize = preferences.fontPointSize;
    subtitleBottomMarginPx = preferences.bottomMarginPx;

    subtitleFontPointSize = std::clamp(subtitleFontPointSize, 14, 48);
    subtitleBottomMarginPx = std::clamp(subtitleBottomMarginPx, 60, 240);
}

void SubtitleController::AdjustOffset(int deltaMs)
{
    subtitleOffsetMs += deltaMs;
    subtitleOffsetMs = std::clamp(subtitleOffsetMs, -5000, 5000);
    ClearRenderedTrack();
    ShowOsd(QString("Subtitle offset: %1 ms").arg(subtitleOffsetMs, 0, 10, QChar(' ')));
}

void SubtitleController::AdjustFontSize(int deltaPt)
{
    subtitleFontPointSize = std::clamp(subtitleFontPointSize + deltaPt, 14, 48);
    UpdateStyle();
    ShowOsd(QString("Subtitle size: %1 pt").arg(subtitleFontPointSize));
}

void SubtitleController::AdjustBottomMargin(int deltaPx)
{
    subtitleBottomMarginPx = std::clamp(subtitleBottomMarginPx + deltaPx, 60, 240);
    UpdateStyle();
    ShowOsd(QString("Subtitle position: %1 px").arg(subtitleBottomMarginPx));
}

QVector<int> SubtitleController::AvailableTrackIds() const
{
    QVector<int> ids;
    const QVector<SubtitleTrack>& tracks = subtitleManager.Tracks();
    for (const SubtitleTrack& track : tracks)
    {
        if (track.source == SubtitleSourceType::Whisper)
        {
            if ((asrEnabledProvider && asrEnabledProvider()) || !track.cues.isEmpty())
                ids.push_back(track.id);
            continue;
        }

        if (!track.cues.isEmpty())
            ids.push_back(track.id);
    }

    return ids;
}

void SubtitleController::UpdateTrackButton()
{
    if (!view)
        return;

    const QVector<int> ids = AvailableTrackIds();
    QString tooltip =
        "Left click: cycle subtitle track / start ASR when no track\n"
        "Right click: subtitle menu";

    if (activeSubtitleTrackId != 0 && !ids.contains(activeSubtitleTrackId))
        activeSubtitleTrackId = ids.isEmpty() ? 0 : ids.front();

    if (!ids.isEmpty() && activeSubtitleTrackId != 0)
    {
        const SubtitleTrack* track = subtitleManager.FindTrack(activeSubtitleTrackId);
        if (track)
            tooltip.prepend(QString("Current: %1 (%2)\n").arg(track->name, SourceTag(track->source)));
    }
    else
    {
        tooltip.prepend("Current: Off\n");
        if (!(asrEnabledProvider && asrEnabledProvider()))
            tooltip += "\nTip: click SUB to start ASR generation.";
    }

    if (subtitleOffsetMs != 0)
        tooltip += QString("\nOffset: %1 ms").arg(subtitleOffsetMs);

    view->UpdateTrackButton("SUB", activeSubtitleTrackId != 0, tooltip);
}

void SubtitleController::CycleTrack()
{
    const QVector<int> ids = AvailableTrackIds();
    if (ids.isEmpty())
    {
        if (!(asrEnabledProvider && asrEnabledProvider()))
        {
            if (asrToggleHandler)
                asrToggleHandler();
            if (asrEnabledProvider && asrEnabledProvider())
                ShowOsd("ASR started. Wait for speech to generate subtitles.");
            return;
        }

        activeSubtitleTrackId = 0;
        ClearRenderedTrack();
        UpdateTrackButton();
        ShowOsd("ASR is running. Waiting for recognized speech.");
        return;
    }

    QVector<int> cycleIds;
    cycleIds.push_back(0);
    cycleIds += ids;

    int currentIndex = cycleIds.indexOf(activeSubtitleTrackId);
    if (currentIndex < 0)
        currentIndex = 0;

    activeSubtitleTrackId = cycleIds[(currentIndex + 1) % cycleIds.size()];
    ClearRenderedTrack();
    UpdateTrackButton();

    if (activeSubtitleTrackId == 0)
    {
        ShowOsd("Subtitle track: OFF");
        return;
    }

    const SubtitleTrack* track = subtitleManager.FindTrack(activeSubtitleTrackId);
    if (!track)
    {
        ShowOsd("Subtitle track: OFF");
        return;
    }

    ShowOsd(QString("Subtitle track: %1 (%2)").arg(track->name, SourceTag(track->source)));
}

void SubtitleController::ShowMenu(const QPoint& pos)
{
    if (!view)
        return;

    QMenu menu(view->HostWidget());
    ApplyMenuStyle(menu);

    QString statusText = "Current: Off";
    if (activeSubtitleTrackId != 0)
    {
        const SubtitleTrack* activeTrack = subtitleManager.FindTrack(activeSubtitleTrackId);
        if (activeTrack)
            statusText = QString("Current: %1 (%2)").arg(activeTrack->name, SourceTag(activeTrack->source));
    }

    QAction* statusAction = menu.addAction(statusText);
    statusAction->setEnabled(false);
    menu.addSeparator();

    QAction* asrToggleAction = menu.addAction((asrEnabledProvider && asrEnabledProvider()) ? "Disable ASR Generation" : "Enable ASR Generation");
    asrToggleAction->setCheckable(true);
    asrToggleAction->setChecked(asrEnabledProvider && asrEnabledProvider());
    menu.addSeparator();

    QAction* offAction = menu.addAction("Off");
    offAction->setCheckable(true);
    offAction->setChecked(activeSubtitleTrackId == 0);

    const QVector<int> ids = AvailableTrackIds();
    for (int trackId : ids)
    {
        const SubtitleTrack* track = subtitleManager.FindTrack(trackId);
        if (!track)
            continue;

        QAction* trackAction = menu.addAction(QString("%1 (%2)").arg(track->name, SourceTag(track->source)));
        trackAction->setCheckable(true);
        trackAction->setChecked(trackId == activeSubtitleTrackId);
        trackAction->setData(trackId);
    }

    menu.addSeparator();
    QAction* openAction = menu.addAction("Open Subtitle...");

    QAction* exportAction = nullptr;
    const int asrTrackId = subtitleManager.FindTrackBySource(SubtitleSourceType::Whisper);
    const SubtitleTrack* asrTrack = subtitleManager.FindTrack(asrTrackId);
    if (asrTrack && !asrTrack->cues.isEmpty())
        exportAction = menu.addAction("Export ASR to SRT");

    menu.addSeparator();
    QAction* offsetMinusAction = menu.addAction("Offset -250 ms");
    QAction* offsetPlusAction = menu.addAction("Offset +250 ms");
    QAction* resetOffsetAction = menu.addAction(QString("Reset Offset (%1 ms)").arg(subtitleOffsetMs));

    menu.addSeparator();
    QAction* sizeSmallerAction = menu.addAction("Subtitle Smaller");
    QAction* sizeLargerAction = menu.addAction("Subtitle Larger");
    QAction* moveUpAction = menu.addAction("Move Subtitle Up");
    QAction* moveDownAction = menu.addAction("Move Subtitle Down");

    QAction* chosen = menu.exec(view->MapTrackMenuToGlobal(pos));
    if (!chosen)
        return;

    if (chosen == offAction)
    {
        activeSubtitleTrackId = 0;
        ClearRenderedTrack();
        UpdateTrackButton();
        ShowOsd("Subtitle track: OFF");
        return;
    }

    if (chosen == asrToggleAction)
    {
        if (asrToggleHandler)
            asrToggleHandler();
        return;
    }

    if (chosen == openAction)
    {
        OpenSubtitleFile();
        return;
    }

    if (chosen == exportAction)
    {
        ExportAsrSubtitle();
        return;
    }

    if (chosen == offsetMinusAction)
    {
        AdjustOffset(-250);
        UpdateTrackButton();
        return;
    }

    if (chosen == offsetPlusAction)
    {
        AdjustOffset(250);
        UpdateTrackButton();
        return;
    }

    if (chosen == resetOffsetAction)
    {
        subtitleOffsetMs = 0;
        ClearRenderedTrack();
        UpdateTrackButton();
        ShowOsd("Subtitle offset: 0 ms");
        return;
    }

    if (chosen == sizeSmallerAction)
    {
        AdjustFontSize(-2);
        return;
    }

    if (chosen == sizeLargerAction)
    {
        AdjustFontSize(2);
        return;
    }

    if (chosen == moveUpAction)
    {
        AdjustBottomMargin(10);
        return;
    }

    if (chosen == moveDownAction)
    {
        AdjustBottomMargin(-10);
        return;
    }

    if (chosen->data().isValid())
    {
        activeSubtitleTrackId = chosen->data().toInt();
        ClearRenderedTrack();
        UpdateTrackButton();
        const SubtitleTrack* track = subtitleManager.FindTrack(activeSubtitleTrackId);
        if (!track)
        {
            ShowOsd("Subtitle track: OFF");
            return;
        }

        ShowOsd(QString("Subtitle track: %1 (%2)").arg(track->name, SourceTag(track->source)));
    }
}

QString SubtitleController::SourceTag(SubtitleSourceType source) const
{
    switch (source)
    {
    case SubtitleSourceType::ExternalFile:
        return "SRT";
    case SubtitleSourceType::Embedded:
        return "INT";
    case SubtitleSourceType::Whisper:
        return "ASR";
    }
    return "SUB";
}

void SubtitleController::ApplyMenuStyle(QMenu& menu) const
{
    QtUiTheme::ApplyMenuStyle(menu);
}
