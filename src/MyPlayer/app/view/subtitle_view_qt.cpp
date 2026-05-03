
#include "subtitle_view_qt.h"
#include "qt_ui_theme.h"

#include "../../ui/video/video_widget.h"

#include <QLabel>
#include <QMetaObject>
#include <QPushButton>
#include <QThread>
#include <QWidget>

SubtitleViewQt::SubtitleViewQt(QWidget* host, VideoWidget* video, QPushButton* trackButton, QLabel* osdLabel,
    std::function<int()> videoAreaWidthProvider)

    : host_(host)
    , video_(video)
    , trackButton_(trackButton)
    , osdLabel_(osdLabel)

    , videoAreaWidthProvider_(std::move(videoAreaWidthProvider))
{
}

QWidget* SubtitleViewQt::HostWidget() const
{
    return host_;
}

VideoWidget* SubtitleViewQt::VideoWidgetHandle() const
{
    return video_;
}

int SubtitleViewQt::VideoAreaWidth() const
{

    return videoAreaWidthProvider_ ? videoAreaWidthProvider_() : 0;
}

int SubtitleViewQt::HostHeight() const
{

    return host_ ? host_->height() : 0;
}

void SubtitleViewQt::UpdateTrackButton(const QString& text, bool checked, const QString& tooltip)
{
    if (!trackButton_) return;

    trackButton_->setText(text);
    trackButton_->setChecked(checked);
    trackButton_->setToolTip(tooltip);
}

QPoint SubtitleViewQt::MapTrackMenuToGlobal(const QPoint& pos) const
{

    return trackButton_ ? trackButton_->mapToGlobal(pos) : pos;
}

void SubtitleViewQt::SetOsdText(const QString& text)
{
    osdText_ = text;

    if (osdVisible_ && video_)
        video_->showSubtitleStatusOsd(osdText_);
}

void SubtitleViewQt::ShowOsd()
{
    osdVisible_ = true;
    if (video_)
        video_->showSubtitleStatusOsd(osdText_);
}

void SubtitleViewQt::HideOsd()
{
    osdVisible_ = false;
    if (video_)
        video_->hideSubtitleStatusOsd();
}

bool SubtitleViewQt::IsOsdVisible() const
{
    return osdVisible_;
}

int SubtitleViewQt::OsdWidth() const { return 0; }
void SubtitleViewQt::MoveOsd(int x, int y) { (void)x; (void)y; }
void SubtitleViewQt::RaiseOsd() {}

QStringList SubtitleViewQt::SelectSubtitleFiles(const QString& startDir) const
{
    if (!host_) return {};

    return QtUiTheme::GetOpenFileNames(host_, "Open Subtitle Files", startDir, "SubRip (*.srt)");
}

QString SubtitleViewQt::SelectSubtitleSavePath(const QString& startPath) const
{
    if (!host_) return {};
    return QtUiTheme::GetSaveFileName(host_, "Export ASR Subtitle", startPath, "SubRip (*.srt)");
}

void SubtitleViewQt::ShowInfoMessage(const QString& title, const QString& text) const
{
    if (!host_) return;

    if (QThread::currentThread() != host_->thread())
    {

        QMetaObject::invokeMethod(host_, [this, title, text]() {
            ShowInfoMessage(title, text);
            }, Qt::QueuedConnection);
        return;
    }

    QtUiTheme::ShowInfo(host_, title, text);
}

void SubtitleViewQt::ShowWarningMessage(const QString& title, const QString& text) const
{
    if (!host_) return;

    if (QThread::currentThread() != host_->thread())
    {
        QMetaObject::invokeMethod(host_, [this, title, text]() {
            ShowWarningMessage(title, text);
            }, Qt::QueuedConnection);
        return;
    }

    QtUiTheme::ShowWarning(host_, title, text);
}
