

#include "monitor_tile_widget.h"

#include "../video/video_widget.h"
#include "../../app/view/qt_ui_theme.h"
#include "../../features/detector/detector_types.h"

#include <QApplication>
#include <QCloseEvent>
#include <QContextMenuEvent>
#include <QDateTime>
#include <QDrag>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QVBoxLayout>

#include <algorithm>

namespace
{
constexpr int kMonitorAsrFontPointSize = 18;
constexpr int kMonitorAsrBottomMarginPx = 36;
constexpr int kMonitorAsrMaxCueCount = 48;

QString StateText(const MonitorSessionSnapshot& snapshot)
{
    QStringList flags;
    flags << QString::fromLatin1(StreamSessionStateName(snapshot.playback.state));
    if (snapshot.audioOwner && !snapshot.muted)
        flags << "AUDIO";
    if (snapshot.detectorEnabled)
        flags << "DET";
    if (snapshot.asrEnabled)
        flags << "ASR";
    else if (snapshot.asrRequested)
        flags << "ASR-WAIT";
    if (snapshot.recordingActive && snapshot.recordingSegmentPlannedEndUtc.isValid())
    {
        const qint64 secondsRemaining = std::max<qint64>(
            0,
            QDateTime::currentDateTimeUtc().secsTo(snapshot.recordingSegmentPlannedEndUtc));
        flags << QString("REC:%1s").arg(secondsRemaining);
    }
    else if (snapshot.recordingEnabled)
    {
        flags << "REC";
    }
    if (!snapshot.recordingLastError.trimmed().isEmpty())
        flags << "REC-ERR";
    if (snapshot.alarmCount > 0)
        flags << QString("ALM:%1").arg(snapshot.alarmCount);
    return flags.join(" | ");
}

QString DetailText(const MonitorSessionSnapshot& snapshot)
{
    const QString resolution = (snapshot.media.videoWidth > 0 && snapshot.media.videoHeight > 0)
        ? QString("%1x%2").arg(snapshot.media.videoWidth).arg(snapshot.media.videoHeight)
        : QString("no-video");
    const QString groupText = snapshot.groupName.trimmed().isEmpty()
        ? QString()
        : QString("  grp:%1").arg(snapshot.groupName.trimmed());
    QString detail = QString("%1%2  qp:%3/%4  buf:%5  evt:%6")
        .arg(resolution)
        .arg(groupText)
        .arg(snapshot.stats.videoQueuePackets)
        .arg(snapshot.stats.audioQueuePackets)
        .arg(snapshot.stats.audioDeviceBufferedMs)
        .arg(snapshot.eventCount);
    if (snapshot.recordingActive
        && snapshot.recordingSegmentStartUtc.isValid()
        && snapshot.recordingSegmentPlannedEndUtc.isValid())
    {
        detail += QString("\nSEG %1 -> %2")
            .arg(snapshot.recordingSegmentStartUtc.toLocalTime().time().toString("HH:mm:ss"))
            .arg(snapshot.recordingSegmentPlannedEndUtc.toLocalTime().time().toString("HH:mm:ss"));
    }
    if (!snapshot.recordingLastError.trimmed().isEmpty())
    {
        QString errorText = snapshot.recordingLastError.trimmed();
        if (errorText.size() > 96)
            errorText = errorText.left(93) + "...";
        detail += QString("\nREC ERR: %1").arg(errorText);
    }
    if (snapshot.asrRequested && !snapshot.asrEnabled)
    {
        if (snapshot.media.audioChannels <= 0)
        {
            detail += "\nASR waiting: no audio track";
        }
        else if (!snapshot.asrEligible)
        {
            detail += "\nASR waiting: select/maximize this tile";
        }
        else
        {
            detail += "\nASR waiting: model/audio warmup";
        }
    }
    return detail;
}

QString ButtonStyle(bool active, const QString& accent)
{
    if (active)
    {
        return QString(
            "QPushButton { background: %1; color: #FFFFFF; border: 1px solid rgba(255,255,255,45); "
            "border-radius: 4px; font-weight: 600; padding: 3px 6px; }").arg(accent);
    }

    return
        "QPushButton { background: rgba(255,255,255,22); color: #F5F5F7; border: 1px solid rgba(255,255,255,30); "
        "border-radius: 4px; padding: 3px 6px; }";
}
}

