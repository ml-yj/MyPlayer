
#pragma once

#include <QString>
#include <memory>

class IStatsView;
class IPlaybackSessionService;
class IPlaybackStatsService;
struct StreamOpenOptions;

class PlaybackStatsController
{
public:

    PlaybackStatsController(
        IPlaybackSessionService* sessionService,
        IPlaybackStatsService* statsService,
        std::unique_ptr<IStatsView> view);

    bool ConsumeStatusGeneration(quint64 generation);

    QString StatusEventText();

    QString TakeStatusEventText();

    void RefreshNetworkStats(
        bool networkOpenPending,
        const QString& pendingNetworkUrl,
        const StreamOpenOptions& pendingNetworkOptions) const;
    QString BuildEffectivePolicySummary(const StreamOpenOptions& configuredOptions) const;

    void ToggleDebugOsd();
    void ResetDebugOsd();

    void TickDebugOsd();

private:

    IPlaybackSessionService* session = nullptr;
    IPlaybackStatsService* statsService = nullptr;

    std::unique_ptr<IStatsView> view;

    quint64 lastStatusEventGeneration = 0;

    int osdTickCount = 0;
    int osdFpsDisplay = 0;
    int osdTotalRendered = 0;
};
