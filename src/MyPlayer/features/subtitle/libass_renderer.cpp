#include "libass_renderer.h"

#include <QByteArray>
#include <QChar>
#include <QColor>
#include <QDebug>
#include <QFont>
#include <QPainter>
#include <QRegularExpression>
#include <QString>

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
constexpr long long kDisplayToleranceMs = 120;

const SubtitleCue* CueAt(const SubtitleTrack& track, long long adjustedMs)
{
    if (track.id == 0)
        return nullptr;

    for (const SubtitleCue& cue : track.cues)
    {
        if (cue.endMs < adjustedMs - kDisplayToleranceMs)
            continue;
        if (cue.startMs <= adjustedMs + kDisplayToleranceMs &&
            cue.endMs >= adjustedMs - kDisplayToleranceMs)
        {
            return &cue;
        }
        if (cue.startMs > adjustedMs + kDisplayToleranceMs)
            break;
    }

    return nullptr;
}

QString NormalizeAssPayload(QString text)
{
    text.replace("\r\n", "\n");
    text.replace('\r', '\n');
    text.replace('\n', "\\N");
    return text.trimmed();
}

QString PlainTextToAss(QString text)
{
    text.replace("\r\n", "\n");
    text.replace('\r', '\n');
    text.replace(QRegularExpression("(?i)<br\\s*/?>"), "\n");
    text.replace(QRegularExpression("(?i)<i>"), "{\\i1}");
    text.replace(QRegularExpression("(?i)</i>"), "{\\i0}");
    text.replace(QRegularExpression("(?i)<b>"), "{\\b1}");
    text.replace(QRegularExpression("(?i)</b>"), "{\\b0}");
    text.replace(QRegularExpression("(?i)<u>"), "{\\u1}");
    text.replace(QRegularExpression("(?i)</u>"), "{\\u0}");
    text.replace(QRegularExpression("(?i)&nbsp;"), " ");
    text.replace(QRegularExpression("<[^>]+>"), "");
    text.replace('{', '(');
    text.replace('}', ')');
    text.replace('\n', QChar(0x2028));
    text = text.trimmed();
    text.replace(QChar(0x2028), "\\N");
    return text;
}

QString CueToAssText(const SubtitleCue& cue)
{
    if (!cue.assText.trimmed().isEmpty())
        return NormalizeAssPayload(cue.assText);
    return PlainTextToAss(cue.text);
}

bool TrackHasAssPayload(const SubtitleTrack& track)
{
    if (track.id == 0)
        return false;

    for (const SubtitleCue& cue : track.cues)
    {
        if (!cue.assText.trimmed().isEmpty())
            return true;
    }

    return false;
}

bool ShouldUseLibass(const SubtitleTrack& track, bool libassAvailable)
{
    return libassAvailable && track.renderWithLibass && TrackHasAssPayload(track);
}

QString FormatAssTime(long long ms)
{
    ms = std::max(0LL, ms);
    const long long totalCentis = ms / 10LL;
    const long long centis = totalCentis % 100LL;
    const long long totalSeconds = totalCentis / 100LL;
    const long long seconds = totalSeconds % 60LL;
    const long long totalMinutes = totalSeconds / 60LL;
    const long long minutes = totalMinutes % 60LL;
    const long long hours = totalMinutes / 60LL;

    return QString("%1:%2:%3.%4")
        .arg(hours)
        .arg(minutes, 2, 10, QChar('0'))
        .arg(seconds, 2, 10, QChar('0'))
        .arg(centis, 2, 10, QChar('0'));
}