MonitorTileWidget::MonitorTileWidget(const QString& sessionId, QWidget* parent)
    : QWidget(parent)
    , sessionId_(sessionId)
{
    setAttribute(Qt::WA_StyledBackground, true);
    setMinimumSize(0, 0);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setStyleSheet(
        "MonitorTileWidget { background: rgba(12, 16, 22, 238); border: 1px solid rgba(255,255,255,28); border-radius: 8px; }"
        "QLabel { color: #F5F5F7; }");

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(6, 6, 6, 6);
    rootLayout->setSpacing(6);

    auto* headerLayout = new QHBoxLayout();
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(6);
    titleLabel_ = new QLabel(sessionId_, this);
    titleLabel_->setStyleSheet("font-size: 13px; font-weight: 600;");
    titleLabel_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    stateLabel_ = new QLabel("Idle", this);
    stateLabel_->setStyleSheet("font-size: 11px; color: #9FB4C6;");
    stateLabel_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    headerLayout->addWidget(titleLabel_, 1);
    headerLayout->addWidget(stateLabel_, 0);
    rootLayout->addLayout(headerLayout);

    auto* actionLayout = new QHBoxLayout();
    actionLayout->setContentsMargins(0, 0, 0, 0);
    actionLayout->setSpacing(4);
    detectorButton_ = new QPushButton("DET", this);
    detectorButton_->setCheckable(true);
    asrButton_ = new QPushButton("ASR", this);
    asrButton_->setCheckable(true);
    recordButton_ = new QPushButton("REC", this);
    recordButton_->setCheckable(true);
    muteButton_ = new QPushButton("M", this);
    muteButton_->setCheckable(true);
    reopenButton_ = new QPushButton("OPN", this);
    snapshotButton_ = new QPushButton("SHOT", this);
    popoutButton_ = new QPushButton("POP", this);
    removeButton_ = new QPushButton("X", this);
    for (QPushButton* button : {
            detectorButton_, asrButton_, recordButton_, muteButton_,
            reopenButton_, snapshotButton_, popoutButton_, removeButton_ })
    {
        button->setFixedHeight(24);
    }
    actionLayout->addWidget(detectorButton_);
    actionLayout->addWidget(asrButton_);
    actionLayout->addWidget(recordButton_);
    actionLayout->addWidget(muteButton_);
    actionLayout->addStretch(1);
    actionLayout->addWidget(reopenButton_);
    actionLayout->addWidget(snapshotButton_);
    actionLayout->addWidget(popoutButton_);
    actionLayout->addWidget(removeButton_);
    rootLayout->addLayout(actionLayout);

    video_ = new VideoWidget(this);
    video_->setMinimumSize(0, 0);
    video_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    video_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    video_->setSubtitleStyle(kMonitorAsrFontPointSize, kMonitorAsrBottomMarginPx);
    video_->setSubtitleClock(0, 0);
    rootLayout->addWidget(video_, 1);

    detailLabel_ = new QLabel("no-video", this);
    detailLabel_->setWordWrap(true);
    detailLabel_->setStyleSheet("font-size: 11px; color: #C3D0DF;");
    detailLabel_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    rootLayout->addWidget(detailLabel_);

    connect(detectorButton_, &QPushButton::toggled, this, [this](bool checked) {
        emit ToggleDetectorRequested(sessionId_, checked);
    });
    connect(asrButton_, &QPushButton::toggled, this, [this](bool checked) {
        emit ToggleAsrRequested(sessionId_, checked);
    });
    connect(recordButton_, &QPushButton::toggled, this, [this](bool checked) {
        emit ToggleRecordingRequested(sessionId_, checked);
    });
    connect(muteButton_, &QPushButton::toggled, this, [this](bool checked) {
        emit ToggleMuteRequested(sessionId_, checked);
    });
    connect(reopenButton_, &QPushButton::clicked, this, [this]() {
        emit ReopenRequested(sessionId_);
    });
    connect(snapshotButton_, &QPushButton::clicked, this, [this]() {
        emit SnapshotRequested(sessionId_);
    });
    connect(popoutButton_, &QPushButton::clicked, this, [this]() {
        emit PopoutRequested(sessionId_);
    });
    connect(removeButton_, &QPushButton::clicked, this, [this]() {
        emit RemoveRequested(sessionId_);
    });
}

