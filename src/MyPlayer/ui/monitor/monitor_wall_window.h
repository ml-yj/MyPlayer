#pragma once

#include "../../core/monitor/monitor_event_center.h"
#include "../../core/monitor/monitor_types.h"
#include "../../core/archive/archive_models.h"

#include <QDate>
#include <QMap>
#include <QList>
#include <QWidget>

class QCalendarWidget;
class QCheckBox;
class QComboBox;
class QGridLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QScrollArea;
class QSplitter;
class QSpinBox;
class MonitorTileWidget;

class MonitorWallWindow final : public QWidget
{
    Q_OBJECT

public:
    explicit MonitorWallWindow(QWidget* parent = nullptr);

    MonitorSourceDescriptor ReadSourceDescriptor() const;
    void WriteSourceDescriptor(const MonitorSourceDescriptor& source);

    QString ArchiveRootDir() const;
    void SetArchiveRootDir(const QString& archiveRootDir);
    QString RecordingContainer() const;
    void SetRecordingContainer(const QString& container);
    int RecordingSegmentDurationSeconds() const;
    void SetRecordingSegmentDurationSeconds(int seconds);
    MonitorWallLayoutPreset CurrentLayoutPreset() const;
    QString CurrentWorkspaceId() const;
    QString CurrentScreenName() const;
    QString CurrentGroupFilter() const;
    QString CurrentFavoriteLayoutId() const;
    QString SelectedEventId() const;
    QString SelectedCameraId() const;
    void SetSelectedPlaybackDate(const QDate& date);
    void SetPlaybackCalendarHighlights(const QList<ArchiveDaySummary>& summaries, const QDate& month);
    void SetActiveWallSlot(int slotIndex);
    void SetStatusText(const QString& text);
    void SetStatusSummaryText(const QString& text);
    void InvalidateWallLayout();
    void SetWindowFullscreen(bool fullscreen);
    void SetPresentationMode(bool enabled);