QByteArray BuildTrackScript(const SubtitleTrack& track, int frameWidth,
    int frameHeight, qreal devicePixelRatio, int fontPointSize, int bottomMarginPx)
{
    if (!track.renderWithLibass || track.id == 0 || track.cues.isEmpty() ||
        frameWidth <= 0 || frameHeight <= 0)
    {
        return {};
    }

    const int sideMarginPx = std::max(32, qRound(frameWidth * 0.08));
    const int bottomMarginScaled = std::max(20, qRound(bottomMarginPx * devicePixelRatio));
    const int fontPx = std::max(16, qRound(fontPointSize * devicePixelRatio * 1.15));

    QString script;
    script.reserve(track.cues.size() * 96 + 512);
    script += "[Script Info]\n";
    script += "ScriptType: v4.00+\n";
    script += QString("PlayResX: %1\n").arg(frameWidth);
    script += QString("PlayResY: %1\n").arg(frameHeight);
    script += "ScaledBorderAndShadow: yes\n";
    script += "WrapStyle: 0\n";
    script += "\n[V4+ Styles]\n";
    script += "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, "
              "Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, "
              "Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\n";
    script += QString("Style: Default,Microsoft YaHei UI,%1,&H00FFFFFF,&H000000FF,&H96000000,&H00000000,"
                      "-1,0,0,0,100,100,0,0,1,2.4,0.8,2,%2,%2,%3,1\n")
        .arg(fontPx)
        .arg(sideMarginPx)
        .arg(bottomMarginScaled);
    script += "\n[Events]\n";
    script += "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n";

    bool hasDialogue = false;
    for (const SubtitleCue& cue : track.cues)
    {
        const QString assText = CueToAssText(cue);
        if (assText.isEmpty())
            continue;

        hasDialogue = true;
        script += QString("Dialogue: 0,%1,%2,Default,,0,0,0,,%3\n")
            .arg(FormatAssTime(cue.startMs))
            .arg(FormatAssTime(cue.endMs))
            .arg(assText);
    }

    if (!hasDialogue)
        return {};

    return script.toUtf8();
}

void DrawFallbackSubtitle(QPainter& painter, const SubtitleTrack& track,
    long long adjustedMs, const QSize& logicalSize, int fontPointSize, int bottomMarginPx)
{
    const SubtitleCue* cue = CueAt(track, adjustedMs);
    if (track.source == SubtitleSourceType::Whisper)
    {
        static long long lastLoggedClockMs = std::numeric_limits<long long>::min();
        static long long lastLoggedCueStartMs = std::numeric_limits<long long>::min();

        const long long cueStartMs = cue ? cue->startMs : std::numeric_limits<long long>::min();
        if (std::llabs(adjustedMs - lastLoggedClockMs) >= 1000 ||
            cueStartMs != lastLoggedCueStartMs)
        {
            lastLoggedClockMs = adjustedMs;
            lastLoggedCueStartMs = cueStartMs;
            if (cue)
            {
                qDebug().noquote()
                    << QString("[subdbg] render fallback hit track=%1 cues=%2 clock=%3 cue=%4-%5 text=\"%6\"")
                           .arg(track.id)
                           .arg(track.cues.size())
                           .arg(adjustedMs)
                           .arg(cue->startMs)
                           .arg(cue->endMs)
                           .arg(cue->text.left(80));
            }
            else
            {
                qDebug().noquote()
                    << QString("[subdbg] render fallback miss track=%1 cues=%2 clock=%3")
                           .arg(track.id)
                           .arg(track.cues.size())
                           .arg(adjustedMs);
            }
        }
    }

    if (!cue)
        return;

    QString text = cue->text.trimmed();
    if (text.isEmpty())
    {
        text = cue->assText;
        text.replace("\\N", "\n");
        text.remove(QRegularExpression("\\{[^}]*\\}"));
        text = text.trimmed();
    }

    if (text.isEmpty() || logicalSize.isEmpty())
        return;

    const int width = logicalSize.width();
    const int height = logicalSize.height();
    const int sideMargin = std::max(24, width / 10);
    const int maxWidth = std::max(120, width - sideMargin * 2);
    const int bottom = std::clamp(bottomMarginPx, 20, std::max(40, height - 20));

    QFont font("Microsoft YaHei UI");
    font.setPointSize(fontPointSize);
    font.setBold(true);
    painter.setFont(font);

    QRect measureRect(0, 0, maxWidth, height / 3);
    const int flags = Qt::AlignHCenter | Qt::AlignVCenter | Qt::TextWordWrap;
    const QRect textRect = painter.fontMetrics().boundingRect(measureRect, flags, text);
    QRect bubbleRect(
        (width - textRect.width()) / 2 - 18,
        height - bottom - textRect.height() - 18,
        textRect.width() + 36,
        textRect.height() + 24);

    if (bubbleRect.left() < 12)
        bubbleRect.moveLeft(12);
    if (bubbleRect.right() > width - 12)
        bubbleRect.moveRight(width - 12);

    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 180));
    painter.drawRoundedRect(bubbleRect, 10, 10);

    painter.setPen(QColor(255, 255, 255));
    painter.drawText(bubbleRect.adjusted(16, 10, -16, -10), flags, text);
}
}

