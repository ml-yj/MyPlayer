
#pragma once

#include <QList>
#include <QRect>
#include <QString>

#include <functional>
#include <memory>

class QObject;
class QWidget;

namespace Ui { class MyPlayerClass; }

class DisplayAdjustmentController
{
public:

    DisplayAdjustmentController(
        QWidget* host,
        Ui::MyPlayerClass* ui,

        std::function<int()> videoAreaWidthProvider,
        std::function<int()> hostHeightProvider);

    QList<QWidget*> AutoHideWidgets() const;

    void InstallEventFilters(QObject* filter) const;

    void SetPanelVisible(bool visible);
    bool IsPanelVisible() const;

    void LayoutPanel(const QRect& anchorRect);

    void TakeScreenshot();

    void TickUi();

private:

    void OnBrightnessChanged(int value);
    void OnContrastChanged(int value);
    void OnSaturationChanged(int value);
    void ResetBCS();

    void ShowCenteredOsd(const QString& text);
    void HideCenteredOsd();

    QWidget* host_ = nullptr;
    Ui::MyPlayerClass* ui_ = nullptr;

    std::function<int()> videoAreaWidthProvider_;
    std::function<int()> hostHeightProvider_;

    int screenshotOsdTimer_ = 0;
    int bcsOsdTimer_ = 0;
};