    void SetWorkspaces(const QList<QPair<QString, QString>>& workspaces, const QString& currentWorkspaceId);
    void SetAvailableScreens(const QList<QPair<QString, QString>>& screens, const QString& currentScreenName);
    void SetGroupFilters(const QList<QPair<QString, QString>>& groups, const QString& currentGroupFilter);
    void SetFavoriteLayouts(const QList<QPair<QString, QString>>& layouts, const QString& currentLayoutId);
    void SetEvents(const QList<MonitorEventEntry>& entries);
    void SetCameraSessions(const QVector<MonitorSessionSnapshot>& sessions, const QString& selectedSessionId);
    void SetPlaybackFiles(
        const QString& cameraTitle,
        const QString& metaText,
        const QList<ArchiveSegmentRecord>& segments);
    void SetRecordAllState(bool enabled, int activeWallCount);
    void ApplyWorkspaceSnapshot(
        const MonitorWorkspaceSnapshot& snapshot,
        const QMap<QString, MonitorTileWidget*>& tiles);

signals:
    void WindowVisibilityChanged(bool visible);
    void AddOrUpdateRequested();
    void OpenSelectedRequested();
    void OpenAllRequested();
    void CloseAllRequested();
    void RemoveSelectedRequested();
    void ArchiveRootChanged(const QString& archiveRootDir);
    void BrowseArchiveRootRequested();
    void BrowseSourceFileRequested();
    void ToggleFullscreenRequested();
    void RecordingContainerChanged(const QString& container);
    void RecordingSegmentDurationChanged(int seconds);
    void LayoutPresetChanged(MonitorWallLayoutPreset preset);
    void WorkspaceChanged(const QString& workspaceId);
    void PreviousWorkspaceRequested();
    void NextWorkspaceRequested();
    void SaveWorkspaceRequested();
    void NewWorkspaceRequested();
    void DeleteWorkspaceRequested();
    void ScreenAssignmentChanged(const QString& screenName);
    void GroupFilterChanged(const QString& groupName);
    void OpenGroupRequested();
    void CloseGroupRequested();
    void FavoriteLayoutChanged(const QString& layoutId);
    void SaveFavoriteLayoutRequested();
    void DeleteFavoriteLayoutRequested();
    void AcknowledgeSelectedEventRequested();
    void ClearSelectedEventRequested();
    void JumpToSelectedEventRequested();
    void EventActivated(const QString& eventId);
    void TileReorderRequested(const QString& draggedSessionId, const QString& targetSessionId);
    void CameraSelectionChanged(const QString& sessionId);
    void CameraActivated(const QString& sessionId);
    void PlaybackDayChanged(const QDate& day);
    void PlaybackMonthChanged(const QDate& month);
    void PlaybackSegmentRequested(const QString& relativePath);
    void ToggleRecordAllRequested(bool enabled);
    void WallSlotActivated(int slotIndex);

protected:
    void closeEvent(QCloseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    static void ClearGridLayout(QGridLayout* layout);
    MonitorTileWidget* ResolveTileAt(const QPoint& pos) const;
    QWidget* CreateEmptySlotWidget(int slotIndex);
    void SetPlaybackCalendarVisible(bool visible);
    void UpdatePlaybackCalendarToggleButton();
    void UpdateStatusLabel();

    QLabel* titleLabel_ = nullptr;
    QSplitter* mainSplitter_ = nullptr;
    QWidget* leftPanel_ = nullptr;
    QWidget* rightPanel_ = nullptr;
    QWidget* toolbarRow_ = nullptr;
    QWidget* formWidget_ = nullptr;
    QWidget* eventPanel_ = nullptr;
    QLineEdit* cameraIdEdit_ = nullptr;
    QLineEdit* displayNameEdit_ = nullptr;

    QLineEdit* groupEdit_ = nullptr;
    QLineEdit* sourceUrlEdit_ = nullptr;
    QPushButton* browseSourceButton_ = nullptr;
    QCheckBox* lowLatencyCheck_ = nullptr;
    QCheckBox* detectorCheck_ = nullptr;
    QCheckBox* asrCheck_ = nullptr;
    QCheckBox* recordingCheck_ = nullptr;
    QLineEdit* archiveRootEdit_ = nullptr;
    QPushButton* browseArchiveButton_ = nullptr;
    QComboBox* recordingFormatCombo_ = nullptr;
    QSpinBox* segmentDurationSpin_ = nullptr;

    QPushButton* workspacePrevButton_ = nullptr;
    QComboBox* workspaceCombo_ = nullptr;
    QPushButton* workspaceNextButton_ = nullptr;
    QPushButton* saveWorkspaceButton_ = nullptr;
    QPushButton* newWorkspaceButton_ = nullptr;
    QPushButton* deleteWorkspaceButton_ = nullptr;
    QComboBox* screenCombo_ = nullptr;
    QComboBox* groupFilterCombo_ = nullptr;
    QPushButton* openGroupButton_ = nullptr;
    QPushButton* closeGroupButton_ = nullptr;
    QComboBox* favoriteLayoutCombo_ = nullptr;
    QPushButton* saveFavoriteButton_ = nullptr;
    QPushButton* deleteFavoriteButton_ = nullptr;
    QComboBox* layoutCombo_ = nullptr;
    QPushButton* addOrUpdateButton_ = nullptr;
    QPushButton* openSelectedButton_ = nullptr;
    QPushButton* openAllButton_ = nullptr;
    QPushButton* closeAllButton_ = nullptr;
    QPushButton* removeSelectedButton_ = nullptr;
    QPushButton* fullscreenButton_ = nullptr;
    QPushButton* acknowledgeEventButton_ = nullptr;
    QPushButton* clearEventButton_ = nullptr;
    QPushButton* jumpEventButton_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QListWidget* cameraList_ = nullptr;
    QLabel* playbackTitleLabel_ = nullptr;
    QLabel* playbackMetaLabel_ = nullptr;
    QPushButton* playbackCalendarToggleButton_ = nullptr;
    QCalendarWidget* playbackCalendar_ = nullptr;
    QListWidget* playbackList_ = nullptr;
    QPushButton* recordAllButton_ = nullptr;
    QScrollArea* scrollArea_ = nullptr;
    QWidget* gridHost_ = nullptr;
    QGridLayout* gridLayout_ = nullptr;
    QListWidget* eventList_ = nullptr;
    QList<QWidget*> emptySlotWidgets_;
    QList<QDate> highlightedPlaybackDates_;
    int activeWallSlotIndex_ = -1;
    QString lastWallLayoutSignature_;
    QString statusSummaryText_;
    QString statusOverrideText_;
    qint64 statusOverrideUntilMs_ = 0;
    int leftPanelExpandedWidth_ = 380;
};