void AssRenderCache::Invalidate()
{
    cachedTimeMs_ = std::numeric_limits<long long>::min();
    cachedImage_ = QImage();
}

void AssRenderCache::Render(long long adjustedMs, bool useLibass,
    LibassRuntimeContext& runtimeContext, int frameWidth, int frameHeight,
    qreal devicePixelRatio)
{
    if (cachedTimeMs_ == adjustedMs && !cachedImage_.isNull())
        return;

    if (!useLibass)
    {
        cachedTimeMs_ = adjustedMs;
        cachedImage_ = QImage();
        return;
    }

    bool changed = false;
    const QImage rendered = runtimeContext.RenderFrame(adjustedMs, frameWidth, frameHeight,
        devicePixelRatio, &changed);
    if (!changed && cachedTimeMs_ != std::numeric_limits<long long>::min())
    {
        cachedTimeMs_ = adjustedMs;
        return;
    }

    cachedTimeMs_ = adjustedMs;
    cachedImage_ = rendered;
}

const QImage& AssRenderCache::Image() const
{
    return cachedImage_;
}

LibassRenderer::LibassRenderer() = default;

LibassRenderer::~LibassRenderer() = default;

void LibassRenderer::clearTrack()
{
    trackData = SubtitleTrack{};
    trackDirty = true;
    runtimeContext_.ClearTrack();
    InvalidateCache();
}

void LibassRenderer::setTrack(const SubtitleTrack* track)
{
    if (!track || track->id == 0 || track->cues.isEmpty())
    {
        clearTrack();
        return;
    }

    if (trackData.id == track->id && trackData.revision == track->revision)
        return;

    trackData = *track;
    trackDirty = true;
    InvalidateCache();
}

void LibassRenderer::setStyle(int fontPointSizeIn, int bottomMarginPxIn)
{
    const int clampedFontPt = std::clamp(fontPointSizeIn, 14, 72);
    const int clampedBottomMargin = std::clamp(bottomMarginPxIn, 20, 400);
    if (fontPointSize == clampedFontPt && bottomMarginPx == clampedBottomMargin)
        return;

    fontPointSize = clampedFontPt;
    bottomMarginPx = clampedBottomMargin;
    trackDirty = true;
    InvalidateCache();
}

void LibassRenderer::setFrame(const QSize& logicalSizeIn, qreal devicePixelRatioIn)
{
    const QSize saneLogicalSize = logicalSizeIn.expandedTo(QSize(0, 0));
    const qreal saneDpr = devicePixelRatioIn > 0.0 ? devicePixelRatioIn : 1.0;
    const int newFrameWidth = std::max(0, qRound(saneLogicalSize.width() * saneDpr));
    const int newFrameHeight = std::max(0, qRound(saneLogicalSize.height() * saneDpr));

    if (logicalSize == saneLogicalSize &&
        qFuzzyCompare(devicePixelRatio, saneDpr) &&
        frameWidth == newFrameWidth &&
        frameHeight == newFrameHeight)
    {
        return;
    }

    logicalSize = saneLogicalSize;
    devicePixelRatio = saneDpr;
    frameWidth = newFrameWidth;
    frameHeight = newFrameHeight;
    trackDirty = true;
    InvalidateCache();
}

