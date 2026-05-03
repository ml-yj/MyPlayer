

#include "monitor_wall_window.h"

#include "monitor_tile_widget.h"

#include <QAbstractItemView>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QEvent>
#include <QComboBox>
#include <QGridLayout>
#include <QLayoutItem>
#include <QMimeData>
#include <QPushButton>
#include <QScrollArea>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QWidget>

#include <algorithm>

namespace
{

int IndexFromPreset(MonitorWallLayoutPreset preset)
{
    switch (preset)
    {
    case MonitorWallLayoutPreset::Single:  return 0;
    case MonitorWallLayoutPreset::Grid2x2: return 1;
    case MonitorWallLayoutPreset::Grid3x3: return 2;
    case MonitorWallLayoutPreset::Grid4x4: return 3;
    case MonitorWallLayoutPreset::Custom:  return 4;
    }
    return 4;
}

QString EmptySlotStyle(bool active)
{
    const QString borderColor = active ? QStringLiteral("#0A84FF") : QStringLiteral("rgba(255,255,255,24)");
    const QString background = active ? QStringLiteral("rgba(10,132,255,28)") : QStringLiteral("rgba(255,255,255,6)");
    return QString(
        "QPushButton { background: %1; border: 2px dashed %2; border-radius: 8px; "
        "color: #8EA3B5; font-size: 13px; font-weight: 600; text-align: center; padding: 12px; }"
        "QPushButton:hover { background: rgba(255,255,255,12); }")
        .arg(background, borderColor);
}

QString LayoutSignature(const MonitorWorkspaceSnapshot& snapshot)
{
    QStringList tokens;
    tokens << QString::number(snapshot.layout.rows)
           << QString::number(snapshot.layout.columns)
           << QString::number(static_cast<int>(snapshot.layout.preset));
    for (const MonitorTilePlacement& placement : snapshot.layout.placements)
    {
        tokens << QString("%1@%2,%3,%4,%5")
            .arg(placement.sessionId)
            .arg(placement.row)
            .arg(placement.column)
            .arg(placement.rowSpan)
            .arg(placement.columnSpan);
    }
    return tokens.join('|');
}
}

void MonitorWallWindow::ApplyWorkspaceSnapshot(
    const MonitorWorkspaceSnapshot& snapshot,
    const QMap<QString, MonitorTileWidget*>& tiles)
{
    if (!gridLayout_)
        return;

    const QString signature = LayoutSignature(snapshot);
    if (signature != lastWallLayoutSignature_)
    {
        for (QWidget* placeholder : emptySlotWidgets_)
            delete placeholder;
        emptySlotWidgets_.clear();
        ClearGridLayout(gridLayout_);

        constexpr int kMaxGridDimension = 4;
        for (int row = 0; row < kMaxGridDimension; ++row)
        {
            gridLayout_->setRowStretch(row, 0);
            gridLayout_->setRowMinimumHeight(row, 0);
        }
        for (int column = 0; column < kMaxGridDimension; ++column)
        {
            gridLayout_->setColumnStretch(column, 0);
            gridLayout_->setColumnMinimumWidth(column, 0);
        }

        const int slotCount = std::max(1, snapshot.layout.rows * snapshot.layout.columns);
        if (activeWallSlotIndex_ >= slotCount)
            activeWallSlotIndex_ = -1;

        QSet<QString> occupiedCells;
        for (const MonitorTilePlacement& placement : snapshot.layout.placements)
        {
            auto it = tiles.find(placement.sessionId);
            if (it == tiles.end() || !it.value())
                continue;
            gridLayout_->addWidget(it.value(), placement.row, placement.column, placement.rowSpan, placement.columnSpan);
            occupiedCells.insert(QString("%1:%2").arg(placement.row).arg(placement.column));
        }

        for (int row = 0; row < std::max(1, snapshot.layout.rows); ++row)
        {
            for (int column = 0; column < std::max(1, snapshot.layout.columns); ++column)
            {
                const QString key = QString("%1:%2").arg(row).arg(column);
                if (occupiedCells.contains(key))
                    continue;
                QWidget* placeholder = CreateEmptySlotWidget(row * std::max(1, snapshot.layout.columns) + column);
                emptySlotWidgets_.append(placeholder);
                gridLayout_->addWidget(placeholder, row, column, 1, 1);
            }
        }

        for (int row = 0; row < snapshot.layout.rows; ++row)
        {
            gridLayout_->setRowMinimumHeight(row, 0);
            gridLayout_->setRowStretch(row, 1);
        }
        for (int column = 0; column < snapshot.layout.columns; ++column)
        {
            gridLayout_->setColumnMinimumWidth(column, 0);
            gridLayout_->setColumnStretch(column, 1);
        }

        if (gridHost_)
            gridHost_->updateGeometry();

        lastWallLayoutSignature_ = signature;
    }

    if (layoutCombo_)
    {
        const bool popupVisible = layoutCombo_->view() && layoutCombo_->view()->isVisible();
        if (!popupVisible)
        {
            const QSignalBlocker blocker(layoutCombo_);
            layoutCombo_->setCurrentIndex(IndexFromPreset(snapshot.layout.preset));
        }
    }

    SetStatusSummaryText(QString("sessions=%1  selected=%2  audio=%3")
        .arg(snapshot.sessions.size())
        .arg(snapshot.selectedSessionId.isEmpty() ? "-" : snapshot.selectedSessionId)
        .arg(snapshot.audioSessionId.isEmpty() ? "-" : snapshot.audioSessionId));
}

