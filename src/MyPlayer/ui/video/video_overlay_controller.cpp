

#include "video_overlay_controller.h"

#include <cstdlib>

#include <QFont>
#include <QFontMetrics>
#include <QMetaObject>
#include <QPainter>
#include <QPen>
#include <QStringList>
#include <QThread>
#include <QtGlobal>
#include <QWidget>

static const QColor kCocoColors[] = {
    QColor(255, 56, 56),   QColor(255, 157, 151), QColor(255, 112, 31),
    QColor(255, 178, 29),  QColor(207, 210, 49),  QColor(72, 249, 10),
    QColor(146, 204, 23),  QColor(61, 219, 134),  QColor(26, 147, 52),
    QColor(0, 212, 187),   QColor(44, 153, 168),  QColor(0, 194, 255),
    QColor(52, 69, 147),   QColor(100, 115, 255), QColor(0, 24, 236),
    QColor(132, 56, 255),  QColor(82, 0, 133),    QColor(203, 56, 255),
    QColor(255, 149, 200), QColor(255, 55, 199)
};
constexpr int kCocoColorCount = sizeof(kCocoColors) / sizeof(kCocoColors[0]);

OverlayRenderer::OverlayRenderer(QWidget* host)
    : host_(host)
{
}

OverlayRenderer::~OverlayRenderer() = default;

void OverlayRenderer::OnHostResized()
{
    UpdateOverlay();
}

void OverlayRenderer::SetSubtitleTrack(const SubtitleTrack* track)
{
    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(subtitleMux_);
        if (track)
        {
            changed = subtitleTrackId_ != track->id || subtitleTrackRevision_ != track->revision;
            if (changed)
            {
                subtitleRenderer_.setTrack(track);
                subtitleTrackId_ = track->id;
                subtitleTrackRevision_ = track->revision;
            }
        }
        else
        {
            changed = subtitleTrackId_ != 0;
            if (changed)
            {
                subtitleRenderer_.clearTrack();
                subtitleTrackId_ = 0;
                subtitleTrackRevision_ = 0;
            }
        }
    }

    if (changed)
        UpdateOverlay();
}

void OverlayRenderer::ClearSubtitleTrack()
{
    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(subtitleMux_);
        changed = subtitleTrackId_ != 0;
        if (changed)
        {
            subtitleRenderer_.clearTrack();
            subtitleTrackId_ = 0;
            subtitleTrackRevision_ = 0;
        }
    }

    if (changed)
        UpdateOverlay();
}

void OverlayRenderer::SetSubtitleStyle(int fontPointSize, int bottomMarginPx)
{
    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(subtitleMux_);
        changed = subtitleFontPointSizeState_ != fontPointSize ||
            subtitleBottomMarginState_ != bottomMarginPx;
        if (changed)
        {
            subtitleRenderer_.setStyle(fontPointSize, bottomMarginPx);
            subtitleFontPointSizeState_ = fontPointSize;
            subtitleBottomMarginState_ = bottomMarginPx;
        }
    }

    if (changed)
        UpdateOverlay();
}

void OverlayRenderer::SetSubtitleClock(long long clockMs, long long offsetMs)
{
    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(subtitleMux_);
        changed = subtitleClockMs_ != clockMs || subtitleOffsetMs_ != offsetMs;
        if (changed)
        {
            subtitleClockMs_ = clockMs;
            subtitleOffsetMs_ = offsetMs;
        }
    }

    if (changed)
        UpdateOverlay();
}

void OverlayRenderer::SetDetectionOverlay(bool enabled)
{
    detOverlayEnabled_ = enabled;
    if (!enabled)
    {
        std::lock_guard<std::mutex> lock(detMux_);
        currentDetections_.boxes.clear();
    }
    UpdateOverlay();
}

void OverlayRenderer::UpdateDetections(const DetectionResult& result)
{
    {
        std::lock_guard<std::mutex> lock(detMux_);
        currentDetections_ = result;
    }
    UpdateOverlay();
}

