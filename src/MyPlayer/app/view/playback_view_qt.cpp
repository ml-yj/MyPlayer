
#include "playback_view_qt.h"
#include "qt_ui_theme.h"

#include "ui_my_player.h"

#include "../../ui/video/video_widget.h"

#include <QMetaObject>
#include <QPushButton>
#include <QThread>
#include <QWidget>

PlaybackViewQt::PlaybackViewQt(QWidget* host, Ui::MyPlayerClass* ui, QPushButton* audioTrackButton)
    : host_(host)
    , ui_(ui)
    , audioTrackButton_(audioTrackButton)
{
}

QWidget* PlaybackViewQt::HostWidget() const
{
    return host_;
}

VideoWidget* PlaybackViewQt::VideoWidgetHandle() const
{
    return ui_ ? ui_->video : nullptr;
}

QString PlaybackViewQt::UrlText() const
{
    return (ui_ && ui_->urlInput) ? ui_->urlInput->text() : QString();
}

void PlaybackViewQt::SetUrlText(const QString& text)
{
    if (ui_ && ui_->urlInput)
        ui_->urlInput->setText(text);
}

int PlaybackViewQt::VolumeValue() const
{
    return (ui_ && ui_->volumeSlider) ? ui_->volumeSlider->value() : 0;
}

int PlaybackViewQt::SeekValue() const
{
    return (ui_ && ui_->playPos) ? ui_->playPos->value() : 0;
}

int PlaybackViewQt::SeekMaximum() const
{
    return (ui_ && ui_->playPos) ? ui_->playPos->maximum() : 0;
}

void PlaybackViewQt::SetSeekValue(int value)
{
    if (ui_ && ui_->playPos)
        ui_->playPos->setValue(value);
}

void PlaybackViewQt::SetPlayButtonText(const QString& text)
{
    if (ui_ && ui_->isplay)
        ui_->isplay->setText(text);
}

void PlaybackViewQt::SetSpeedText(const QString& text)
{
    if (ui_ && ui_->speedLabel)
        ui_->speedLabel->setText(text);
}

void PlaybackViewQt::SetSpeedValue(int value)
{
    if (ui_ && ui_->speedSlider)
        ui_->speedSlider->setValue(value);
}

void PlaybackViewQt::SetSpeedControlsEnabled(bool enabled)
{
    if (ui_ && ui_->speedSlider)
        ui_->speedSlider->setEnabled(enabled);
    if (ui_ && ui_->speedLabel)
        ui_->speedLabel->setEnabled(enabled);
}

void PlaybackViewQt::SetWindowTitleText(const QString& text)
{
    if (host_)
        host_->setWindowTitle(text);
}

void PlaybackViewQt::SetPreferLiveRendering(bool isLive)
{
    if (ui_ && ui_->video)
        ui_->video->setPreferLiveRendering(isLive);
}

void PlaybackViewQt::SetSeekVisible(bool visible)
{
    if (ui_ && ui_->playPos)
        ui_->playPos->setVisible(visible);
}

void PlaybackViewQt::SetLiveVisible(bool visible)
{
    if (ui_ && ui_->liveLabel)
        ui_->liveLabel->setVisible(visible);
}

void PlaybackViewQt::SetTimeText(const QString& text)
{
    if (ui_ && ui_->timeLabel)
        ui_->timeLabel->setText(text);
}

void PlaybackViewQt::SetAudioTrackButtonVisible(bool visible)
{
    if (audioTrackButton_)
        audioTrackButton_->setVisible(visible);
}

void PlaybackViewQt::SetAudioTrackButtonText(const QString& text)
{
    if (audioTrackButton_)
        audioTrackButton_->setText(text);
}

void PlaybackViewQt::SetAudioTrackButtonToolTip(const QString& text)
{
    if (audioTrackButton_)
        audioTrackButton_->setToolTip(text);
}

QPoint PlaybackViewQt::MapAudioTrackMenuToGlobal(const QPoint& pos) const
{
    return audioTrackButton_ ? audioTrackButton_->mapToGlobal(pos) : pos;
}

QStringList PlaybackViewQt::SelectVideoFiles() const
{
    if (!host_) return {};

    return QtUiTheme::GetOpenFileNames(host_, "Select Video Files",
        QString(), "Video (*.mp4 *.mkv *.avi *.mov *.flv *.wmv *.ts *.webm *.m4v *.mpg *.mpeg);;All (*)");
}

void PlaybackViewQt::ShowInfoMessage(const QString& title, const QString& text) const
{
    if (!host_) return;

    if (QThread::currentThread() != host_->thread())
    {

        QMetaObject::invokeMethod(host_, [this, title, text]() { ShowInfoMessage(title, text); }, Qt::QueuedConnection);
        return;
    }

    QtUiTheme::ShowInfo(host_, title, text);
}

void PlaybackViewQt::ShowWarningMessage(const QString& title, const QString& text) const
{
    if (!host_) return;

    if (QThread::currentThread() != host_->thread())
    {
        QMetaObject::invokeMethod(host_, [this, title, text]() { ShowWarningMessage(title, text); }, Qt::QueuedConnection);
        return;
    }

    QtUiTheme::ShowWarning(host_, title, text);
}
