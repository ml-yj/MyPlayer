#pragma once
#include <QWidget>

namespace MyPlayerUiHelpers
{

    inline constexpr int kControlAutoHideTicks = 75;

    inline bool LayoutAllowsControl(const QWidget* widget)
    {

        if (!widget)
            return false;

        const QVariant value = widget->property("layoutVisible");

        return !value.isValid() || value.toBool();
    }

    inline void ApplyManagedVisibility(QWidget* widget, bool layoutVisible, bool controlsShown)
    {

        if (!widget)
            return;

        widget->setProperty("layoutVisible", layoutVisible);

        widget->setVisible(controlsShown && layoutVisible);
    }

}
