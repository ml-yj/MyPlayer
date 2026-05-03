#include "feature_view_qt.h"
#include "qt_ui_theme.h"

#include "../../ui/video/video_widget.h"
#include "../../features/detector/detector_types.h"

#include <QMetaObject>
#include <QMenu>
#include <QPushButton>
#include <QThread>
#include <QWidget>

FeatureViewQt::FeatureViewQt(QWidget* host, VideoWidget* videoWidget)
    : host_(host)
    , videoWidget_(videoWidget)
{

    asrButton_ = new QPushButton("ASR", host_);
    asrButton_->setFixedSize(64, 36);
    asrButton_->setCheckable(true);

    QObject::connect(asrButton_, &QPushButton::clicked, [this]() {
        if (asrToggleHandler_)
            asrToggleHandler_();
        });

    anime4kButton_ = new QPushButton("A4K", host_);
    anime4kButton_->setFixedSize(64, 36);
    anime4kButton_->setCheckable(true);
    QObject::connect(anime4kButton_, &QPushButton::clicked, [this]() {
        if (anime4kToggleHandler_)
            anime4kToggleHandler_();
        });

    detectorButton_ = new QPushButton("DET", host_);
    detectorButton_->setFixedSize(56, 36);
    detectorButton_->setCheckable(true);

    detectorButton_->setContextMenuPolicy(Qt::CustomContextMenu);

    QObject::connect(detectorButton_, &QPushButton::clicked, [this]() {
        if (detectorToggleHandler_)
            detectorToggleHandler_();
        });

    QObject::connect(detectorButton_, &QPushButton::customContextMenuRequested, [this](const QPoint& pos) {
        if (detectorMenuHandler_)
            detectorMenuHandler_(pos);
        });
}

void FeatureViewQt::SetAsrToggleHandler(std::function<void()> handler) { asrToggleHandler_ = std::move(handler); }
void FeatureViewQt::SetAnime4KToggleHandler(std::function<void()> handler) { anime4kToggleHandler_ = std::move(handler); }
void FeatureViewQt::SetDetectorToggleHandler(std::function<void()> handler) { detectorToggleHandler_ = std::move(handler); }
void FeatureViewQt::SetDetectorMenuHandler(std::function<void(const QPoint&)> handler) { detectorMenuHandler_ = std::move(handler); }

QList<QWidget*> FeatureViewQt::AutoHideWidgets() const
{

    return { asrButton_, anime4kButton_, detectorButton_ };
}

void FeatureViewQt::InstallEventFilters(QObject* filter) const
{
    if (!filter) return;
    for (QWidget* widget : AutoHideWidgets())
    {
        if (widget)
            widget->installEventFilter(filter);
    }
}

QObject* FeatureViewQt::ContextObject() const { return host_; }

QPushButton* FeatureViewQt::AsrButton() const { return asrButton_; }
QPushButton* FeatureViewQt::Anime4KButton() const { return anime4kButton_; }
QPushButton* FeatureViewQt::DetectorButton() const { return detectorButton_; }

void FeatureViewQt::SetAsrChecked(bool checked) { if (asrButton_) asrButton_->setChecked(checked); }
void FeatureViewQt::SetAnime4KChecked(bool checked) { if (anime4kButton_) anime4kButton_->setChecked(checked); }
void FeatureViewQt::SetDetectorChecked(bool checked) { if (detectorButton_) detectorButton_->setChecked(checked); }

void FeatureViewQt::SetAiButtonsEnabled(bool enabled)
{
    if (asrButton_) asrButton_->setEnabled(enabled);
    if (anime4kButton_) anime4kButton_->setEnabled(enabled);
    if (detectorButton_) detectorButton_->setEnabled(enabled);
}

void FeatureViewQt::SetAnime4KEnabled(bool enabled) { if (videoWidget_) videoWidget_->setAnime4KEnabled(enabled); }
bool FeatureViewQt::IsAnime4KEnabled() const { return videoWidget_ && videoWidget_->isAnime4KEnabled(); }
QString FeatureViewQt::Anime4KBackendSummary() const { return videoWidget_ ? videoWidget_->GetAnime4KBackendSummary() : QString("GPU"); }

void FeatureViewQt::SetDetectionOverlayEnabled(bool enabled) { if (videoWidget_) videoWidget_->setDetectionOverlay(enabled); }
void FeatureViewQt::UpdateDetections(const DetectionResult& result) { if (videoWidget_) videoWidget_->updateDetections(result); }
void FeatureViewQt::ClearDetections() { if (videoWidget_) videoWidget_->updateDetections(DetectionResult{}); }

void FeatureViewQt::ShowAnime4KOsd(const QString& text) { if (videoWidget_) videoWidget_->showFeatureStatusOsd(text); }
void FeatureViewQt::HideAnime4KOsd() { if (videoWidget_) videoWidget_->hideFeatureStatusOsd(); }
void FeatureViewQt::ShowDetectorOsd(const QString& text) { if (videoWidget_) videoWidget_->showFeatureStatusOsd(text); }
void FeatureViewQt::HideDetectorOsd() { if (videoWidget_) videoWidget_->hideFeatureStatusOsd(); }

void FeatureViewQt::RelayoutOsd(int videoWidth, int hostHeight) { (void)videoWidth; (void)hostHeight; }

void FeatureViewQt::ShowWarning(const QString& title, const QString& text)
{
    if (!host_) return;

    if (QThread::currentThread() != host_->thread())
    {
        QMetaObject::invokeMethod(host_, [this, title, text]() { ShowWarning(title, text); }, Qt::QueuedConnection);
        return;
    }

    QtUiTheme::ShowWarning(host_, title, text);
}

DetectorMenuCommand FeatureViewQt::ShowDetectorMenu(
    const QPoint& pos, const QString& modelStatus, const QString& labelsStatus, bool canClearLabels)
{
    if (!detectorButton_) return DetectorMenuCommand::None;

    QMenu menu(host_);
    QtUiTheme::ApplyMenuStyle(menu);

    QAction* title = menu.addAction("Detector");
    title->setEnabled(false);
    menu.addSeparator();

    QAction* modelAction = menu.addAction(modelStatus);
    modelAction->setEnabled(false);
    QAction* labelsAction = menu.addAction(labelsStatus);
    labelsAction->setEnabled(false);
    menu.addSeparator();

    QAction* loadModelAction = menu.addAction("Load model...");
    QAction* loadLabelsAction = menu.addAction("Load labels...");
    QAction* clearLabelsAction = menu.addAction("Use auto/default labels");
    clearLabelsAction->setEnabled(canClearLabels);

    QAction* chosen = menu.exec(detectorButton_->mapToGlobal(pos));

    if (chosen == loadModelAction) return DetectorMenuCommand::LoadModel;
    if (chosen == loadLabelsAction) return DetectorMenuCommand::LoadLabels;
    if (chosen == clearLabelsAction) return DetectorMenuCommand::ClearLabels;

    return DetectorMenuCommand::None;
}

QString FeatureViewQt::SelectDetectorModelPath(const QString& initialDir)
{
    return QtUiTheme::GetOpenFileName(
        host_, "Select Detector Model", initialDir, "ONNX Model (*.onnx);;All Files (*)");
}

QString FeatureViewQt::SelectDetectorLabelsPath(const QString& initialDir)
{
    return QtUiTheme::GetOpenFileName(
        host_, "Select Labels File", initialDir, "Text Files (*.txt *.labels);;All Files (*)");
}
