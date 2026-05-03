
#include "network_controller.h"

#include "../../service/config_service.h"
#include "../../service/playback_service_interfaces.h"
#include "../../view/network_view_qt.h"
#include "../stats/playback_stats_controller.h"

#include <algorithm>
#include <utility>

NetworkController::NetworkController(std::unique_ptr<INetworkView> viewValue,
    IPlaybackSessionService* sessionService,
    ConfigService* configService,
    PlaybackStatsController* statsController)
    : view(std::move(viewValue))
    , session(sessionService)
    , config(configService)
    , stats(statsController)
{
    if (view)
    {
        view->SetTogglePanelHandler([this]() { TogglePanel(); });
        view->SetToggleStatsHandler([this]() { ToggleStatsPanel(); });
        view->SetApplyHandler([this]() {
            if (applyHandler)
                applyHandler();
        });
    }
}

void NetworkController::SetApplyHandler(std::function<void()> handler)
{
    applyHandler = std::move(handler);
    if (view)
    {
        view->SetApplyHandler([this]() {
            if (applyHandler)
                applyHandler();
        });
    }
}

void NetworkController::SetStatsController(PlaybackStatsController* statsController)
{
    stats = statsController;
}

void NetworkController::SetLayoutChangedHandler(std::function<void()> handler)
{
    layoutChangedHandler = std::move(handler);
}

QPushButton* NetworkController::ToggleButton() const
{
    return view ? view->ToggleButton() : nullptr;
}

QPushButton* NetworkController::StatsButton() const
{
    return view ? view->StatsButton() : nullptr;
}

QWidget* NetworkController::Panel() const
{
    return view ? view->Panel() : nullptr;
}

QWidget* NetworkController::StatsPanel() const
{
    return view ? view->StatsPanel() : nullptr;
}

bool NetworkController::IsStatsPanelVisible() const
{
    return view && view->IsStatsPanelVisible();
}

void NetworkController::NotifyLayoutChanged() const
{
    if (layoutChangedHandler)
        layoutChangedHandler();
}

QList<QWidget*> NetworkController::AutoHideWidgets() const
{
    return view ? view->AutoHideWidgets() : QList<QWidget*>{};
}

void NetworkController::InstallEventFilters(QObject* filter) const
{
    if (view)
        view->InstallEventFilters(filter);
}

void NetworkController::TogglePanel()
{
    if (!view)
        return;

    const bool show = !view->IsPanelVisible();
    view->SetPanelVisible(show);
    view->SetToggleButtonChecked(show);
    if (show)
    {
        if (session)
            SyncUiFromOptions(session->GetNetworkOpenOptions());
        RefreshEffectivePolicySummary();
        view->RaisePanel();
        NotifyLayoutChanged();
    }
}

void NetworkController::ToggleStatsPanel()
{
    if (!view)
        return;

    const bool show = !view->IsStatsPanelVisible();
    view->SetStatsPanelVisible(show);
    view->SetStatsButtonChecked(show);
    if (show)
    {
        RefreshStatsPanel();
        view->RaiseStatsPanel();
        NotifyLayoutChanged();
    }
}

void NetworkController::LayoutPanels(int videoWidth, int hostHeight)
{
    if (view)
        view->LayoutPanels(videoWidth, hostHeight);
}

void NetworkController::SaveSettings() const
{
    const StreamOpenOptions options = BuildOpenOptionsFromUi();
    if (config)
        config->SaveNetworkOpenOptions(options);
}

void NetworkController::LoadSettings()
{
    StreamOpenOptions options = config ? config->LoadNetworkOpenOptions() : StreamOpenOptions::DefaultNetwork();
    const StreamOpenOptions balancedDefaults = StreamOpenOptions::DefaultNetwork();
    const StreamOpenOptions lowLatencyDefaults = StreamOpenOptions::LowLatencyNetwork();
    options.audioDeviceBufferMs = options.enableLowLatency
        ? lowLatencyDefaults.audioDeviceBufferMs
        : balancedDefaults.audioDeviceBufferMs;

    SyncUiFromOptions(options);
    if (session)
        session->SetNetworkOpenOptions(options);
    RefreshEffectivePolicySummary();
}

