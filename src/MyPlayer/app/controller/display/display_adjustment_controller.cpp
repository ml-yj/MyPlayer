

#include "display_adjustment_controller.h"

#include "ui_my_player.h"
#include "../../../ui/video/video_widget.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QPushButton>
#include <QStandardPaths>

#include <algorithm>
#include <utility>

DisplayAdjustmentController::DisplayAdjustmentController(
    QWidget* host,
    Ui::MyPlayerClass* ui,
    std::function<int()> videoAreaWidthProvider,
    std::function<int()> hostHeightProvider)
    : host_(host)
    , ui_(ui)
    , videoAreaWidthProvider_(std::move(videoAreaWidthProvider))
    , hostHeightProvider_(std::move(hostHeightProvider))
{
    if (ui_)
    {
        QObject::connect(
            ui_->brightnessSlider, &QSlider::valueChanged,
            [this](int value) { OnBrightnessChanged(value); });
        QObject::connect(
            ui_->contrastSlider, &QSlider::valueChanged,
            [this](int value) { OnContrastChanged(value); });
        QObject::connect(
            ui_->saturationSlider, &QSlider::valueChanged,
            [this](int value) { OnSaturationChanged(value); });
        QObject::connect(
            ui_->bcsResetBtn, &QPushButton::clicked,
            [this]() { ResetBCS(); });
        SetPanelVisible(false);
    }
}

QList<QWidget*> DisplayAdjustmentController::AutoHideWidgets() const
{
    if (!ui_)
        return {};

    return {
        ui_->bcsPanel,
        ui_->brightnessSlider,
        ui_->contrastSlider,
        ui_->saturationSlider,
        ui_->brightnessLabel,
        ui_->contrastLabel,
        ui_->saturationLabel,
        ui_->bcsResetBtn
    };
}

void DisplayAdjustmentController::InstallEventFilters(QObject* filter) const
{
    if (!filter)
        return;

    for (QWidget* widget : AutoHideWidgets())
    {
        if (widget)
            widget->installEventFilter(filter);
    }
}

void DisplayAdjustmentController::SetPanelVisible(bool visible)
{
    for (QWidget* widget : AutoHideWidgets())
    {
        if (!widget)
            continue;
        widget->setProperty("layoutVisible", visible);
        widget->setVisible(visible);
    }
}

bool DisplayAdjustmentController::IsPanelVisible() const
{
    return ui_ && ui_->bcsPanel && ui_->bcsPanel->property("layoutVisible").toBool();
}

void DisplayAdjustmentController::LayoutPanel(const QRect& anchorRect)
{
    if (!ui_ || !ui_->bcsPanel || !host_)
        return;

    const QSize panelSize = ui_->bcsPanel->sizeHint().expandedTo(QSize(100, 180));
    const int videoWidth = videoAreaWidthProvider_ ? std::max(220, videoAreaWidthProvider_()) : host_->width();
    const int hostHeight = hostHeightProvider_ ? std::max(panelSize.height(), hostHeightProvider_()) : host_->height();
    const int margin = 12;
    const int gap = 8;

    int x = anchorRect.isValid()
        ? anchorRect.left() + ((anchorRect.width() - panelSize.width()) / 2)
        : (videoWidth - panelSize.width() - margin);
    x = std::clamp(x, margin, std::max(margin, videoWidth - panelSize.width() - margin));

    int y = anchorRect.isValid()
        ? (anchorRect.top() - panelSize.height() - gap)
        : 50;
    if (anchorRect.isValid() && y < margin)
        y = anchorRect.bottom() + gap;
    y = std::clamp(y, margin, std::max(margin, hostHeight - panelSize.height() - margin));

    ui_->bcsPanel->setGeometry(x, y, panelSize.width(), panelSize.height());
    ui_->bcsPanel->raise();
}

void DisplayAdjustmentController::TakeScreenshot()
{
    if (!ui_ || !ui_->video)
        return;

    const QImage image = ui_->video->grabFramebuffer();
    if (image.isNull())
        return;

    const QString picturesDir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    const QString saveDir = picturesDir + "/MyPlayer";
    QDir().mkpath(saveDir);

    const QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    const QString filePath = saveDir + "/screenshot_" + timestamp + ".png";

    const bool ok = image.save(filePath, "PNG");
    ShowCenteredOsd(
        ok ? ("Screenshot: " + QFileInfo(filePath).fileName()) : QString("Screenshot failed!"));
    screenshotOsdTimer_ = 50;
}

void DisplayAdjustmentController::TickUi()
{
    if (screenshotOsdTimer_ > 0)
    {
        --screenshotOsdTimer_;
        if (screenshotOsdTimer_ == 0)
            HideCenteredOsd();
    }

    if (bcsOsdTimer_ > 0)
    {
        --bcsOsdTimer_;
        if (bcsOsdTimer_ == 0)
            HideCenteredOsd();
    }
}

void DisplayAdjustmentController::OnBrightnessChanged(int value)
{
    if (!ui_ || !ui_->video || !ui_->brightnessLabel)
        return;

    const float brightness = value / 200.0f;
    ui_->video->setBrightness(brightness);
    ui_->brightnessLabel->setText(QString("Bright: %1").arg(value));
    ShowCenteredOsd(QString("Brightness: %1").arg(value));
    bcsOsdTimer_ = 25;
}

void DisplayAdjustmentController::OnContrastChanged(int value)
{
    if (!ui_ || !ui_->video || !ui_->contrastLabel)
        return;

    const float contrast = value / 100.0f;
    ui_->video->setContrast(contrast);
    ui_->contrastLabel->setText(QString("Contrast: %1").arg(contrast, 0, 'f', 1));
    ShowCenteredOsd(QString("Contrast: %1").arg(contrast, 0, 'f', 1));
    bcsOsdTimer_ = 25;
}

void DisplayAdjustmentController::OnSaturationChanged(int value)
{
    if (!ui_ || !ui_->video || !ui_->saturationLabel)
        return;

    const float saturation = value / 100.0f;
    ui_->video->setSaturation(saturation);
    ui_->saturationLabel->setText(QString("Satur: %1").arg(saturation, 0, 'f', 1));
    ShowCenteredOsd(QString("Saturation: %1").arg(saturation, 0, 'f', 1));
    bcsOsdTimer_ = 25;
}

void DisplayAdjustmentController::ResetBCS()
{
    if (!ui_ || !ui_->video)
        return;

    ui_->brightnessSlider->setValue(0);
    ui_->contrastSlider->setValue(100);
    ui_->saturationSlider->setValue(100);
    ui_->video->setBCS(0.0f, 1.0f, 1.0f);
    ShowCenteredOsd("Color Reset");
    bcsOsdTimer_ = 25;
}

void DisplayAdjustmentController::ShowCenteredOsd(const QString& text)
{
    if (!ui_ || !ui_->video)
        return;

    ui_->video->showDisplayStatusOsd(text);
}

void DisplayAdjustmentController::HideCenteredOsd()
{
    if (!ui_ || !ui_->video)
        return;

    ui_->video->hideDisplayStatusOsd();
}