QString MonitorTileWidget::SessionId() const
{
    return sessionId_;
}

VideoWidget* MonitorTileWidget::VideoSurface() const
{
    return video_;
}

void MonitorTileWidget::SetSnapshot(const MonitorSessionSnapshot& snapshot)
{
    titleLabel_->setText(snapshot.displayName.isEmpty() ? snapshot.sessionId : snapshot.displayName);
    stateLabel_->setText(StateText(snapshot));
    QString detailText = DetailText(snapshot);
    if (snapshot.detectorEnabled && lastDetectionCount_ >= 0)
    {
        detailText += QString("\nDET boxes: %1").arg(lastDetectionCount_);
        if (lastDetectionFrameWidth_ > 0 && lastDetectionFrameHeight_ > 0)
            detailText += QString("  src:%1x%2").arg(lastDetectionFrameWidth_).arg(lastDetectionFrameHeight_);
    }
    detailLabel_->setText(detailText);

    const QSignalBlocker blockDetector(detectorButton_);
    const QSignalBlocker blockAsr(asrButton_);
    const QSignalBlocker blockRecord(recordButton_);
    const QSignalBlocker blockMute(muteButton_);
    detectorButton_->setChecked(snapshot.detectorEnabled);
    asrButton_->setChecked(snapshot.asrRequested || snapshot.asrEnabled);
    recordButton_->setChecked(snapshot.recordingEnabled);
    muteButton_->setChecked(snapshot.muted);

    detectorButton_->setStyleSheet(ButtonStyle(snapshot.detectorEnabled, "rgba(40, 120, 210, 220)"));
    asrButton_->setStyleSheet(ButtonStyle(snapshot.asrRequested || snapshot.asrEnabled, "rgba(34, 139, 130, 220)"));
    recordButton_->setStyleSheet(ButtonStyle(snapshot.recordingEnabled, "rgba(190, 46, 46, 230)"));
    muteButton_->setStyleSheet(ButtonStyle(snapshot.muted, "rgba(90, 90, 90, 220)"));
    reopenButton_->setStyleSheet(ButtonStyle(false, QString()));
    snapshotButton_->setStyleSheet(ButtonStyle(false, QString()));
    popoutButton_->setStyleSheet(ButtonStyle(false, QString()));
    removeButton_->setStyleSheet(ButtonStyle(false, QString()));

    const bool alarmBlink = snapshot.alarmActive && !snapshot.alarmAcknowledged
        && ((QDateTime::currentMSecsSinceEpoch() / 350) % 2 == 0);
    QString borderColor = snapshot.selected ? "#0A84FF" : "rgba(255,255,255,28)";
    if (snapshot.alarmActive)
        borderColor = alarmBlink ? "#FF453A" : "#FF9F0A";
    setStyleSheet(QString(
        "MonitorTileWidget { background: rgba(12, 16, 22, 238); border: 2px solid %1; border-radius: 8px; }"
        "QLabel { color: #F5F5F7; }")
        .arg(borderColor));

    video_->setDetectionOverlay(snapshot.detectorEnabled);
    if (!snapshot.detectorEnabled)
    {
        lastDetectionCount_ = -1;
        lastDetectionFrameWidth_ = 0;
        lastDetectionFrameHeight_ = 0;
        DetectionResult empty;
        video_->updateDetections(empty);
    }
    const long long subtitleClockMs = std::max(0LL, snapshot.playback.positionMs);
    video_->setSubtitleClock(subtitleClockMs, 0);
    if (!snapshot.asrRequested && !snapshot.asrEnabled)
        ClearAsrSubtitle();
    RefreshFrame();
}