void OverlayRenderer::SetDebugOverlayVisible(bool visible)
{
    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(debugMux_);
        changed = debugOverlayVisible_ != visible;
        debugOverlayVisible_ = visible;
    }

    if (changed)
        UpdateOverlay();
}

bool OverlayRenderer::IsDebugOverlayVisible() const
{
    std::lock_guard<std::mutex> lock(debugMux_);
    return debugOverlayVisible_;
}

void OverlayRenderer::SetDebugOverlayText(const QString& text)
{
    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(debugMux_);
        changed = debugOverlayText_ != text;
        debugOverlayText_ = text;
    }

    if (changed)
        UpdateOverlay();
}

void OverlayRenderer::ShowSubtitleStatusOsd(const QString& text)
{
    SetCenterOsdText(
        CenterOsdSlot::Subtitle,
        text,
        QColor(255, 215, 0),
        QColor(0, 0, 0, 180));
}

void OverlayRenderer::HideSubtitleStatusOsd()
{
    SetCenterOsdVisible(CenterOsdSlot::Subtitle, false);
}

void OverlayRenderer::ShowFeatureStatusOsd(const QString& text)
{
    SetCenterOsdText(
        CenterOsdSlot::Feature,
        text,
        QColor(0, 255, 0),
        QColor(0, 0, 0, 180));
}

void OverlayRenderer::HideFeatureStatusOsd()
{
    SetCenterOsdVisible(CenterOsdSlot::Feature, false);
}

void OverlayRenderer::ShowDisplayStatusOsd(const QString& text)
{
    SetCenterOsdText(
        CenterOsdSlot::Display,
        text,
        QColor(0, 255, 0),
        QColor(0, 0, 0, 180));
}

void OverlayRenderer::HideDisplayStatusOsd()
{
    SetCenterOsdVisible(CenterOsdSlot::Display, false);
}

void OverlayRenderer::SetCenterOsdText(CenterOsdSlot slot, const QString& text,
    const QColor& foreground, const QColor& background)
{
    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(centerOsdMux_);
        CenterOsdState& state = centerOsdStates_[static_cast<int>(slot)];
        const QString trimmed = text.trimmed();
        changed = state.text != trimmed
            || state.foreground != foreground
            || state.background != background
            || !state.visible;
        state.text = trimmed;
        state.foreground = foreground;
        state.background = background;
        state.visible = !trimmed.isEmpty();
    }

    if (changed)
        UpdateOverlay();
}

void OverlayRenderer::SetCenterOsdVisible(CenterOsdSlot slot, bool visible)
{
    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(centerOsdMux_);
        CenterOsdState& state = centerOsdStates_[static_cast<int>(slot)];
        changed = state.visible != visible;
        state.visible = visible && !state.text.trimmed().isEmpty();
    }

    if (changed)
        UpdateOverlay();
}

void OverlayRenderer::UpdateOverlay()
{
    if (!host_)
        return;

    if (QThread::currentThread() != host_->thread())
    {
        QMetaObject::invokeMethod(host_, [host = host_]() {
            if (host)
                host->update();
        }, Qt::QueuedConnection);
        return;
    }

    host_->update();
}

void OverlayRenderer::Paint(QPainter& painter)
{
    PaintSubtitle(painter);
    DrawDetections(painter);
    PaintDebugOverlay(painter);
    PaintCenterOsd(painter);
}

void OverlayRenderer::PaintSubtitle(QPainter& painter)
{
    if (!painter.isActive() || !host_)
        return;

    long long clockMs = 0;
    long long offsetMs = 0;
    {
        std::lock_guard<std::mutex> lock(subtitleMux_);
        subtitleRenderer_.setFrame(host_->size(), host_->devicePixelRatioF());
        clockMs = subtitleClockMs_;
        offsetMs = subtitleOffsetMs_;
        subtitleRenderer_.draw(painter, clockMs, offsetMs);
    }
}

