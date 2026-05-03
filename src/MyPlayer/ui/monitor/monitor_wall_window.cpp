

#include "monitor_wall_window.h"

#include "../../core/archive/archive_path_policy.h"
#include "monitor_tile_widget.h"

#include <QCalendarWidget>
#include <QCheckBox>
#include <QColor>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFormLayout>
#include <QFrame>
#include <QFont>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMimeData>
#include <QPushButton>
#include <QScrollArea>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QShowEvent>
#include <QSpinBox>
#include <QSplitter>
#include <QTextCharFormat>
#include <QVBoxLayout>

#include <algorithm>

namespace
{

QString PresetLabel(MonitorWallLayoutPreset preset)
{
    switch (preset)
    {
    case MonitorWallLayoutPreset::Single:  return "1";
    case MonitorWallLayoutPreset::Grid2x2: return "2x2";
    case MonitorWallLayoutPreset::Grid3x3: return "3x3";
    case MonitorWallLayoutPreset::Grid4x4: return "4x4";
    case MonitorWallLayoutPreset::Custom:  return "Auto";
    }
    return "Auto";
}

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

MonitorWallWindow::MonitorWallWindow(QWidget* parent)
    : QWidget(parent)
{
    setWindowTitle("Monitor Wall");
    resize(1440, 900);
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(
        "MonitorWallWindow, QWidget { background: #11161D; color: #F5F5F7; }"
        "QFrame#Card { background: rgba(255,255,255,10); border: 1px solid rgba(255,255,255,18); border-radius: 10px; }"
        "QLineEdit, QComboBox, QListWidget { background: rgba(255,255,255,18); border: 1px solid rgba(255,255,255,22); "
        "border-radius: 6px; padding: 4px 8px; color: #F5F5F7; }"
        "QPushButton { background: rgba(255,255,255,18); border: 1px solid rgba(255,255,255,22); "
        "border-radius: 6px; padding: 5px 10px; color: #F5F5F7; }"
        "QPushButton:hover { background: rgba(255,255,255,28); }"
        "QPushButton:checked { background: rgba(28, 112, 214, 200); border-color: rgba(120,180,255,120); }"
        "QCheckBox { color: #D7E3F0; }"
        "QListWidget::item { padding: 8px 6px; border-bottom: 1px solid rgba(255,255,255,10); }"
        "QListWidget::item:selected { background: rgba(10,132,255,110); }");

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(12, 12, 12, 12);
    rootLayout->setSpacing(10);

    titleLabel_ = new QLabel("Monitor Wall", this);
    titleLabel_->setStyleSheet("font-size: 18px; font-weight: 700;");
    rootLayout->addWidget(titleLabel_);

    mainSplitter_ = new QSplitter(Qt::Horizontal, this);
    mainSplitter_->setObjectName("monitorMainSplitter");
    mainSplitter_->setHandleWidth(10);
    mainSplitter_->setOpaqueResize(false);
    mainSplitter_->setStyleSheet(
        "QSplitter::handle:horizontal {"
        " background: rgba(255,255,255,12);"
        " border-left: 1px solid rgba(255,255,255,20);"
        " border-right: 1px solid rgba(255,255,255,20);"
        " }"
        "QSplitter::handle:horizontal:hover {"
        " background: rgba(10,132,255,70);"
        " }");

    leftPanel_ = new QWidget(mainSplitter_);
    leftPanel_->setMinimumWidth(260);
    leftPanel_->setMaximumWidth(720);
    leftPanel_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    auto* leftLayout = new QVBoxLayout(leftPanel_);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(10);

    formWidget_ = new QFrame(leftPanel_);
    formWidget_->setObjectName("Card");
    auto* formLayout = new QFormLayout(formWidget_);
    formLayout->setContentsMargins(12, 12, 12, 12);
    formLayout->setSpacing(8);
    cameraIdEdit_ = new QLineEdit(formWidget_);
    cameraIdEdit_->setPlaceholderText("camera-1");
    displayNameEdit_ = new QLineEdit(formWidget_);
    displayNameEdit_->setPlaceholderText("Display name");
    auto* sourceRow = new QWidget(formWidget_);
    auto* sourceLayout = new QHBoxLayout(sourceRow);
    sourceLayout->setContentsMargins(0, 0, 0, 0);
    sourceLayout->setSpacing(6);
    sourceUrlEdit_ = new QLineEdit(sourceRow);
    sourceUrlEdit_->setPlaceholderText("rtsp://... or local file");
    browseSourceButton_ = new QPushButton("...", sourceRow);
    browseSourceButton_->setFixedWidth(36);
    sourceLayout->addWidget(sourceUrlEdit_, 1);
    sourceLayout->addWidget(browseSourceButton_);
    lowLatencyCheck_ = new QCheckBox("Low latency", formWidget_);
    lowLatencyCheck_->setChecked(true);
    detectorCheck_ = new QCheckBox("Detector", formWidget_);
    asrCheck_ = new QCheckBox("ASR", formWidget_);
    recordingCheck_ = new QCheckBox("Recording", formWidget_);
    detectorCheck_->hide();
    asrCheck_->hide();
    recordingCheck_->hide();
    auto* optionRow = new QWidget(formWidget_);
    auto* optionLayout = new QHBoxLayout(optionRow);
    optionLayout->setContentsMargins(0, 0, 0, 0);
    optionLayout->setSpacing(10);
    optionLayout->addWidget(lowLatencyCheck_);
    optionLayout->addStretch(1);
    auto* commandRow = new QWidget(formWidget_);
    auto* commandLayout = new QHBoxLayout(commandRow);
    commandLayout->setContentsMargins(0, 0, 0, 0);
    commandLayout->setSpacing(8);
    addOrUpdateButton_ = new QPushButton("Save Camera", commandRow);
    removeSelectedButton_ = new QPushButton("Delete Camera", commandRow);
    commandLayout->addWidget(addOrUpdateButton_);
    commandLayout->addWidget(removeSelectedButton_);
    formLayout->addRow("Camera ID", cameraIdEdit_);
    formLayout->addRow("Display", displayNameEdit_);
    formLayout->addRow("Source", sourceRow);
    formLayout->addRow("Options", optionRow);
    formLayout->addRow(QString(), commandRow);
    leftLayout->addWidget(formWidget_);

    auto* listCard = new QFrame(leftPanel_);
    listCard->setObjectName("Card");
    auto* listLayout = new QVBoxLayout(listCard);
    listLayout->setContentsMargins(12, 12, 12, 12);
    listLayout->setSpacing(8);
    auto* listTitle = new QLabel("Camera List", listCard);
    listTitle->setStyleSheet("font-size: 14px; font-weight: 600;");
    cameraList_ = new QListWidget(listCard);
    cameraList_->setSelectionMode(QAbstractItemView::SingleSelection);
    listLayout->addWidget(listTitle);
    listLayout->addWidget(cameraList_, 1);
    leftLayout->addWidget(listCard, 1);

    auto* playbackCard = new QFrame(leftPanel_);
    playbackCard->setObjectName("Card");
    auto* playbackLayout = new QVBoxLayout(playbackCard);
    playbackLayout->setContentsMargins(12, 12, 12, 12);
    playbackLayout->setSpacing(8);
    auto* playbackHeaderRow = new QHBoxLayout();
    playbackHeaderRow->setContentsMargins(0, 0, 0, 0);
    playbackHeaderRow->setSpacing(8);
    auto* playbackLabel = new QLabel("Playback Files", playbackCard);
    playbackLabel->setStyleSheet("font-size: 14px; font-weight: 600;");
    playbackCalendarToggleButton_ = new QPushButton("Date", playbackCard);
    playbackHeaderRow->addWidget(playbackLabel);
    playbackHeaderRow->addStretch(1);
    playbackHeaderRow->addWidget(playbackCalendarToggleButton_);
    playbackTitleLabel_ = new QLabel(playbackCard);
    playbackMetaLabel_ = new QLabel(playbackCard);
    playbackTitleLabel_->hide();
    playbackMetaLabel_->hide();
    playbackCalendar_ = new QCalendarWidget(playbackCard);
    playbackCalendar_->setGridVisible(true);
    playbackCalendar_->setVerticalHeaderFormat(QCalendarWidget::NoVerticalHeader);
    playbackCalendar_->setStyleSheet(
        "font-size: 12px;"
        "alternate-background-color: rgba(255,255,255,10);");
    playbackCalendar_->hide();
    auto* archiveRootRow = new QWidget(playbackCard);
    auto* archiveRootLayout = new QHBoxLayout(archiveRootRow);
    archiveRootLayout->setContentsMargins(0, 0, 0, 0);
    archiveRootLayout->setSpacing(6);
    archiveRootEdit_ = new QLineEdit(archiveRootRow);
    archiveRootEdit_->setPlaceholderText("Archive root");
    browseArchiveButton_ = new QPushButton("...", archiveRootRow);
    browseArchiveButton_->setFixedWidth(36);
    archiveRootLayout->addWidget(archiveRootEdit_, 1);
    archiveRootLayout->addWidget(browseArchiveButton_);
    auto* recordingOptionsRow = new QWidget(playbackCard);
    auto* recordingOptionsLayout = new QHBoxLayout(recordingOptionsRow);
    recordingOptionsLayout->setContentsMargins(0, 0, 0, 0);
    recordingOptionsLayout->setSpacing(8);
    recordingFormatCombo_ = new QComboBox(recordingOptionsRow);
    recordingFormatCombo_->addItem("MKV", "mkv");
    recordingFormatCombo_->addItem("fMP4", "mp4");
    segmentDurationSpin_ = new QSpinBox(recordingOptionsRow);
    segmentDurationSpin_->setRange(10, 3600);
    segmentDurationSpin_->setSuffix(" s");
    recordingOptionsLayout->addWidget(new QLabel("Format", recordingOptionsRow));
    recordingOptionsLayout->addWidget(recordingFormatCombo_);
    recordingOptionsLayout->addSpacing(8);
    recordingOptionsLayout->addWidget(new QLabel("Segment Duration", recordingOptionsRow));
    recordingOptionsLayout->addWidget(segmentDurationSpin_);
    recordingOptionsLayout->addStretch(1);
    playbackList_ = new QListWidget(playbackCard);
    playbackList_->setSelectionMode(QAbstractItemView::SingleSelection);
    playbackList_->setStyleSheet("font-family: 'Consolas','Courier New',monospace; font-size: 12px;");
    playbackLayout->addLayout(playbackHeaderRow);
    playbackLayout->addWidget(archiveRootRow);
    playbackLayout->addWidget(recordingOptionsRow);
    playbackLayout->addWidget(playbackCalendar_);
    playbackLayout->addWidget(playbackList_, 1);
    leftLayout->addWidget(playbackCard, 1);

    mainSplitter_->addWidget(leftPanel_);

    rightPanel_ = new QWidget(mainSplitter_);
    rightPanel_->setMinimumWidth(320);
    rightPanel_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto* rightLayout = new QVBoxLayout(rightPanel_);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(10);

    toolbarRow_ = new QFrame(rightPanel_);
    toolbarRow_->setObjectName("Card");
    auto* toolbarLayout = new QHBoxLayout(toolbarRow_);
    toolbarLayout->setContentsMargins(12, 10, 12, 10);
    toolbarLayout->setSpacing(8);
    layoutCombo_ = new QComboBox(toolbarRow_);
    for (MonitorWallLayoutPreset preset : {
            MonitorWallLayoutPreset::Single,
            MonitorWallLayoutPreset::Grid2x2,
            MonitorWallLayoutPreset::Grid3x3,
            MonitorWallLayoutPreset::Grid4x4,
            MonitorWallLayoutPreset::Custom })
    {
        layoutCombo_->addItem(PresetLabel(preset));
    }
    openSelectedButton_ = new QPushButton("Open Selected", toolbarRow_);
    openAllButton_ = new QPushButton("Open All", toolbarRow_);
    closeAllButton_ = new QPushButton("Close All", toolbarRow_);
    recordAllButton_ = new QPushButton("REC All", toolbarRow_);
    recordAllButton_->setCheckable(true);
    fullscreenButton_ = new QPushButton("Full Screen", toolbarRow_);
    toolbarLayout->addWidget(new QLabel("Layout", toolbarRow_));
    toolbarLayout->addWidget(layoutCombo_);
    toolbarLayout->addSpacing(8);
    toolbarLayout->addWidget(openSelectedButton_);
    toolbarLayout->addWidget(openAllButton_);
    toolbarLayout->addWidget(closeAllButton_);
    toolbarLayout->addWidget(recordAllButton_);
    toolbarLayout->addStretch(1);
    toolbarLayout->addWidget(fullscreenButton_);
    rightLayout->addWidget(toolbarRow_);

    scrollArea_ = new QScrollArea(rightPanel_);
    scrollArea_->setWidgetResizable(true);
    scrollArea_->setFrameShape(QFrame::NoFrame);
    scrollArea_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    scrollArea_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    gridHost_ = new QWidget(scrollArea_);
    gridHost_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    gridHost_->setAcceptDrops(true);
    gridHost_->installEventFilter(this);
    gridLayout_ = new QGridLayout(gridHost_);
    gridLayout_->setContentsMargins(0, 0, 0, 0);
    gridLayout_->setSpacing(10);
    scrollArea_->setWidget(gridHost_);
    rightLayout->addWidget(scrollArea_, 1);

    statusLabel_ = new QLabel("Idle", rightPanel_);
    statusLabel_->setStyleSheet("color: #AFC1D1; font-size: 12px;");
    rightLayout->addWidget(statusLabel_);
    mainSplitter_->addWidget(rightPanel_);
    mainSplitter_->setStretchFactor(0, 0);
    mainSplitter_->setStretchFactor(1, 1);
    mainSplitter_->setCollapsible(0, true);
    mainSplitter_->setCollapsible(1, false);
    mainSplitter_->setSizes({ leftPanelExpandedWidth_, std::max(900, width() - leftPanelExpandedWidth_) });
    if (QWidget* splitterHandle = mainSplitter_->handle(1))
        splitterHandle->setCursor(Qt::SplitHCursor);
    connect(mainSplitter_, &QSplitter::splitterMoved, this, [this](int, int) {
        if (!mainSplitter_)
            return;
        const QList<int> sizes = mainSplitter_->sizes();
        if (!sizes.isEmpty() && sizes.first() > 0)
            leftPanelExpandedWidth_ = sizes.first();
    });
    rootLayout->addWidget(mainSplitter_, 1);

    groupEdit_ = new QLineEdit(this);
    groupEdit_->hide();

    eventPanel_ = new QWidget(this);
    eventPanel_->hide();
    acknowledgeEventButton_ = new QPushButton(this);
    acknowledgeEventButton_->hide();
    clearEventButton_ = new QPushButton(this);
    clearEventButton_->hide();
    jumpEventButton_ = new QPushButton(this);
    jumpEventButton_->hide();
    eventList_ = new QListWidget(this);
    eventList_->hide();

    connect(addOrUpdateButton_, &QPushButton::clicked, this, &MonitorWallWindow::AddOrUpdateRequested);
    connect(removeSelectedButton_, &QPushButton::clicked, this, &MonitorWallWindow::RemoveSelectedRequested);
    connect(archiveRootEdit_, &QLineEdit::editingFinished, this, [this]() {
        emit ArchiveRootChanged(ArchiveRootDir());
    });
    connect(browseArchiveButton_, &QPushButton::clicked, this, &MonitorWallWindow::BrowseArchiveRootRequested);
    connect(browseSourceButton_, &QPushButton::clicked, this, &MonitorWallWindow::BrowseSourceFileRequested);
    connect(openSelectedButton_, &QPushButton::clicked, this, &MonitorWallWindow::OpenSelectedRequested);
    connect(openAllButton_, &QPushButton::clicked, this, &MonitorWallWindow::OpenAllRequested);
    connect(closeAllButton_, &QPushButton::clicked, this, &MonitorWallWindow::CloseAllRequested);
    connect(fullscreenButton_, &QPushButton::clicked, this, &MonitorWallWindow::ToggleFullscreenRequested);
    connect(recordingFormatCombo_, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index >= 0)
            emit RecordingContainerChanged(RecordingContainer());
    });
    connect(segmentDurationSpin_, &QSpinBox::valueChanged, this, [this](int seconds) {
        emit RecordingSegmentDurationChanged(seconds);
    });
    connect(layoutCombo_, &QComboBox::currentIndexChanged, this, [this](int index) {
        emit LayoutPresetChanged(PresetFromIndex(index));
    });
    connect(acknowledgeEventButton_, &QPushButton::clicked, this, &MonitorWallWindow::AcknowledgeSelectedEventRequested);
    connect(clearEventButton_, &QPushButton::clicked, this, &MonitorWallWindow::ClearSelectedEventRequested);
    connect(jumpEventButton_, &QPushButton::clicked, this, &MonitorWallWindow::JumpToSelectedEventRequested);

    connect(eventList_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        if (item)
            emit EventActivated(item->data(Qt::UserRole).toString());
    });
    connect(cameraList_, &QListWidget::currentItemChanged, this,
        [this](QListWidgetItem* current, QListWidgetItem*) {
            if (current)
                emit CameraSelectionChanged(current->data(Qt::UserRole).toString());
        });
    connect(cameraList_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        if (item)
            emit CameraActivated(item->data(Qt::UserRole).toString());
    });
    connect(recordAllButton_, &QPushButton::toggled, this, &MonitorWallWindow::ToggleRecordAllRequested);
    connect(playbackCalendarToggleButton_, &QPushButton::clicked, this, [this]() {
        SetPlaybackCalendarVisible(!playbackCalendar_ || !playbackCalendar_->isVisible());
    });
    connect(playbackCalendar_, &QCalendarWidget::selectionChanged, this, [this]() {
        if (playbackCalendar_)
        {
            emit PlaybackDayChanged(playbackCalendar_->selectedDate());
            SetPlaybackCalendarVisible(false);
        }
    });
    connect(playbackCalendar_, &QCalendarWidget::currentPageChanged, this, [this](int year, int month) {
        emit PlaybackMonthChanged(QDate(year, month, 1));
    });
    connect(playbackList_, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        if (item)
            emit PlaybackSegmentRequested(item->data(Qt::UserRole).toString());
    });

    UpdatePlaybackCalendarToggleButton();
}