void LibassRenderer::draw(QPainter& painter, long long clockMs, long long offsetMs)
{
    const long long adjustedMs = std::max(0LL, clockMs - offsetMs);
    if (logicalSize.isEmpty())
        return;

    if (!ShouldUseLibass(trackData, runtimeContext_.IsAvailable()))
    {
        DrawFallbackSubtitle(painter, trackData, adjustedMs, logicalSize, fontPointSize, bottomMarginPx);
        return;
    }

    RebuildTrackIfNeeded();
    renderCache_.Render(adjustedMs, true, runtimeContext_, frameWidth, frameHeight,
        devicePixelRatio);
    if (!renderCache_.Image().isNull())
        painter.drawImage(QPointF(0.0, 0.0), renderCache_.Image());
}

bool LibassRenderer::isAvailable() const
{
    return runtimeContext_.IsAvailable();
}

void LibassRenderer::InvalidateCache()
{
    renderCache_.Invalidate();
}

void LibassRenderer::RebuildTrackIfNeeded()
{
    if (!trackDirty)
        return;

    runtimeContext_.ClearTrack();
    if (!ShouldUseLibass(trackData, runtimeContext_.IsAvailable()) ||
        frameWidth <= 0 || frameHeight <= 0)
    {
        trackDirty = false;
        return;
    }

    const QByteArray scriptUtf8 = BuildTrackScript(trackData, frameWidth,
        frameHeight, devicePixelRatio, fontPointSize, bottomMarginPx);
    runtimeContext_.LoadScript(scriptUtf8, frameWidth, frameHeight);
    trackDirty = false;
}

#include <QDebug>
#include <QtGlobal>

#include <cstdarg>
#include <cstdio>

#if __has_include(<ass/ass.h>)
#define MYPLAYER_HAS_LIBASS 1
extern "C" {
#include <ass/ass.h>
}
#else
#define MYPLAYER_HAS_LIBASS 0
#endif

namespace
{
#if MYPLAYER_HAS_LIBASS

void LibassMessageCallback(int level, const char* fmt, va_list args, void*)
{
    if (level >= 4 || !fmt)
        return;

    char buffer[1024];
    buffer[0] = '\0';
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    const QString text = QString::fromLocal8Bit(buffer).trimmed();
    if (!text.isEmpty())
        qWarning().noquote() << "[ass]" << text;
}

void BlendAssImage(QImage& target, const ASS_Image* image)
{
    if (!image || target.isNull())
        return;

    const quint8 srcR = static_cast<quint8>((image->color >> 24) & 0xFF);
    const quint8 srcG = static_cast<quint8>((image->color >> 16) & 0xFF);
    const quint8 srcB = static_cast<quint8>((image->color >> 8) & 0xFF);
    const quint8 srcA = static_cast<quint8>(255 - (image->color & 0xFF));
    if (srcA == 0)
        return;

    for (int y = 0; y < image->h; ++y)
    {
        const int dstY = image->dst_y + y;
        if (dstY < 0 || dstY >= target.height())
            continue;

        const quint8* srcRow = image->bitmap + y * image->stride;
        QRgb* dstRow = reinterpret_cast<QRgb*>(target.scanLine(dstY));
        for (int x = 0; x < image->w; ++x)
        {
            const int dstX = image->dst_x + x;
            if (dstX < 0 || dstX >= target.width())
                continue;

            const quint8 glyphAlpha = srcRow[x];
            if (glyphAlpha == 0)
                continue;

            const quint32 alpha = (static_cast<quint32>(glyphAlpha) * srcA + 127U) / 255U;
            if (alpha == 0)
                continue;

            QRgb& dst = dstRow[dstX];
            const quint32 dstA = qAlpha(dst);
            const quint32 dstR = qRed(dst);
            const quint32 dstG = qGreen(dst);
            const quint32 dstB = qBlue(dst);
            const quint32 invAlpha = 255U - alpha;

            const quint32 outA = alpha + ((dstA * invAlpha + 127U) / 255U);
            const quint32 outR = ((srcR * alpha) + (dstR * invAlpha) + 127U) / 255U;
            const quint32 outG = ((srcG * alpha) + (dstG * invAlpha) + 127U) / 255U;
            const quint32 outB = ((srcB * alpha) + (dstB * invAlpha) + 127U) / 255U;

            dst = qRgba(static_cast<int>(outR), static_cast<int>(outG),
                static_cast<int>(outB), static_cast<int>(outA));
        }
    }
}
#endif
}

