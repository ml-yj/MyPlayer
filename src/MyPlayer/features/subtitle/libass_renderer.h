#pragma once

#include "subtitle_types.h"

#include <QByteArray>
#include <QImage>
#include <QSize>
#include <limits>
#include <memory>

class QPainter;

class LibassRuntimeContext
{
public:
    LibassRuntimeContext();
    ~LibassRuntimeContext();

    bool IsAvailable() const;
    void ClearTrack();
    bool LoadScript(const QByteArray& scriptUtf8, int frameWidth, int frameHeight);
    QImage RenderFrame(long long adjustedMs, int frameWidth, int frameHeight,
        qreal devicePixelRatio, bool* changed);

private:
    class Private;
    std::unique_ptr<Private> d_;
};

class AssRenderCache
{
public:
    void Invalidate();
    void Render(long long adjustedMs, bool useLibass, LibassRuntimeContext& runtimeContext,
        int frameWidth, int frameHeight, qreal devicePixelRatio);
    const QImage& Image() const;

private:
    long long cachedTimeMs_ = std::numeric_limits<long long>::min();
    QImage cachedImage_;
};

class LibassRenderer
{
public:
    LibassRenderer();
    ~LibassRenderer();

    void clearTrack();
    void setTrack(const SubtitleTrack* track);
    void setStyle(int fontPointSize, int bottomMarginPx);
    void setFrame(const QSize& logicalSize, qreal devicePixelRatio);
    void draw(QPainter& painter, long long clockMs, long long offsetMs);
    bool isAvailable() const;

private:
    void InvalidateCache();
    void RebuildTrackIfNeeded();

    SubtitleTrack trackData;
    QSize logicalSize;
    qreal devicePixelRatio = 1.0;
    int frameWidth = 0;
    int frameHeight = 0;
    int fontPointSize = 24;
    int bottomMarginPx = 120;
    bool trackDirty = true;
    LibassRuntimeContext runtimeContext_;
    AssRenderCache renderCache_;
};
