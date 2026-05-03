
#include "playback_stats_controller.h"

#include "../../view/stats_view_qt.h"
#include "../../service/playback_service_interfaces.h"
#include "../../../core/session/stream_config.h"
#include "../../../common/util.h"
#include <algorithm>
#include <utility>
namespace
{
QString ModeText(const StreamOpenOptions& options)
{
    return options.enableLowLatency ? QString("low-latency") : QString("balanced");
}

QString ClockText(LiveClockPolicy policy)
{
    return policy == LiveClockPolicy::AudioMasterDropLateVideo
        ? QString("audio+drop-late")
        : QString("audio-master");
}

QString RtspTransportText(const StreamOpenOptions& options)
{
    return options.forceTcpForRtsp ? QString("TCP") : QString("UDP");
}

QString StrategyFamilyText(StreamPlaybackKind playbackKind, StreamSourceType sourceType)
{
    if (UsesContinuousLiveBufferStrategy(sourceType))
        return QString("continuous-live");
    if (sourceType == StreamSourceType::Hls)
        return QString("segmented-live");
    if (sourceType == StreamSourceType::HttpProgressive)
        return QString("http-progressive");
    if (sourceType == StreamSourceType::LocalFile)
        return QString("local-file");
    return QString::fromLatin1(StreamPlaybackKindName(playbackKind));
}
}

PlaybackStatsController::PlaybackStatsController(
    IPlaybackSessionService* sessionService,
    IPlaybackStatsService* statsFacade,
    std::unique_ptr<IStatsView> viewValue)
    : session(sessionService)
    , statsService(statsFacade)
    , view(std::move(viewValue))
{
}

bool PlaybackStatsController::ConsumeStatusGeneration(quint64 generation)
{
    if (generation == 0 || generation == lastStatusEventGeneration)
        return false;

    lastStatusEventGeneration = generation;
    return true;
}

QString PlaybackStatsController::StatusEventText()
{
    if (!session)
        return QString();
    return QString::fromStdString(session->GetSessionSnapshot().statusEventText);
}

QString PlaybackStatsController::TakeStatusEventText()
{
    if (!session)
        return {};

    const PlaybackSessionSnapshot sessionSnapshot = session->GetSessionSnapshot();
    if (!ConsumeStatusGeneration(sessionSnapshot.statusEventGeneration))
        return {};

    return QString::fromStdString(sessionSnapshot.statusEventText);
}

