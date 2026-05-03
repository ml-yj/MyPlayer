#pragma once

#include <QString>

class IStatsView
{
public:

    virtual ~IStatsView() = default;

    virtual void SetNetworkStatsText(const QString& text) = 0;

    virtual bool IsDebugOsdVisible() const = 0;

    virtual void SetDebugOsdVisible(bool visible) = 0;

    virtual void SetDebugOsdText(const QString& text) = 0;

    virtual bool IsAnime4KEnabled() const = 0;

    virtual QString RenderBackendSummary() const = 0;

    virtual QString Anime4KBackendSummary() const = 0;

    virtual void GetAnime4KOutputSize(int& outW, int& outH) const = 0;
};

#include "../../ui/video/video_widget.h"

#include <QLabel>

class StatsViewQt final : public IStatsView
{
public:
    StatsViewQt(QLabel* networkStatsLabel, VideoWidget* videoWidget)
        : networkStatsLabel_(networkStatsLabel)
        , videoWidget_(videoWidget)
    {
    }

    void SetNetworkStatsText(const QString& text) override
    {
        if (networkStatsLabel_)
            networkStatsLabel_->setText(text);
    }

    bool IsDebugOsdVisible() const override
    {
        return videoWidget_ && videoWidget_->isDebugOverlayVisible();
    }

    void SetDebugOsdVisible(bool visible) override
    {
        if (videoWidget_)
            videoWidget_->setDebugOverlayVisible(visible);
    }

    void SetDebugOsdText(const QString& text) override
    {
        if (videoWidget_)
            videoWidget_->setDebugOverlayText(text);
    }

    bool IsAnime4KEnabled() const override
    {
        return videoWidget_ && videoWidget_->isAnime4KEnabled();
    }

    QString RenderBackendSummary() const override
    {
        return videoWidget_ ? videoWidget_->GetRenderBackendSummary() : QString("Unknown");
    }

    QString Anime4KBackendSummary() const override
    {
        return videoWidget_ ? videoWidget_->GetAnime4KBackendSummary() : QString("OFF");
    }

    void GetAnime4KOutputSize(int& outW, int& outH) const override
    {
        outW = 0;
        outH = 0;
        if (videoWidget_)
            videoWidget_->getAnime4KOutputSize(outW, outH);
    }

private:
    QLabel* networkStatsLabel_ = nullptr;
    VideoWidget* videoWidget_ = nullptr;
};