void MonitorTileWidget::ApplyDetections(const DetectionResult& result)
{
    lastDetectionCount_ = static_cast<int>(result.boxes.size());
    lastDetectionFrameWidth_ = result.frameWidth;
    lastDetectionFrameHeight_ = result.frameHeight;
    video_->updateDetections(result);
}

void MonitorTileWidget::ApplyAsrSubtitle(
    const QString& text,
    long long startMs,
    long long endMs,
    quint64 generation,
    quint64 serial)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty() || endMs <= startMs)
        return;

    if (asrClearBarrierKnown_
        && !IsAsrEpochGreater(generation, serial, asrClearBarrierGeneration_, asrClearBarrierSerial_))
    {
        return;
    }

    if (asrTrackEpochKnown_)
    {
        if (IsAsrEpochGreater(asrTrackGeneration_, asrTrackSerial_, generation, serial))
            return;

        if (IsAsrEpochGreater(generation, serial, asrTrackGeneration_, asrTrackSerial_))
        {
            asrTrack_.cues.clear();
            asrTrackGeneration_ = generation;
            asrTrackSerial_ = serial;
            asrTrackEpochKnown_ = true;
            asrClearBarrierKnown_ = false;
            TouchAsrTrack();
        }
    }
    else
    {
        asrTrackGeneration_ = generation;
        asrTrackSerial_ = serial;
        asrTrackEpochKnown_ = true;
        asrClearBarrierKnown_ = false;
    }

    EnsureAsrTrackInitialized();
    auto cueIt = std::find_if(asrTrack_.cues.begin(), asrTrack_.cues.end(),
        [startMs, endMs, &trimmed](const SubtitleCue& cue)
        {
            return cue.startMs == startMs && cue.endMs == endMs && cue.text == trimmed;
        });
    if (cueIt != asrTrack_.cues.end())
        return;

    const auto insertIt = std::lower_bound(asrTrack_.cues.begin(), asrTrack_.cues.end(), startMs,
        [](const SubtitleCue& cue, long long cueStartMs)
        {
            return cue.startMs < cueStartMs;
        });
    asrTrack_.cues.insert(insertIt, SubtitleCue{ startMs, endMs, trimmed });
    if (asrTrack_.cues.size() > kMonitorAsrMaxCueCount)
        asrTrack_.cues.remove(0, asrTrack_.cues.size() - kMonitorAsrMaxCueCount);

    TouchAsrTrack();
    video_->hideSubtitleStatusOsd();
    video_->setSubtitleTrack(&asrTrack_);
    RefreshFrame();
}

void MonitorTileWidget::ClearAsrSubtitle()
{
    if (asrTrackEpochKnown_)
    {
        asrClearBarrierGeneration_ = asrTrackGeneration_;
        asrClearBarrierSerial_ = asrTrackSerial_;
        asrClearBarrierKnown_ = true;
    }

    asrTrackEpochKnown_ = false;
    asrTrackGeneration_ = 0;
    asrTrackSerial_ = 0;
    if (asrTrack_.id == 0 && asrTrack_.cues.isEmpty())
    {
        video_->hideSubtitleStatusOsd();
        video_->clearSubtitleTrack();
        return;
    }

    asrTrack_ = SubtitleTrack{};
    video_->hideSubtitleStatusOsd();
    video_->clearSubtitleTrack();
    RefreshFrame();
}