void PlaybackStatsController::RefreshNetworkStats(
    bool networkOpenPending,
    const QString& pendingNetworkUrl,
    const StreamOpenOptions& pendingNetworkOptions) const
{
    if (!view || !session || !statsService)
        return;

    const PlaybackSessionSnapshot sessionSnapshot = session->GetSessionSnapshot();
    const PlaybackMediaSnapshot mediaSnapshot = statsService->GetMediaSnapshot();
    const StreamStatsSnapshot stats = statsService->GetStreamStats();
    const StreamOpenOptions activeOptions = session->GetActiveOpenOptions();
    const StreamOpenOptions savedOptions = session->GetNetworkOpenOptions();
    const std::string currentUrl = sessionSnapshot.currentUrl;

    if (networkOpenPending)
    {
        view->SetNetworkStatsText(QString(
            "State: Opening\n"
            "Mode: %1\n"
            "RTSP: %2\n"
            "Clock: %3\n"
            "Queues: V%4 / A%5\n"
            "Probe: %6 KB | Analyze: %7 ms\n"
            "URL: %8")
            .arg(pendingNetworkOptions.enableLowLatency ? "low-latency" : "balanced")
            .arg(pendingNetworkOptions.forceTcpForRtsp ? "TCP" : "UDP")
            .arg(pendingNetworkOptions.liveClockPolicy == LiveClockPolicy::AudioMasterDropLateVideo
                ? "audio + drop late"
                : "audio master")
            .arg(pendingNetworkOptions.videoQueuePackets)
            .arg(pendingNetworkOptions.audioQueuePackets)
            .arg(pendingNetworkOptions.probeSizeBytes / 1024)
            .arg(pendingNetworkOptions.analyzeDurationUs / 1000)
            .arg(pendingNetworkUrl.left(96)));
        return;
    }

    if (!stats.isNetwork)
    {
        view->SetNetworkStatsText(QString(
            "Current: no network stream\n"
            "Next open: %1\n"
            "RTSP: %2\n"
            "Clock: %3\n"
            "Queues: V%4 / A%5\n"
            "Audio out: %6 ms\n"
            "Timeout: %7 ms\n"
            "Probe: %8 KB | Analyze: %9 ms")
            .arg(savedOptions.enableLowLatency ? "low-latency" : "balanced")
            .arg(savedOptions.forceTcpForRtsp ? "TCP" : "UDP")
            .arg(savedOptions.liveClockPolicy == LiveClockPolicy::AudioMasterDropLateVideo
                ? "audio + drop late"
                : "audio master")
            .arg(savedOptions.videoQueuePackets)
            .arg(savedOptions.audioQueuePackets)
            .arg(savedOptions.audioDeviceBufferMs)
            .arg(savedOptions.connectTimeoutMs)
            .arg(savedOptions.probeSizeBytes / 1024)
            .arg(savedOptions.analyzeDurationUs / 1000));
        return;
    }

    const double streamFps = mediaSnapshot.videoFpsDen > 0
        ? static_cast<double>(mediaSnapshot.videoFpsNum) / static_cast<double>(mediaSnapshot.videoFpsDen)
        : 0.0;
    const QString urlText = QString::fromStdString(currentUrl).left(96);
    const QString adaptiveMode = QString::fromStdString(stats.adaptiveMode.empty() ? "manual" : stats.adaptiveMode);
    const QString adaptiveHint = QString::fromStdString(stats.adaptiveHint.empty() ? "-" : stats.adaptiveHint);
    const QString tuneProfile = QString::fromStdString(stats.liveTuneProfile.empty() ? "off" : stats.liveTuneProfile);
    const QString tuneHint = QString::fromStdString(stats.liveTuneHint.empty() ? "-" : stats.liveTuneHint);
    const QString detSkipText = stats.runtimeDetectorBaseSkipFrames > 0
        ? QString("%1 (base %2)").arg(stats.runtimeDetectorSkipFrames).arg(stats.runtimeDetectorBaseSkipFrames)
        : "-";
    view->SetNetworkStatsText(QString(
        "State: %1\n"
        "Mode: %2\n"
        "Open: %3 ms\n"
        "Reconnect: %4/%5 | ok %6\n"
        "Buffering: %7 | read miss %8\n"
        "Queues: V%9/%10 | A%11/%12 | audio out %13 ms\n"
        "Drops: v-q %14 | v-late %15 | a-q %16 | catch-up %17 | a-throttle %18\n"
        "Recovery: %19\n"
        "Hint: %20\n"
        "Tune: %21 | audio %22 ms | vlead %23 ms | late %24 ms\n"
        "Buffer ctl: %25 | start V%26/A%27/%28ms | resume V%29/A%30/%31ms\n"
        "Low water: V%32/A%33/%34ms | profile %35 | ready %36/%37 | hold %38 ms | cool %39 ms | streak %40\n"
        "Buffer dbg: resume %41 | suppress %42\n"
        "AI sched: GPU q %43/a %44 | CPU q %45/a %46 | done %47 | drop %48 | cancel %49 | det-drop %50 | det-cancel %51 | wait %52/%53 ms\n"
        "Tune hint: %54\n"
        "DET skip: %55\n"
        "Diag: log %56 | warn %57 | err %58 | qt %59/%60 | dump %61 | write-fail %62\n"
        "Transport: %63 | Clock: %64\n"
        "Resolution: %65x%66 | FPS %67\n"
        "Bitrate: %68 kbps\n"
        "URL: %69")
        .arg(QString::fromLatin1(StreamSessionStateName(stats.state)))
        .arg(stats.lowLatencyEnabled ? "low-latency" : "balanced")
        .arg(stats.openLatencyMs)
        .arg(stats.reconnectAttempts)
        .arg(activeOptions.reconnect.maxAttempts)
        .arg(stats.reconnectSuccesses)
        .arg(stats.bufferingEvents)
        .arg(stats.consecutiveReadFailures)
        .arg(stats.videoQueuePackets)
        .arg(stats.videoQueueLimit)
        .arg(stats.audioQueuePackets)
        .arg(stats.audioQueueLimit)
        .arg(stats.audioDeviceBufferedMs)
        .arg(stats.droppedVideoPackets)
        .arg(stats.droppedLateVideoFrames)
        .arg(stats.droppedAudioPackets)
        .arg(stats.audioCatchUpEvents)
        .arg(stats.audioThrottleEvents)
        .arg(adaptiveMode)
        .arg(adaptiveHint)
        .arg(tuneProfile)
        .arg(stats.runtimeAudioTargetMs)
        .arg(stats.runtimeVideoLeadMs)
        .arg(stats.runtimeLateDropMs)
        .arg(stats.primingPending ? "priming" : "ready")
        .arg(stats.runtimeStartupVideoPackets)
        .arg(stats.runtimeStartupAudioPackets)
        .arg(stats.runtimeStartupAudioBufferedMs)
        .arg(stats.runtimeResumeVideoPackets)
        .arg(stats.runtimeResumeAudioPackets)
        .arg(stats.runtimeResumeAudioBufferedMs)
        .arg(stats.runtimeLowWaterVideoPackets)
        .arg(stats.runtimeLowWaterAudioPackets)
        .arg(stats.runtimeLowWaterAudioBufferedMs)
        .arg(stats.runtimeBufferProfileLevel)
        .arg(stats.runtimeReadyStreak)
        .arg(stats.runtimeReadyTarget)
        .arg(stats.runtimeMinBufferHoldMs)
        .arg(stats.runtimeRebufferCooldownMs)
        .arg(stats.runtimeLowWaterStreak)
        .arg(stats.runtimePlaybackResumeCount)
        .arg(stats.runtimeRebufferSuppressedCount)
        .arg(stats.aiGpuQueueDepth)
        .arg(stats.aiGpuActiveTasks)
        .arg(stats.aiCpuQueueDepth)
        .arg(stats.aiCpuActiveTasks)
        .arg(stats.aiCompletedTasks)
        .arg(stats.aiDroppedTasks)
        .arg(stats.aiCancelledTasks)
        .arg(stats.aiDetectorDroppedTasks)
        .arg(stats.aiDetectorCancelledTasks)
        .arg(stats.aiAverageWaitMs)
        .arg(stats.aiLastWaitMs)
        .arg(tuneHint)
        .arg(detSkipText)
        .arg(stats.diagnosticsLogLines)
        .arg(stats.diagnosticsWarningLines)
        .arg(stats.diagnosticsErrorLines)
        .arg(stats.diagnosticsQtWarnings)
        .arg(stats.diagnosticsQtCriticals)
        .arg(stats.diagnosticsCrashDumpsWritten)
        .arg(stats.diagnosticsFileWriteFailures)
        .arg(activeOptions.forceTcpForRtsp ? "TCP" : "UDP")
        .arg(activeOptions.liveClockPolicy == LiveClockPolicy::AudioMasterDropLateVideo
            ? "audio + drop late"
            : "audio master")
        .arg(mediaSnapshot.videoWidth)
        .arg(mediaSnapshot.videoHeight)
        .arg(streamFps, 0, 'f', 2)
        .arg(mediaSnapshot.bitrate / 1000.0, 0, 'f', 0)
        .arg(urlText));
}