class LibassRuntimeContext::Private
{
public:
#if MYPLAYER_HAS_LIBASS
    ASS_Library* library = nullptr;
    ASS_Renderer* renderer = nullptr;
    ASS_Track* track = nullptr;
    QByteArray scriptUtf8;
#endif
};

LibassRuntimeContext::LibassRuntimeContext()
    : d_(std::make_unique<Private>())
{
#if MYPLAYER_HAS_LIBASS
    d_->library = ass_library_init();
    if (d_->library)
    {
        ass_set_message_cb(d_->library, &LibassMessageCallback, nullptr);
        d_->renderer = ass_renderer_init(d_->library);
    }

    if (d_->renderer)
        ass_set_fonts(d_->renderer, nullptr, "Microsoft YaHei UI", 1, nullptr, 1);
#endif
}

LibassRuntimeContext::~LibassRuntimeContext()
{
#if MYPLAYER_HAS_LIBASS
    if (d_)
    {
        if (d_->track)
            ass_free_track(d_->track);
        if (d_->renderer)
            ass_renderer_done(d_->renderer);
        if (d_->library)
            ass_library_done(d_->library);
    }
#endif
}

bool LibassRuntimeContext::IsAvailable() const
{
#if MYPLAYER_HAS_LIBASS
    return d_ && d_->renderer;
#else
    return false;
#endif
}

void LibassRuntimeContext::ClearTrack()
{
#if MYPLAYER_HAS_LIBASS
    if (!d_)
        return;

    if (d_->track)
    {
        ass_free_track(d_->track);
        d_->track = nullptr;
    }
    d_->scriptUtf8.clear();
#endif
}

bool LibassRuntimeContext::LoadScript(const QByteArray& scriptUtf8, int frameWidth, int frameHeight)
{
#if MYPLAYER_HAS_LIBASS
    ClearTrack();
    if (!d_ || !d_->library || !d_->renderer || scriptUtf8.isEmpty() ||
        frameWidth <= 0 || frameHeight <= 0)
    {
        return false;
    }

    d_->scriptUtf8 = scriptUtf8;
    d_->track = ass_read_memory(d_->library, d_->scriptUtf8.data(), d_->scriptUtf8.size(), nullptr);
    if (!d_->track)
    {
        d_->scriptUtf8.clear();
        return false;
    }

    ass_set_frame_size(d_->renderer, frameWidth, frameHeight);
    ass_set_storage_size(d_->renderer, frameWidth, frameHeight);
    ass_set_fonts(d_->renderer, nullptr, "Microsoft YaHei UI", 1, nullptr, 1);
    return true;
#else
    Q_UNUSED(scriptUtf8);
    Q_UNUSED(frameWidth);
    Q_UNUSED(frameHeight);
    return false;
#endif
}

QImage LibassRuntimeContext::RenderFrame(long long adjustedMs, int frameWidth, int frameHeight,
    qreal devicePixelRatio, bool* changed)
{
    if (changed)
        *changed = false;

#if MYPLAYER_HAS_LIBASS
    if (!d_ || !d_->renderer || !d_->track || frameWidth <= 0 || frameHeight <= 0)
        return {};

    int changedFlag = 0;
    ASS_Image* images = ass_render_frame(d_->renderer, d_->track, adjustedMs, &changedFlag);
    if (changed)
        *changed = changedFlag != 0;

    QImage composed(frameWidth, frameHeight, QImage::Format_ARGB32_Premultiplied);
    composed.fill(Qt::transparent);
    for (ASS_Image* image = images; image; image = image->next)
        BlendAssImage(composed, image);

    composed.setDevicePixelRatio(devicePixelRatio);
    return composed;
#else
    Q_UNUSED(adjustedMs);
    Q_UNUSED(frameWidth);
    Q_UNUSED(frameHeight);
    Q_UNUSED(devicePixelRatio);
    return {};
#endif
}
