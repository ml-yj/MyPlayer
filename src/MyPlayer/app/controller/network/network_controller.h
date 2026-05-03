#pragma once

#include <QList>
#include <QString>
#include <functional>
#include <memory>

#include "../../../core/session/stream_config.h"

class QObject;
class QWidget;
class QPushButton;
class INetworkView;
class IPlaybackSessionService;
class PlaybackStatsController;
class ConfigService;

class NetworkController
{
public:
    NetworkController(
        std::unique_ptr<INetworkView> view,
        IPlaybackSessionService* sessionService,
        ConfigService* configService,
        PlaybackStatsController* statsController);
    void SetStatsController(PlaybackStatsController* statsController);

    void SetApplyHandler(std::function<void()> handler);
    void SetLayoutChangedHandler(std::function<void()> handler);

    QPushButton* ToggleButton() const;
    QPushButton* StatsButton() const;
    QWidget* Panel() const;
    QWidget* StatsPanel() const;
    bool IsStatsPanelVisible() const;
    QList<QWidget*> AutoHideWidgets() const;
    void InstallEventFilters(QObject* filter) const;

    void SaveSettings() const;
    void LoadSettings();
    StreamOpenOptions BuildOpenOptionsFromUi() const;
    void SyncUiFromOptions(const StreamOpenOptions& options);
    void RefreshStatsPanel();
    void TickUi();
    void TogglePanel();
    void ToggleStatsPanel();
    void LayoutPanels(int videoWidth, int hostHeight);

    void CancelPendingOpen();
    quint64 BeginPendingOpen(const QString& url, const StreamOpenOptions& options);
    bool MatchesPendingRequest(quint64 requestId) const;
    void CompletePendingOpen();
    bool ConsumeStatusGeneration(quint64 generation);

private:
    void RefreshEffectivePolicySummary();
    void NotifyLayoutChanged() const;

    std::unique_ptr<INetworkView> view;
    IPlaybackSessionService* session = nullptr;
    ConfigService* config = nullptr;
    PlaybackStatsController* stats = nullptr;
    std::function<void()> applyHandler;
    std::function<void()> layoutChangedHandler;

    quint64 networkOpenRequestId = 0;
    bool networkOpenPending = false;
    QString pendingNetworkUrl;
    StreamOpenOptions pendingNetworkOptions = StreamOpenOptions::DefaultNetwork();
};
