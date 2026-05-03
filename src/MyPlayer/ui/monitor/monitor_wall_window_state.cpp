

#include "monitor_wall_window.h"

#include "../../core/archive/archive_path_policy.h"

#include <QCalendarWidget>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDateTime>
#include <QFont>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QSpinBox>
#include <QTextCharFormat>

#include <algorithm>

namespace
{

MonitorWallLayoutPreset PresetFromIndex(int index)
{
    switch (index)
    {
    case 0: return MonitorWallLayoutPreset::Single;
    case 1: return MonitorWallLayoutPreset::Grid2x2;
    case 2: return MonitorWallLayoutPreset::Grid3x3;
    case 3: return MonitorWallLayoutPreset::Grid4x4;
    default: return MonitorWallLayoutPreset::Custom;
    }
}

QString CameraListText(const MonitorSessionSnapshot& snapshot)
{
    const QString cameraId = snapshot.cameraId.trimmed().isEmpty()
        ? snapshot.sessionId.trimmed()
        : snapshot.cameraId.trimmed();
    const QString display = snapshot.displayName.trimmed();
    if (display.isEmpty() || display == cameraId)
        return cameraId;
    return QString("%1  |  %2").arg(cameraId, display);
}

QString PlaybackItemText(const ArchiveSegmentRecord& segment)
{
    const qint64 durationSeconds = std::max<qint64>(0, segment.startUtc.secsTo(segment.endUtc));
    const QString proxySuffix = segment.playbackProxyReady ? QStringLiteral("  MP4-PROXY") : QString{};
    return QString("%1  %2 -> %3  %4  %5 MB")
        .arg(segment.startUtc.toLocalTime().toString("yyyy-MM-dd HH:mm:ss"))
        .arg(segment.startUtc.toLocalTime().time().toString("HH:mm:ss"))
        .arg(segment.endUtc.toLocalTime().time().toString("HH:mm:ss"))
        .arg(ArchivePathPolicy::RecordingContainerLabel(segment.container))
        .arg(QString::number(static_cast<double>(segment.fileSizeBytes) / (1024.0 * 1024.0), 'f', 1))
        + QString("  (%1s)%2").arg(durationSeconds).arg(proxySuffix);
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

bool CameraListMatches(QListWidget* list, const QVector<MonitorSessionSnapshot>& sessions)
{
    if (!list || list->count() != sessions.size())
        return false;

    for (int index = 0; index < sessions.size(); ++index)
    {
        QListWidgetItem* item = list->item(index);
        if (!item || item->data(Qt::UserRole).toString() != sessions.at(index).sessionId)
            return false;
    }
    return true;
}
}

MonitorSourceDescriptor MonitorWallWindow::ReadSourceDescriptor() const
{
    MonitorSourceDescriptor source;
    source.cameraId = cameraIdEdit_ ? cameraIdEdit_->text().trimmed() : QString{};
    source.displayName = displayNameEdit_ ? displayNameEdit_->text().trimmed() : QString{};

    source.groupName = groupEdit_ ? groupEdit_->text().trimmed() : QString{};
    source.sourceUrl = sourceUrlEdit_ ? sourceUrlEdit_->text().trimmed() : QString{};
    source.preferLowLatency = lowLatencyCheck_ && lowLatencyCheck_->isChecked();
    source.enableDetector = detectorCheck_ && detectorCheck_->isChecked();
    source.enableAsr = asrCheck_ && asrCheck_->isChecked();
    source.enableRecording = recordingCheck_ && recordingCheck_->isChecked();
    return source;
}

void MonitorWallWindow::WriteSourceDescriptor(const MonitorSourceDescriptor& source)
{
    if (cameraIdEdit_)
        cameraIdEdit_->setText(source.cameraId);
    if (displayNameEdit_)
        displayNameEdit_->setText(source.displayName);

    if (groupEdit_)
        groupEdit_->setText(source.groupName);
    if (sourceUrlEdit_)
        sourceUrlEdit_->setText(source.sourceUrl);
    if (lowLatencyCheck_)
        lowLatencyCheck_->setChecked(source.preferLowLatency);
    if (detectorCheck_)
        detectorCheck_->setChecked(source.enableDetector);
    if (asrCheck_)
        asrCheck_->setChecked(source.enableAsr);
    if (recordingCheck_)
        recordingCheck_->setChecked(source.enableRecording);
}

QString MonitorWallWindow::ArchiveRootDir() const
{
    return archiveRootEdit_ ? archiveRootEdit_->text().trimmed() : QString{};
}

void MonitorWallWindow::SetArchiveRootDir(const QString& archiveRootDir)
{
    if (archiveRootEdit_)
        archiveRootEdit_->setText(archiveRootDir);
}

QString MonitorWallWindow::RecordingContainer() const
{
    return recordingFormatCombo_
        ? ArchivePathPolicy::NormalizeRecordingContainer(recordingFormatCombo_->currentData().toString())
        : QStringLiteral("mkv");
}

void MonitorWallWindow::SetRecordingContainer(const QString& container)
{
    if (!recordingFormatCombo_)
        return;

    const QString normalizedContainer = ArchivePathPolicy::NormalizeRecordingContainer(container);
    const QSignalBlocker blocker(recordingFormatCombo_);
    int index = recordingFormatCombo_->findData(normalizedContainer);
    if (index < 0)
        index = 0;
    recordingFormatCombo_->setCurrentIndex(index);
}

int MonitorWallWindow::RecordingSegmentDurationSeconds() const
{
    return segmentDurationSpin_ ? segmentDurationSpin_->value() : 60;
}

void MonitorWallWindow::SetRecordingSegmentDurationSeconds(int seconds)
{
    if (!segmentDurationSpin_)
        return;

    const QSignalBlocker blocker(segmentDurationSpin_);
    segmentDurationSpin_->setValue(std::max(10, seconds));
}

MonitorWallLayoutPreset MonitorWallWindow::CurrentLayoutPreset() const
{
    return PresetFromIndex(layoutCombo_ ? layoutCombo_->currentIndex() : 4);
}

QString MonitorWallWindow::CurrentWorkspaceId() const
{

    return workspaceCombo_ ? workspaceCombo_->currentData().toString() : QString{};
}

QString MonitorWallWindow::CurrentScreenName() const
{

    return screenCombo_ ? screenCombo_->currentData().toString() : QString{};
}

QString MonitorWallWindow::CurrentGroupFilter() const
{

    return groupFilterCombo_ ? groupFilterCombo_->currentData().toString() : QString{};
}

QString MonitorWallWindow::CurrentFavoriteLayoutId() const
{

    return favoriteLayoutCombo_ ? favoriteLayoutCombo_->currentData().toString() : QString{};
}

QString MonitorWallWindow::SelectedEventId() const
{
    if (!eventList_ || !eventList_->currentItem())
        return {};
    return eventList_->currentItem()->data(Qt::UserRole).toString();
}

QString MonitorWallWindow::SelectedCameraId() const
{
    if (!cameraList_ || !cameraList_->currentItem())
        return {};
    return cameraList_->currentItem()->data(Qt::UserRole).toString();
}

void MonitorWallWindow::SetSelectedPlaybackDate(const QDate& date)
{
    if (!playbackCalendar_ || !date.isValid())
        return;

    const QSignalBlocker blocker(playbackCalendar_);
    playbackCalendar_->setSelectedDate(date);
    playbackCalendar_->setCurrentPage(date.year(), date.month());
    UpdatePlaybackCalendarToggleButton();
}

void MonitorWallWindow::SetPlaybackCalendarHighlights(
    const QList<ArchiveDaySummary>& summaries,
    const QDate& month)
{
    if (!playbackCalendar_ || !month.isValid())
        return;

    const QTextCharFormat defaultFormat;
    for (const QDate& date : highlightedPlaybackDates_)
        playbackCalendar_->setDateTextFormat(date, defaultFormat);
    highlightedPlaybackDates_.clear();

    for (const ArchiveDaySummary& summary : summaries)
    {
        if (!summary.day.isValid()
            || summary.day.year() != month.year()
            || summary.day.month() != month.month())
        {
            continue;
        }

        QTextCharFormat format;
        format.setForeground(QColor("#F5F5F7"));
        format.setFontWeight(QFont::DemiBold);
        format.setToolTip(QString("Segments: %1").arg(summary.segmentCount));
        if (summary.hasAlarm)
            format.setBackground(QColor(176, 48, 48, 220));
        else if (summary.hasMedia)
            format.setBackground(QColor(56, 118, 212, 210));
        playbackCalendar_->setDateTextFormat(summary.day, format);
        highlightedPlaybackDates_.append(summary.day);
    }
    playbackCalendar_->update();
    UpdatePlaybackCalendarToggleButton();
}

void MonitorWallWindow::SetActiveWallSlot(int slotIndex)
{
    activeWallSlotIndex_ = slotIndex;
    for (QWidget* placeholder : emptySlotWidgets_)
    {
        auto* button = qobject_cast<QPushButton*>(placeholder);
        if (!button)
            continue;
        const int placeholderSlotIndex = button->property("slotIndex").toInt();
        button->setStyleSheet(EmptySlotStyle(placeholderSlotIndex == activeWallSlotIndex_));
    }
}

void MonitorWallWindow::SetStatusText(const QString& text)
{
    statusOverrideText_ = text.trimmed();
    statusOverrideUntilMs_ = QDateTime::currentMSecsSinceEpoch() + 6000;
    UpdateStatusLabel();
}

void MonitorWallWindow::SetStatusSummaryText(const QString& text)
{
    statusSummaryText_ = text.trimmed();
    UpdateStatusLabel();
}

void MonitorWallWindow::InvalidateWallLayout()
{
    lastWallLayoutSignature_.clear();
}

void MonitorWallWindow::SetWindowFullscreen(bool fullscreen)
{
    if (fullscreenButton_)
        fullscreenButton_->setText(fullscreen ? "Exit Full" : "Full Screen");
}

void MonitorWallWindow::SetPresentationMode(bool enabled)
{
    if (mainSplitter_ && leftPanel_)
    {
        if (enabled)
        {
            const QList<int> sizes = mainSplitter_->sizes();
            if (!sizes.isEmpty() && sizes.first() > 0)
                leftPanelExpandedWidth_ = sizes.first();
            leftPanel_->setVisible(false);
            mainSplitter_->setSizes({ 0, std::max(1, width()) });
        }
        else
        {
            leftPanel_->setVisible(true);
            const int restoredWidth = std::max(leftPanel_->minimumWidth(), leftPanelExpandedWidth_);
            mainSplitter_->setSizes({ restoredWidth, std::max(1, width() - restoredWidth) });
        }
    }
    else if (leftPanel_)
    {
        leftPanel_->setVisible(!enabled);
    }
    if (toolbarRow_)
        toolbarRow_->setVisible(!enabled);
    if (statusLabel_)
        statusLabel_->setVisible(!enabled);
    if (titleLabel_)
        titleLabel_->setVisible(!enabled);
}

void MonitorWallWindow::SetWorkspaces(
    const QList<QPair<QString, QString>>& workspaces,
    const QString& currentWorkspaceId)
{

    if (!workspaceCombo_)
        return;

    const QSignalBlocker blocker(workspaceCombo_);
    workspaceCombo_->clear();
    for (const auto& workspace : workspaces)
        workspaceCombo_->addItem(workspace.second, workspace.first);

    int currentIndex = workspaceCombo_->findData(currentWorkspaceId);
    if (currentIndex < 0 && workspaceCombo_->count() > 0)
        currentIndex = 0;
    workspaceCombo_->setCurrentIndex(currentIndex);
}

void MonitorWallWindow::SetAvailableScreens(
    const QList<QPair<QString, QString>>& screens,
    const QString& currentScreenName)
{

    if (!screenCombo_)
        return;

    const QSignalBlocker blocker(screenCombo_);
    screenCombo_->clear();
    for (const auto& screen : screens)
        screenCombo_->addItem(screen.second, screen.first);

    int currentIndex = screenCombo_->findData(currentScreenName);
    if (currentIndex < 0 && screenCombo_->count() > 0)
        currentIndex = 0;
    screenCombo_->setCurrentIndex(currentIndex);
}

void MonitorWallWindow::SetGroupFilters(
    const QList<QPair<QString, QString>>& groups,
    const QString& currentGroupFilter)
{

    if (!groupFilterCombo_)
        return;

    const QSignalBlocker blocker(groupFilterCombo_);
    groupFilterCombo_->clear();
    for (const auto& group : groups)
        groupFilterCombo_->addItem(group.second, group.first);

    int currentIndex = groupFilterCombo_->findData(currentGroupFilter);
    if (currentIndex < 0 && groupFilterCombo_->count() > 0)
        currentIndex = 0;
    groupFilterCombo_->setCurrentIndex(currentIndex);
}

void MonitorWallWindow::SetFavoriteLayouts(
    const QList<QPair<QString, QString>>& layouts,
    const QString& currentLayoutId)
{

    if (!favoriteLayoutCombo_)
        return;

    const QSignalBlocker blocker(favoriteLayoutCombo_);
    favoriteLayoutCombo_->clear();
    favoriteLayoutCombo_->addItem("Current", QString{});
    for (const auto& layout : layouts)
        favoriteLayoutCombo_->addItem(layout.second, layout.first);

    int currentIndex = favoriteLayoutCombo_->findData(currentLayoutId);
    if (currentIndex < 0)
        currentIndex = 0;
    favoriteLayoutCombo_->setCurrentIndex(currentIndex);
}

void MonitorWallWindow::SetEvents(const QList<MonitorEventEntry>& entries)
{
    if (!eventList_)
        return;

    const QString selectedEventId = SelectedEventId();
    eventList_->clear();
    for (const MonitorEventEntry& entry : entries)
    {
        QString prefix = entry.cleared ? "[CLEARED]" : (entry.acknowledged ? "[ACK]" : "[OPEN]");
        QString severity = "INFO";
        if (entry.severity == ArchiveEventSeverity::Warning)
            severity = "WARN";
        else if (entry.severity == ArchiveEventSeverity::Alarm)
            severity = "ALARM";
        const QString whenText = entry.lastOccurredAtUtc.isValid()
            ? entry.lastOccurredAtUtc.toLocalTime().toString("MM-dd HH:mm:ss")
            : "-";
        auto* item = new QListWidgetItem(
            QString("%1 %2  %3\n%4  x%5")
                .arg(prefix, severity, whenText, entry.title)
                .arg(entry.occurrenceCount),
            eventList_);
        item->setData(Qt::UserRole, entry.eventId);
        if (!selectedEventId.isEmpty() && selectedEventId == entry.eventId)
            eventList_->setCurrentItem(item);
    }
}

void MonitorWallWindow::SetCameraSessions(
    const QVector<MonitorSessionSnapshot>& sessions,
    const QString& selectedSessionId)
{
    if (!cameraList_)
        return;

    const QSignalBlocker blocker(cameraList_);
    if (CameraListMatches(cameraList_, sessions))
    {
        for (int index = 0; index < sessions.size(); ++index)
        {
            if (QListWidgetItem* item = cameraList_->item(index))
                item->setText(CameraListText(sessions.at(index)));
        }
    }
    else
    {
        cameraList_->clear();
        for (const MonitorSessionSnapshot& session : sessions)
        {
            auto* item = new QListWidgetItem(CameraListText(session), cameraList_);
            item->setData(Qt::UserRole, session.sessionId);
        }
    }

    QListWidgetItem* selectedItem = nullptr;
    for (int index = 0; index < cameraList_->count(); ++index)
    {
        QListWidgetItem* item = cameraList_->item(index);
        if (item && item->data(Qt::UserRole).toString() == selectedSessionId)
        {
            selectedItem = item;
            break;
        }
    }

    if (selectedItem)
        cameraList_->setCurrentItem(selectedItem);
    else if (cameraList_->currentItem())
        cameraList_->setCurrentItem(nullptr);
}

void MonitorWallWindow::SetPlaybackFiles(
    const QString& cameraTitle,
    const QString& metaText,
    const QList<ArchiveSegmentRecord>& segments)
{
    (void)cameraTitle;
    if (!playbackList_)
        return;

    playbackList_->clear();
    if (segments.isEmpty())
    {
        const QString emptyText = metaText.trimmed().isEmpty()
            ? QStringLiteral("No recordings.")
            : metaText.trimmed();
        auto* item = new QListWidgetItem(emptyText, playbackList_);
        item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
        item->setForeground(QColor("#AFC1D1"));
        return;
    }

    for (const ArchiveSegmentRecord& segment : segments)
    {
        auto* item = new QListWidgetItem(PlaybackItemText(segment), playbackList_);
        item->setData(Qt::UserRole, segment.relativePath);
        item->setToolTip(segment.relativePath);
    }
}

void MonitorWallWindow::SetPlaybackCalendarVisible(bool visible)
{
    if (playbackCalendar_)
        playbackCalendar_->setVisible(visible);
    UpdatePlaybackCalendarToggleButton();
}

void MonitorWallWindow::UpdatePlaybackCalendarToggleButton()
{
    if (!playbackCalendarToggleButton_)
        return;

    const QDate selectedDate = playbackCalendar_ ? playbackCalendar_->selectedDate() : QDate{};
    const QString dateText = selectedDate.isValid()
        ? selectedDate.toString("yyyy-MM-dd")
        : QStringLiteral("Pick Date");
    const bool visible = playbackCalendar_ && playbackCalendar_->isVisible();
    playbackCalendarToggleButton_->setText(visible
        ? QString("Hide Calendar")
        : QString("Date %1").arg(dateText));
}

void MonitorWallWindow::UpdateStatusLabel()
{
    if (!statusLabel_)
        return;

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (!statusOverrideText_.isEmpty() && statusOverrideUntilMs_ > nowMs)
    {
        statusLabel_->setText(statusOverrideText_);
        return;
    }

    if (statusOverrideUntilMs_ <= nowMs)
    {
        statusOverrideText_.clear();
        statusOverrideUntilMs_ = 0;
    }

    statusLabel_->setText(statusSummaryText_.isEmpty() ? QStringLiteral("Idle") : statusSummaryText_);
}

void MonitorWallWindow::SetRecordAllState(bool enabled, int activeWallCount)
{
    if (!recordAllButton_)
        return;

    const QSignalBlocker blocker(recordAllButton_);
    recordAllButton_->setEnabled(activeWallCount > 0);
    recordAllButton_->setChecked(activeWallCount > 0 && enabled);
    if (activeWallCount <= 0)
        recordAllButton_->setText(QStringLiteral("REC All"));
    else
        recordAllButton_->setText(enabled
            ? QString("Stop REC All (%1)").arg(activeWallCount)
            : QString("REC All (%1)").arg(activeWallCount));
}
