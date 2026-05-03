#pragma once

#include "../../core/monitor/monitor_types.h"
#include "../../features/subtitle/subtitle_types.h"

#include <QPoint>
#include <QWidget>

class QLabel;
class QCloseEvent;
class QContextMenuEvent;
class QMouseEvent;
class QPushButton;
class QVBoxLayout;
class VideoWidget;
struct DetectionResult;

class MonitorTileWidget final : public QWidget
{
    Q_OBJECT

public:
    enum RecordingMenuAction
    {
        RecordingMenuDisable = 0,
        RecordingMenuSingle = 1,
    };

    explicit MonitorTileWidget(const QString& sessionId, QWidget* parent = nullptr);

    QString SessionId() const;
    VideoWidget* VideoSurface() const;

    void SetSnapshot(const MonitorSessionSnapshot& snapshot);
    void ApplyDetections(const DetectionResult& result);
    void ApplyAsrSubtitle(
        const QString& text,
        long long startMs,
        long long endMs,
        quint64 generation,
        quint64 serial);
    void ClearAsrSubtitle();

signals:
    void Activated(const QString& sessionId);
    void MaximizeRequested(const QString& sessionId);
    void ToggleDetectorRequested(const QString& sessionId, bool enabled);
    void ToggleAsrRequested(const QString& sessionId, bool enabled);
    void ToggleRecordingRequested(const QString& sessionId, bool enabled);
    void RecordingMenuRequested(const QString& sessionId, int action);
    void ToggleMuteRequested(const QString& sessionId, bool muted);
    void ReopenRequested(const QString& sessionId);
    void SnapshotRequested(const QString& sessionId);
    void PopoutRequested(const QString& sessionId);
    void RemoveRequested(const QString& sessionId);

protected:
    void contextMenuEvent(QContextMenuEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

private:
    static bool IsAsrEpochGreater(quint64 lhsGeneration, quint64 lhsSerial, quint64 rhsGeneration, quint64 rhsSerial);
    void EnsureAsrTrackInitialized();
    void TouchAsrTrack();
    void RefreshFrame();

    QString sessionId_;
    VideoWidget* video_ = nullptr;
    QLabel* titleLabel_ = nullptr;
    QLabel* stateLabel_ = nullptr;
    QLabel* detailLabel_ = nullptr;
    QPushButton* detectorButton_ = nullptr;
    QPushButton* asrButton_ = nullptr;
    QPushButton* recordButton_ = nullptr;
    QPushButton* muteButton_ = nullptr;
    QPushButton* reopenButton_ = nullptr;
    QPushButton* snapshotButton_ = nullptr;
    QPushButton* popoutButton_ = nullptr;
    QPushButton* removeButton_ = nullptr;
    int lastDetectionCount_ = -1;
    int lastDetectionFrameWidth_ = 0;
    int lastDetectionFrameHeight_ = 0;
    QPoint dragStartPos_;
    SubtitleTrack asrTrack_;
    quint64 asrTrackGeneration_ = 0;
    quint64 asrTrackSerial_ = 0;
    bool asrTrackEpochKnown_ = false;
    quint64 asrClearBarrierGeneration_ = 0;
    quint64 asrClearBarrierSerial_ = 0;
    bool asrClearBarrierKnown_ = false;
};

class MonitorTilePopoutWindow final : public QWidget
{
    Q_OBJECT

public:
    explicit MonitorTilePopoutWindow(const QString& sessionId, QWidget* parent = nullptr);

    QString SessionId() const;
    void SetTile(MonitorTileWidget* tile);
    MonitorTileWidget* Tile() const;

signals:
    void Closing(const QString& sessionId);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    QString sessionId_;
    QVBoxLayout* layout_ = nullptr;
    MonitorTileWidget* tile_ = nullptr;
};