void OverlayRenderer::DrawDetections(QPainter& painter)
{
    if (!detOverlayEnabled_ || !painter.isActive() || !host_)
        return;

    DetectionResult detections;
    {
        std::lock_guard<std::mutex> lock(detMux_);
        detections = currentDetections_;
    }

    if (detections.boxes.empty() || detections.frameWidth <= 0 || detections.frameHeight <= 0)
        return;

    const int widgetWidth = host_->width();
    const int widgetHeight = host_->height();
    const float videoAspect = static_cast<float>(detections.frameWidth) /
        static_cast<float>(detections.frameHeight);
    const float widgetAspect = static_cast<float>(widgetWidth) /
        static_cast<float>(widgetHeight);

    float drawX = 0.0f;
    float drawY = 0.0f;
    float drawWidth = 0.0f;
    float drawHeight = 0.0f;
    if (videoAspect > widgetAspect)
    {
        drawWidth = static_cast<float>(widgetWidth);
        drawHeight = drawWidth / videoAspect;
        drawY = (static_cast<float>(widgetHeight) - drawHeight) / 2.0f;
    }
    else
    {
        drawHeight = static_cast<float>(widgetHeight);
        drawWidth = drawHeight * videoAspect;
        drawX = (static_cast<float>(widgetWidth) - drawWidth) / 2.0f;
    }

    painter.setRenderHint(QPainter::Antialiasing);
    painter.setFont(QFont("Consolas", 11));

    for (const auto& box : detections.boxes)
    {
        const int bx = static_cast<int>(drawX + box.x1 * drawWidth);
        const int by = static_cast<int>(drawY + box.y1 * drawHeight);
        const int bw = static_cast<int>((box.x2 - box.x1) * drawWidth);
        const int bh = static_cast<int>((box.y2 - box.y1) * drawHeight);

        const QColor color = kCocoColors[std::abs(box.classId) % kCocoColorCount];
        painter.setPen(QPen(color, 2));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(bx, by, bw, bh);

        const QString name = box.className.isEmpty()
            ? QString("#%1").arg(box.classId)
            : box.className;
        QString label = name + " " + QString::number(static_cast<int>(box.confidence * 100.0f)) + "%";
        if (box.trackId >= 0)
            label = "ID:" + QString::number(box.trackId) + " " + label;

        const int textWidth = painter.fontMetrics().horizontalAdvance(label) + 8;
        painter.fillRect(bx, by - 18, textWidth, 18, color);
        painter.setPen(Qt::white);
        painter.drawText(bx + 4, by - 4, label);
    }
}

void OverlayRenderer::PaintDebugOverlay(QPainter& painter)
{
    if (!painter.isActive() || !host_)
        return;

    QString text;
    bool visible = false;
    {
        std::lock_guard<std::mutex> lock(debugMux_);
        visible = debugOverlayVisible_;
        text = debugOverlayText_;
    }

    if (!visible)
        return;

    text = text.trimmed();
    if (text.isEmpty())
        return;

    const QRect viewport = host_->rect();
    const int margin = qMax(12, qMin(viewport.width(), viewport.height()) / 36);
    const int availableWidth = qMax(180, viewport.width() - margin * 2);
    const int availableHeight = qMax(120, viewport.height() - margin * 2);
    const int maxPanelWidth = qMin(availableWidth, qMax(320, viewport.width() * 48 / 100));
    const int paddingX = 12;
    const int paddingY = 10;

    QStringList rawLines = text.split('\n');
    if (rawLines.isEmpty())
        rawLines.push_back(text);

    int fontPointSize = viewport.height() >= 900 ? 12 : (viewport.height() >= 640 ? 11 : 10);
    QStringList displayLines;
    int lineSpacing = 0;
    int textWidth = 0;

    for (;;)
    {
        QFont font("Consolas");
        font.setStyleHint(QFont::Monospace);
        font.setPointSize(fontPointSize);
        painter.setFont(font);

        const QFontMetrics metrics(font);
        const int maxTextWidth = qMax(120, maxPanelWidth - paddingX * 2);
        displayLines.clear();
        textWidth = 0;
        lineSpacing = metrics.lineSpacing();

        for (const QString& rawLine : rawLines)
        {
            const QString line = metrics.elidedText(rawLine, Qt::ElideRight, maxTextWidth);
            displayLines.push_back(line);
            textWidth = qMax(textWidth, metrics.horizontalAdvance(line));
        }

        const int panelHeight = paddingY * 2 + lineSpacing * displayLines.size();
        if (panelHeight <= availableHeight || fontPointSize <= 9)
            break;

        fontPointSize--;
    }

    QFont font("Consolas");
    font.setStyleHint(QFont::Monospace);
    font.setPointSize(fontPointSize);
    painter.setFont(font);

    const QFontMetrics metrics(font);
    lineSpacing = metrics.lineSpacing();

    const int maxLines = qMax(3, (availableHeight - paddingY * 2) / qMax(1, lineSpacing));
    if (displayLines.size() > maxLines)
    {
        displayLines = displayLines.mid(0, maxLines);
        if (maxLines > 0)
            displayLines[maxLines - 1] = metrics.elidedText("...", Qt::ElideRight,
                qMax(120, maxPanelWidth - paddingX * 2));
    }

    textWidth = 0;
    for (const QString& line : displayLines)
        textWidth = qMax(textWidth, metrics.horizontalAdvance(line));

    const int panelWidth = qMin(maxPanelWidth, textWidth + paddingX * 2);
    const int panelHeight = paddingY * 2 + lineSpacing * displayLines.size();
    const QRect panelRect(viewport.width() - margin - panelWidth, margin, panelWidth, panelHeight);

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(QColor(236, 241, 247, 72), 1));
    painter.setBrush(QColor(8, 12, 18, 176));
    painter.drawRoundedRect(panelRect, 8, 8);

    painter.setPen(QColor(236, 241, 247, 228));
    int y = panelRect.top() + paddingY + metrics.ascent();
    const int x = panelRect.left() + paddingX;
    for (const QString& line : displayLines)
    {
        painter.drawText(x, y, line);
        y += lineSpacing;
    }
    painter.restore();
}

