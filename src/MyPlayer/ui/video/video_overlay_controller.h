#pragma once

#include <array>
#include <mutex>
#include <QColor>
#include <QString>

#include "../../features/detector/detector_thread.h"
#include "../../features/subtitle/libass_renderer.h"

class QPainter;
class QWidget;

class OverlayRenderer
{
public:
    explicit OverlayRenderer(QWidget* host);
    ~OverlayRenderer();

    void OnHostResized();

    void SetSubtitleTrack(const SubtitleTrack* track);
    void ClearSubtitleTrack();
    void SetSubtitleStyle(int fontPointSize, int bottomMarginPx);
    void SetSubtitleClock(long long clockMs, long long offsetMs);

    void SetDetectionOverlay(bool enabled);
    void UpdateDetections(const DetectionResult& result);
    void SetDebugOverlayVisible(bool visible);
    bool IsDebugOverlayVisible() const;
    void SetDebugOverlayText(const QString& text);
    void ShowSubtitleStatusOsd(const QString& text);
    void HideSubtitleStatusOsd();
    void ShowFeatureStatusOsd(const QString& text);
    void HideFeatureStatusOsd();
    void ShowDisplayStatusOsd(const QString& text);
    void HideDisplayStatusOsd();
    void Paint(QPainter& painter);

private:
    enum class CenterOsdSlot : int
    {
        Subtitle = 0,
        Feature = 1,
        Display = 2,
        Count
    };

    struct CenterOsdState
    {
        QString text;
        QColor foreground = Qt::white;
        QColor background = QColor(0, 0, 0, 180);
        bool visible = false;
    };

    void UpdateOverlay();
    void PaintSubtitle(QPainter& painter);
    void DrawDetections(QPainter& painter);
    void PaintDebugOverlay(QPainter& painter);
    void PaintCenterOsd(QPainter& painter);
    void SetCenterOsdText(CenterOsdSlot slot, const QString& text,
        const QColor& foreground, const QColor& background);
    void SetCenterOsdVisible(CenterOsdSlot slot, bool visible);

    QWidget* host_ = nullptr;

    std::mutex detMux_;
    DetectionResult currentDetections_;
    bool detOverlayEnabled_ = false;

    std::mutex subtitleMux_;
    LibassRenderer subtitleRenderer_;
    long long subtitleClockMs_ = 0;
    long long subtitleOffsetMs_ = 0;
    int subtitleTrackId_ = 0;
    quint64 subtitleTrackRevision_ = 0;
    int subtitleFontPointSizeState_ = 24;
    int subtitleBottomMarginState_ = 120;

    mutable std::mutex debugMux_;
    QString debugOverlayText_;
    bool debugOverlayVisible_ = false;

    mutable std::mutex centerOsdMux_;
    std::array<CenterOsdState, static_cast<int>(CenterOsdSlot::Count)> centerOsdStates_{};
};