QString PlaybackStatsController::BuildEffectivePolicySummary(const StreamOpenOptions& configuredOptions) const
{
    const QString configuredLine = QString(
        "Configured next open: %1 | RTSP %2 | %3")
        .arg(ModeText(configuredOptions))
        .arg(RtspTransportText(configuredOptions))
        .arg(ClockText(configuredOptions.liveClockPolicy));

    if (!session || !statsService)
    {
        return configuredLine
            + "\nActive: unavailable"
            + "\nRule: source policy and live tune may still adjust runtime targets";
    }

    const PlaybackSessionSnapshot sessionSnapshot = session->GetSessionSnapshot();
    const StreamStatsSnapshot stats = statsService->GetStreamStats();
    const bool activeNetwork = stats.isNetwork || IsNetworkUrl(sessionSnapshot.currentUrl);
    if (!activeNetwork)
    {
        return configuredLine
            + "\nActive: no network stream"
            + "\nRule: source policy is resolved when a network stream actually opens";
    }

    const StreamOpenOptions activeOptions = session->GetActiveOpenOptions();
    const StreamSourcePolicy sourcePolicy =
        ResolveStreamSourcePolicy(stats.playbackKind, stats.sourceType, activeOptions);

    QString activeLine = QString("Active: %1 %2 | %3 | %4")
        .arg(QString::fromLatin1(StreamSourceTypeName(stats.sourceType)))
        .arg(QString::fromLatin1(StreamPlaybackKindName(stats.playbackKind)))
        .arg(ModeText(activeOptions))
        .arg(ClockText(activeOptions.liveClockPolicy));
    if (stats.sourceType == StreamSourceType::Rtsp)
        activeLine += QString(" | RTSP %1").arg(RtspTransportText(activeOptions));

    const QString familyLine = QString("Family: %1 | gate %2 ms | grace %3 ms")
        .arg(StrategyFamilyText(stats.playbackKind, stats.sourceType))
        .arg(sourcePolicy.firstVideoGateTimeoutMs)
        .arg(sourcePolicy.liveVideoProgressGraceMs);

    if (stats.playbackKind == StreamPlaybackKind::Live)
    {
        const int audioTargetMs = stats.runtimeAudioTargetMs > 0
            ? stats.runtimeAudioTargetMs
            : activeOptions.audioDeviceBufferMs;
        const int videoLeadMs = stats.runtimeVideoLeadMs > 0
            ? stats.runtimeVideoLeadMs
            : activeOptions.videoLeadMs;
        const int lateDropMs = stats.runtimeLateDropMs > 0
            ? stats.runtimeLateDropMs
            : activeOptions.lateVideoDropMs;

        return configuredLine
            + "\n" + activeLine
            + "\n" + familyLine
            + QString("\nRuntime: audio %1 ms | vlead %2 ms | late %3 ms")
                .arg(audioTargetMs)
                .arg(videoLeadMs)
                .arg(lateDropMs)
            + QString("\nGate: start V%1/A%2/%3ms | resume V%4/A%5/%6ms")
                .arg(std::max(0, stats.runtimeStartupVideoPackets))
                .arg(std::max(0, stats.runtimeStartupAudioPackets))
                .arg(std::max(0, stats.runtimeStartupAudioBufferedMs))
                .arg(std::max(0, stats.runtimeResumeVideoPackets))
                .arg(std::max(0, stats.runtimeResumeAudioPackets))
                .arg(std::max(0, stats.runtimeResumeAudioBufferedMs))
            + "\nRule: source policy sets the base lane; live tune can still move runtime targets";
    }

    return configuredLine
        + "\n" + activeLine
        + "\n" + familyLine
        + QString("\nOpen: probe %1 KB | analyze %2 ms | queues V%3/A%4")
            .arg(activeOptions.probeSizeBytes / 1024)
            .arg(activeOptions.analyzeDurationUs / 1000)
            .arg(activeOptions.videoQueuePackets)
            .arg(activeOptions.audioQueuePackets)
        + "\nRule: active values come from open options first, then source policy clamps";
}