void OverlayRenderer::PaintCenterOsd(QPainter& painter)
{
    if (!painter.isActive() || !host_)
        return;

    std::array<CenterOsdState, static_cast<int>(CenterOsdSlot::Count)> centerSlots;
    {
        std::lock_guard<std::mutex> lock(centerOsdMux_);
        centerSlots = centerOsdStates_;
    }

    QList<CenterOsdState> visibleSlots;
    for (const CenterOsdState& slot : centerSlots)
    {
        if (slot.visible && !slot.text.trimmed().isEmpty())
            visibleSlots.push_back(slot);
    }
    if (visibleSlots.isEmpty())
        return;

    QFont font("Consolas");
    font.setStyleHint(QFont::Monospace);
    font.setPointSize(host_->height() >= 900 ? 18 : 16);
    font.setBold(true);
    painter.setFont(font);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    const QFontMetrics metrics(font);
    const int linePaddingX = 20;
    const int linePaddingY = 10;
    const int spacing = 12;
    const int centerY = host_->height() / 3;

    int totalHeight = 0;
    QList<QRect> rects;
    rects.reserve(visibleSlots.size());
    for (const CenterOsdState& slot : visibleSlots)
    {
        const int textWidth = metrics.horizontalAdvance(slot.text);
        const int textHeight = metrics.height();
        const QRect rect(
            0,
            0,
            textWidth + linePaddingX * 2,
            textHeight + linePaddingY * 2);
        rects.push_back(rect);
        totalHeight += rect.height();
    }
    totalHeight += spacing * (visibleSlots.size() - 1);

    int currentY = centerY - totalHeight / 2;
    for (int i = 0; i < visibleSlots.size(); ++i)
    {
        const CenterOsdState& slot = visibleSlots[i];
        QRect rect = rects[i];
        rect.moveLeft((host_->width() - rect.width()) / 2);
        rect.moveTop(currentY);

        painter.save();
        painter.setPen(Qt::NoPen);
        painter.setBrush(slot.background);
        painter.drawRoundedRect(rect, 8, 8);
        painter.setPen(slot.foreground);
        painter.drawText(rect.adjusted(linePaddingX, linePaddingY, -linePaddingX, -linePaddingY),
            Qt::AlignCenter, slot.text);
        painter.restore();

        currentY += rect.height() + spacing;
    }
}