StreamOpenOptions NetworkController::BuildOpenOptionsFromUi() const
{
    StreamOpenOptions options = StreamOpenOptions::DefaultNetwork();
    const StreamOpenOptions balancedDefaults = StreamOpenOptions::DefaultNetwork();
    const StreamOpenOptions lowLatencyDefaults = StreamOpenOptions::LowLatencyNetwork();
    if (!view)
        return options;

    const NetworkViewData data = view->ReadData();
    options.enableLowLatency = data.enableLowLatency;
    options.forceTcpForRtsp = data.forceTcpForRtsp;
    options.noBuffer = data.noBuffer;
    options.lowDelayFlag = data.lowDelayFlag;
    options.connectTimeoutMs = data.connectTimeoutMs;
    options.maxDelayUs = data.maxDelayUs;
    options.bufferSizeBytes = data.bufferSizeKb * 1024;
    options.probeSizeBytes = data.probeSizeKb * 1024;
    options.analyzeDurationUs = data.analyzeDurationMs * 1000;
    options.videoQueuePackets = data.videoQueuePackets;
    options.audioQueuePackets = data.audioQueuePackets;
    options.liveClockPolicy = data.liveClockPolicy;
    options.reorderQueueSize = options.enableLowLatency ? 0 : -1;
    if (!options.enableLowLatency)
    {
        options.videoQueuePackets = std::max(options.videoQueuePackets, balancedDefaults.videoQueuePackets);
        options.audioQueuePackets = std::max(options.audioQueuePackets, balancedDefaults.audioQueuePackets);
    }
    options.audioDeviceBufferMs = options.enableLowLatency
        ? lowLatencyDefaults.audioDeviceBufferMs
        : balancedDefaults.audioDeviceBufferMs;
    options.videoLeadMs = options.enableLowLatency ? 80 : balancedDefaults.videoLeadMs;
    options.lateVideoDropMs = data.lateDropMs;
    options.reconnect.enabled = data.reconnectEnabled;
    options.reconnect.maxAttempts = data.reconnectAttempts;
    options.reconnect.baseDelayMs = 1000;
    options.reconnect.maxDelayMs = 16000;
    if (!options.enableLowLatency && options.liveClockPolicy == LiveClockPolicy::AudioMaster)
        options.lateVideoDropMs = 0;
    return options;
}

void NetworkController::SyncUiFromOptions(const StreamOpenOptions& options)
{
    if (!view)
        return;

    NetworkViewData data;
    data.forceTcpForRtsp = options.forceTcpForRtsp;
    data.liveClockPolicy = options.liveClockPolicy;
    data.enableLowLatency = options.enableLowLatency;
    data.noBuffer = options.noBuffer;
    data.lowDelayFlag = options.lowDelayFlag;
    data.reconnectEnabled = options.reconnect.enabled;
    data.connectTimeoutMs = options.connectTimeoutMs;
    data.maxDelayUs = options.maxDelayUs;
    data.bufferSizeKb = options.bufferSizeBytes / 1024;
    data.probeSizeKb = options.probeSizeBytes / 1024;
    data.analyzeDurationMs = options.analyzeDurationUs / 1000;
    data.videoQueuePackets = options.videoQueuePackets;
    data.audioQueuePackets = options.audioQueuePackets;
    data.lateDropMs = options.lateVideoDropMs;
    data.reconnectAttempts = options.reconnect.maxAttempts;
    view->WriteData(data);
    RefreshEffectivePolicySummary();
}

void NetworkController::RefreshEffectivePolicySummary()
{
    if (!view || !stats)
        return;

    const StreamOpenOptions configuredOptions = BuildOpenOptionsFromUi();
    view->SetEffectivePolicyText(stats->BuildEffectivePolicySummary(configuredOptions));
}

void NetworkController::CancelPendingOpen()
{
    ++networkOpenRequestId;
    networkOpenPending = false;
    pendingNetworkUrl.clear();
    pendingNetworkOptions = StreamOpenOptions::DefaultNetwork();
}

quint64 NetworkController::BeginPendingOpen(const QString& url, const StreamOpenOptions& options)
{
    ++networkOpenRequestId;
    networkOpenPending = true;
    pendingNetworkUrl = url;
    pendingNetworkOptions = options;
    return networkOpenRequestId;
}

bool NetworkController::MatchesPendingRequest(quint64 requestId) const
{
    return requestId == networkOpenRequestId;
}

void NetworkController::CompletePendingOpen()
{
    networkOpenPending = false;
    pendingNetworkUrl.clear();
    pendingNetworkOptions = StreamOpenOptions::DefaultNetwork();
}

void NetworkController::RefreshStatsPanel()
{
    if (!stats)
        return;
    stats->RefreshNetworkStats(networkOpenPending, pendingNetworkUrl, pendingNetworkOptions);
}

bool NetworkController::ConsumeStatusGeneration(quint64 generation)
{
    return stats ? stats->ConsumeStatusGeneration(generation) : false;
}

void NetworkController::TickUi()
{
    if (view && view->IsPanelVisible())
        RefreshEffectivePolicySummary();
    if (IsStatsPanelVisible())
        RefreshStatsPanel();
}