void MonitorWallWindow::closeEvent(QCloseEvent* event)
{
    QWidget::closeEvent(event);
    emit WindowVisibilityChanged(false);
}

bool MonitorWallWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == gridHost_ && event)
    {
        if (event->type() == QEvent::DragEnter)
        {
            auto* dragEvent = static_cast<QDragEnterEvent*>(event);
            if (dragEvent->mimeData()->hasFormat("application/x-monitor-tile"))
            {
                dragEvent->acceptProposedAction();
                return true;
            }
        }
        else if (event->type() == QEvent::DragMove)
        {
            auto* dragEvent = static_cast<QDragMoveEvent*>(event);
            if (dragEvent->mimeData()->hasFormat("application/x-monitor-tile"))
            {
                dragEvent->acceptProposedAction();
                return true;
            }
        }
        else if (event->type() == QEvent::Drop)
        {
            auto* dropEvent = static_cast<QDropEvent*>(event);
            const QByteArray data = dropEvent->mimeData()->data("application/x-monitor-tile");
            const QString draggedSessionId = QString::fromUtf8(data).trimmed();
            if (!draggedSessionId.isEmpty())
            {
                MonitorTileWidget* targetTile = ResolveTileAt(dropEvent->position().toPoint());
                const QString targetSessionId = targetTile ? targetTile->SessionId() : QString{};
                emit TileReorderRequested(draggedSessionId, targetSessionId);
                dropEvent->acceptProposedAction();
                return true;
            }
        }
    }

    return QWidget::eventFilter(watched, event);
}

void MonitorWallWindow::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    SetWindowFullscreen(isFullScreen());
    emit WindowVisibilityChanged(true);
}

void MonitorWallWindow::ClearGridLayout(QGridLayout* layout)
{
    if (!layout)
        return;

    while (QLayoutItem* item = layout->takeAt(0))
        delete item;
}

MonitorTileWidget* MonitorWallWindow::ResolveTileAt(const QPoint& pos) const
{
    if (!gridHost_)
        return nullptr;

    QWidget* widget = gridHost_->childAt(pos);
    while (widget && !qobject_cast<MonitorTileWidget*>(widget))
        widget = widget->parentWidget();
    return qobject_cast<MonitorTileWidget*>(widget);
}

QWidget* MonitorWallWindow::CreateEmptySlotWidget(int slotIndex)
{
    auto* button = new QPushButton(QString("Empty Slot %1\nClick to place selected camera").arg(slotIndex + 1), gridHost_);
    button->setProperty("slotIndex", slotIndex);
    button->setMinimumSize(0, 0);
    button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    button->setStyleSheet(EmptySlotStyle(slotIndex == activeWallSlotIndex_));
    connect(button, &QPushButton::clicked, this, [this, slotIndex]() {
        activeWallSlotIndex_ = slotIndex;
        for (QWidget* placeholder : emptySlotWidgets_)
        {
            auto* emptyButton = qobject_cast<QPushButton*>(placeholder);
            if (!emptyButton)
                continue;
            const int placeholderSlotIndex = emptyButton->property("slotIndex").toInt();
            emptyButton->setStyleSheet(EmptySlotStyle(placeholderSlotIndex == activeWallSlotIndex_));
        }
        emit WallSlotActivated(slotIndex);
    });
    return button;
}