void PlaybackStatsController::ToggleDebugOsd()
{
    if (!view)
        return;

    const bool visible = !view->IsDebugOsdVisible();
    view->SetDebugOsdVisible(visible);
    if (visible)
        ResetDebugOsd();
}

void PlaybackStatsController::ResetDebugOsd()
{
    osdTickCount = 0;
    osdFpsDisplay = 0;
    osdTotalRendered = 0;
    if (statsService)
        statsService->FetchRenderedFrames();
}

void PlaybackStatsController::TickDebugOsd()
{
    if (!view || !view->IsDebugOsdVisible() || !session || !statsService)
        return;

    ++osdTickCount;
    if (osdTickCount >= 25)
    {
        const int frames = statsService->FetchRenderedFrames();
        this->osdFpsDisplay = frames;
        this->osdTotalRendered += frames;
        osdTickCount = 0;
    }

    const PlaybackMediaSnapshot mediaSnapshot = statsService->GetMediaSnapshot();
    const PlaybackSessionSnapshot sessionSnapshot = session->GetSessionSnapshot();
    const int w = mediaSnapshot.videoWidth;
    const int h = mediaSnapshot.videoHeight;
    const int fpsNum = mediaSnapshot.videoFpsNum;
    const int fpsDen = mediaSnapshot.videoFpsDen;
    const long long br = mediaSnapshot.bitrate;

    const double streamFps = (fpsDen > 0) ? static_cast<double>(fpsNum) / fpsDen : 0.0;
    const double brKbps = br / 1000.0;

    int dropped = 0;
    const long long curPts = sessionSnapshot.positionMs;
    if (streamFps > 0.0 && curPts > 0)
    {
        const int expected = static_cast<int>(curPts / 1000.0 * streamFps);
        dropped = expected - osdTotalRendered;
        if (dropped < 0)
            dropped = 0;
    }

    QString resStr = QString("%1x%2").arg(w).arg(h);
    if (view->IsAnime4KEnabled())
    {
        int a4kW = 0;
        int a4kH = 0;
        view->GetAnime4KOutputSize(a4kW, a4kH);
        resStr += QString(" -> %1x%2 (A4K)").arg(a4kW).arg(a4kH);
    }

    QString info = QString(
        "Resolution: %1\n"
        "Stream FPS: %2\n"
        "Real FPS:   %3\n"
        "Bitrate:    %4 kbps\n"
        "Dropped:    %5")
        .arg(resStr)
        .arg(streamFps, 0, 'f', 2)
        .arg(osdFpsDisplay)
        .arg(brKbps, 0, 'f', 0)
        .arg(dropped);

    if (view)
    {
        info += QString(
            "\nRender:     %1\n"
            "A4K:        %2")
            .arg(view->RenderBackendSummary())
            .arg(view->Anime4KBackendSummary());
    }

    const std::string detail = statsService->GetOsdDetail();
    if (!detail.empty())
        info += "\n" + QString::fromStdString(detail);

    view->SetDebugOsdText(info);
}