bool MonitorTileWidget::IsAsrEpochGreater(
    quint64 lhsGeneration,
    quint64 lhsSerial,
    quint64 rhsGeneration,
    quint64 rhsSerial)
{
    return lhsGeneration > rhsGeneration
        || (lhsGeneration == rhsGeneration && lhsSerial > rhsSerial);
}

void MonitorTileWidget::EnsureAsrTrackInitialized()
{
    if (asrTrack_.id != 0)
        return;

    asrTrack_.id = 1;
    asrTrack_.source = SubtitleSourceType::Whisper;
    asrTrack_.name = "Monitor ASR";
    asrTrack_.renderWithLibass = false;
}

void MonitorTileWidget::TouchAsrTrack()
{
    EnsureAsrTrackInitialized();
    ++asrTrack_.revision;
}

void MonitorTileWidget::contextMenuEvent(QContextMenuEvent* event)
{
    if (!event)
        return;

    emit Activated(sessionId_);

    QMenu menu(this);
    QtUiTheme::ApplyMenuStyle(menu);
    QAction* disableRecordingAction = menu.addAction(QStringLiteral(u"\u5426"));
    QAction* recordSingleAction = menu.addAction(QStringLiteral(u"\u5f55\u5236"));
    QAction* selectedAction = menu.exec(event->globalPos());
    if (!selectedAction)
        return;

    if (selectedAction == disableRecordingAction)
        emit RecordingMenuRequested(sessionId_, RecordingMenuDisable);
    else if (selectedAction == recordSingleAction)
        emit RecordingMenuRequested(sessionId_, RecordingMenuSingle);
}

void MonitorTileWidget::mousePressEvent(QMouseEvent* event)
{
    if (event && event->button() == Qt::LeftButton)
        dragStartPos_ = event->pos();

    QWidget::mousePressEvent(event);
    if (event && event->button() == Qt::LeftButton)
        emit Activated(sessionId_);
}

void MonitorTileWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
    QWidget::mouseDoubleClickEvent(event);
    if (event && event->button() == Qt::LeftButton)
        emit MaximizeRequested(sessionId_);
}

void MonitorTileWidget::mouseMoveEvent(QMouseEvent* event)
{
    QWidget::mouseMoveEvent(event);
    if (!event || !(event->buttons() & Qt::LeftButton))
        return;
    if ((event->pos() - dragStartPos_).manhattanLength() < QApplication::startDragDistance())
        return;

    auto* mimeData = new QMimeData();
    mimeData->setData("application/x-monitor-tile", sessionId_.toUtf8());

    QDrag drag(this);
    drag.setMimeData(mimeData);
    drag.setPixmap(grab().scaled(260, 180, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    drag.exec(Qt::MoveAction);
}

void MonitorTileWidget::RefreshFrame()
{
    if (video_)
        video_->update();
}

MonitorTilePopoutWindow::MonitorTilePopoutWindow(const QString& sessionId, QWidget* parent)
    : QWidget(parent)
    , sessionId_(sessionId)
{
    setAttribute(Qt::WA_DeleteOnClose, true);
    setWindowTitle(QString("Monitor Tile - %1").arg(sessionId_));
    resize(960, 540);

    layout_ = new QVBoxLayout(this);
    layout_->setContentsMargins(0, 0, 0, 0);
    layout_->setSpacing(0);
}

QString MonitorTilePopoutWindow::SessionId() const
{
    return sessionId_;
}

void MonitorTilePopoutWindow::SetTile(MonitorTileWidget* tile)
{
    if (!layout_ || !tile)
        return;

    if (tile_ == tile)
        return;

    tile_ = tile;
    layout_->addWidget(tile);
}

MonitorTileWidget* MonitorTilePopoutWindow::Tile() const
{
    return tile_;
}

void MonitorTilePopoutWindow::closeEvent(QCloseEvent* event)
{
    emit Closing(sessionId_);
    QWidget::closeEvent(event);
}
