#include "stream_session_core.h"

#include "../audio/audio_thread.h"
#include "../media/decode.h"
#include "../media/demux.h"
#include "../recording/session_recorder.h"
#include "../../common/diagnostics/logger.h"
#include "../../features/detector/detector_thread.h"
#include "../video/video_thread.h"
#include "../../features/asr/whisper_thread.h"

#include <QThread>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/packet.h>
}

#include <chrono>
#include <memory>
#include <algorithm>

namespace
{
struct SessionRuntimeProfile
{
    StreamPlaybackKind playbackKind = StreamPlaybackKind::File;
    StreamSourceType sourceType = StreamSourceType::LocalFile;
    StreamSourcePolicy sourcePolicy;
    int videoQueueLimit = 0;
    int audioQueueLimit = 0;
    QueueOverflowPolicy videoOverflowPolicy = QueueOverflowPolicy::BlockProducer;
    QueueOverflowPolicy audioOverflowPolicy = QueueOverflowPolicy::BlockProducer;
    int audioOutputBufferMs = 0;
    bool dropLateVideo = false;
    int videoLeadMs = 0;
    int lateVideoDropMs = 0;
};

int ComputeDefaultVideoQueueLimit(int videoWidth, int videoHeight)
{
    const int pixels = videoWidth * videoHeight;
    if (pixels >= 3840 * 2160)
        return 15;
    if (pixels >= 1920 * 1080)
        return 30;
    return 50;
}

SessionRuntimeProfile BuildSessionRuntimeProfile(
    const std::string& currentUrl,
    bool isNetworkStream,
    bool isLiveStream,
    int videoWidth,
    int videoHeight,
    const StreamOpenOptions& selectedOptions)
{
    SessionRuntimeProfile profile;
    const int defaultVideoQueue = ComputeDefaultVideoQueueLimit(videoWidth, videoHeight);
    const int requestedVideoQueue = std::max(1, selectedOptions.videoQueuePackets);
    const int requestedAudioQueue = std::max(1, selectedOptions.audioQueuePackets);

    profile.playbackKind = ResolveStreamPlaybackKind(isNetworkStream, isLiveStream);
    profile.sourceType = ResolveStreamSourceType(currentUrl);
    profile.sourcePolicy =
        ResolveStreamSourcePolicy(profile.playbackKind, profile.sourceType, selectedOptions);

    const bool lowLatencyLive =
        profile.playbackKind == StreamPlaybackKind::Live && selectedOptions.enableLowLatency;
    const bool balancedAudioMasterLive =
        profile.playbackKind == StreamPlaybackKind::Live
        && profile.sourcePolicy.forceBaseTuneInBalancedAudioMaster
        && !lowLatencyLive;

    switch (profile.playbackKind)
    {
    case StreamPlaybackKind::File:
        profile.videoQueueLimit = std::max(defaultVideoQueue, requestedVideoQueue);
        profile.audioQueueLimit = std::max(50, requestedAudioQueue);
        profile.videoOverflowPolicy = QueueOverflowPolicy::BlockProducer;
        profile.audioOverflowPolicy = QueueOverflowPolicy::BlockProducer;
        profile.videoLeadMs = std::max(200, selectedOptions.videoLeadMs);
        profile.lateVideoDropMs = 0;
        break;
    case StreamPlaybackKind::NetworkVod:
        profile.videoQueueLimit = std::clamp(requestedVideoQueue, 12, defaultVideoQueue);
        profile.audioQueueLimit = std::clamp(requestedAudioQueue, 16, 50);
        profile.videoOverflowPolicy = QueueOverflowPolicy::BlockProducer;
        profile.audioOverflowPolicy = QueueOverflowPolicy::BlockProducer;
        profile.videoLeadMs = std::max(160, selectedOptions.videoLeadMs);
        profile.lateVideoDropMs = 0;
        break;
    case StreamPlaybackKind::Live:
        profile.videoQueueLimit = lowLatencyLive
            ? requestedVideoQueue
            : (balancedAudioMasterLive
                ? std::clamp(requestedVideoQueue, 16, defaultVideoQueue)
                : std::clamp(requestedVideoQueue, 12, defaultVideoQueue));
        profile.audioQueueLimit = lowLatencyLive
            ? requestedAudioQueue
            : (balancedAudioMasterLive
                ? std::clamp(requestedAudioQueue, 10, 16)
                : std::clamp(requestedAudioQueue, 8, 12));
        profile.videoOverflowPolicy = lowLatencyLive
            ? QueueOverflowPolicy::DropOldest
            : QueueOverflowPolicy::BlockProducer;
        profile.audioOverflowPolicy = balancedAudioMasterLive
            ? QueueOverflowPolicy::BlockProducer
            : QueueOverflowPolicy::DropOldest;
        profile.audioOutputBufferMs = selectedOptions.audioDeviceBufferMs > 0
            ? selectedOptions.audioDeviceBufferMs
            : (balancedAudioMasterLive ? StreamOpenOptions::DefaultNetwork().audioDeviceBufferMs : 0);
        profile.dropLateVideo =
            selectedOptions.liveClockPolicy == LiveClockPolicy::AudioMasterDropLateVideo;
        profile.videoLeadMs = selectedOptions.videoLeadMs;
        profile.lateVideoDropMs = selectedOptions.lateVideoDropMs;
        break;
    }

    return profile;
}

void ApplySessionRuntimeProfile(const SessionRuntimeProfile& profile, SessionResources& resources)
{
    if (!resources.vt || !resources.at)
        return;

    resources.at->SetPlaybackKind(profile.playbackKind);
    resources.vt->maxList = profile.videoQueueLimit;
    resources.at->maxList = profile.audioQueueLimit;
    resources.vt->SetOverflowPolicy(profile.videoOverflowPolicy);
    resources.at->SetOverflowPolicy(profile.audioOverflowPolicy);
    resources.at->SetOutputBufferMs(profile.audioOutputBufferMs);
    resources.vt->SetLatencyTuning(
        profile.playbackKind,
        profile.dropLateVideo,
        profile.videoLeadMs,
        profile.lateVideoDropMs);
}

bool ShouldTreatNetworkStreamAsLive(const std::string& url, Demux* demux)
{
    if (!demux || !IsNetworkUrl(url))
        return false;

    if (demux->totalMs.load() <= 0)
        return true;

    const StreamSourceType sourceType = ResolveStreamSourceType(url);
    if (UsesContinuousLiveBufferStrategy(sourceType))
        return true;

    if (sourceType == StreamSourceType::Hls && !demux->IsSeekable())
        return true;

    return false;
}
}

StreamSessionCore::StreamSessionCore(DemuxThreadState& stateRef, SessionResources& resourcesRef)
    : state(stateRef)
    , resources(resourcesRef)
{
}

void StreamSessionCore::SetLiveTuningCallbacks(std::function<void()> resetLiveRuntimeTuningCallback,
    std::function<void(bool)> updateLiveRuntimeTuningCallback)
{
    resetLiveRuntimeTuning = std::move(resetLiveRuntimeTuningCallback);
    updateLiveRuntimeTuning = std::move(updateLiveRuntimeTuningCallback);
}

void StreamSessionCore::SetAiEpochRebindCallback(std::function<void(const StreamEpoch&)> rebindAiEpochLockedCallback)
{
    rebindAiEpochLocked = std::move(rebindAiEpochLockedCallback);
}

void StreamSessionCore::UpdateStreamMetadataLocked()
{
    if (!resources.demux)
        return;

    state.totalMs.store(resources.demux->totalMs.load());
    state.bitrate.store(resources.demux->bitrate.load());
    state.videoWidth.store(resources.demux->width.load());
    state.videoHeight.store(resources.demux->height.load());
    state.videoFpsNum.store(resources.demux->videoFpsNum.load());
    state.videoFpsDen.store(resources.demux->videoFpsDen.load());
    state.audioSampleRate.store(resources.demux->sampleRate.load());
    state.audioChannels.store(resources.demux->channels.load());
}

void StreamSessionCore::ApplyRuntimeOptionsLocked(const StreamOpenOptions& selectedOptions, bool isNetworkStream)
{
    if (!resources.vt || !resources.at || !resources.demux)
        return;

    const SessionRuntimeProfile runtimeProfile = BuildSessionRuntimeProfile(
        state.currentUrl,
        isNetworkStream,
        state.isLiveStream.load(),
        resources.demux->width.load(),
        resources.demux->height.load(),
        selectedOptions);

    state.playbackKind.store(runtimeProfile.playbackKind);
    state.sourceType.store(runtimeProfile.sourceType);
    ApplySessionRuntimeProfile(runtimeProfile, resources);

    resources.vt->ResetDroppedPacketCount();
    resources.at->ResetDroppedPacketCount();
    resources.vt->ResetLatencyStats();
    resources.at->ResetLiveLatencyStats();

    if (resetLiveRuntimeTuning)
        resetLiveRuntimeTuning();
    if (updateLiveRuntimeTuning)
        updateLiveRuntimeTuning(true);
}

bool StreamSessionCore::ReopenLocked(const StreamOpenOptions& options, bool countReconnectSuccess)
{
    Logger::Instance().Log(
        LogLevel::Info,
        "session",
        "reopen.begin",
        "Reopening media session",
        {
            { "url", state.currentUrl },
            { "count_reconnect_success", countReconnectSuccess ? "true" : "false" },
        });

    FlushDemuxLocked();
    FlushDecodeLocked();

    const StreamEpoch epoch = AdvanceEpochLocked();
    ApplyEpochResetLocked(epoch, true, true);
    if (resources.recorder)
        resources.recorder->OnSessionClosed();

    delete resources.demux;
    resources.demux = new Demux();
    if (!resources.demux->Open(state.currentUrl.c_str(), options))
    {
        Logger::Instance().Log(
            LogLevel::Error,
            "session",
            "reopen.fail",
            "Reopen failed",
            {
                { "url", state.currentUrl },
                { "error", resources.demux->GetLastError() },
            });
        return false;
    }

    state.activeOpenOptions = resources.demux->GetOpenOptions();
    const bool isNetworkStream = IsNetworkUrl(state.currentUrl);
    state.isLiveStream.store(ShouldTreatNetworkStreamAsLive(state.currentUrl, resources.demux));
    state.playbackKind.store(ResolveStreamPlaybackKind(isNetworkStream, state.isLiveStream.load()));
    state.sourceType.store(ResolveStreamSourceType(state.currentUrl));
    ApplyRuntimeOptionsLocked(state.activeOpenOptions, isNetworkStream);
    state.eofCount = 0;
    state.consecutiveReadFailures.store(0);
    state.primingPending.store(true);
    SetChildThreadsPauseLocked(true);
    TransitionToPrimingLocked();
    if (countReconnectSuccess)
        state.reconnectSuccessCount.fetch_add(1);
    UpdateStreamMetadataLocked();
    if (resources.recorder)
        resources.recorder->OnSessionOpened(*resources.demux, state.currentUrl);
    Logger::Instance().Log(
        LogLevel::Info,
        "session",
        "reopen.end",
        "Reopen completed",
        {
            { "url", state.currentUrl },
            { "playback_kind", StreamPlaybackKindName(state.playbackKind.load()) },
        });
    return true;
}

bool StreamSessionCore::OpenPrepared(Demux* preparedDemux, const char* url, const StreamOpenOptions& options,
    const std::shared_ptr<VideoCallback>& call, int measuredOpenLatencyMs)
{

    if (!preparedDemux || !url || url[0] == '\0')
    {
        delete preparedDemux;
        return false;
    }

    const bool isNet = IsNetworkUrl(url);
    const bool wasPaused = state.isPause.load();
    SetPause(true);
    QThread::msleep(15);

    state.openLatencyMs.store(std::max(0, measuredOpenLatencyMs));
    state.reconnectSuccessCount.store(0);
    state.bufferingEventCount.store(0);
    state.consecutiveReadFailures.store(0);
    state.runtimeBufferProfileLevel.store(0);
    state.runtimeReadyStreak.store(0);
    state.runtimeReadyTarget.store(0);
    state.runtimeRebufferCooldownMs.store(0);
    state.runtimeMinBufferHoldMs.store(0);
    state.runtimeLowWaterStreak.store(0);
    state.runtimePlaybackResumeCount.store(0);
    state.runtimeRebufferSuppressedCount.store(0);
    state.aiGpuQueueDepth.store(0);
    state.aiGpuActiveTasks.store(0);
    state.aiCpuQueueDepth.store(0);
    state.aiCpuActiveTasks.store(0);
    state.aiCompletedTasks.store(0);
    state.aiDroppedTasks.store(0);
    state.aiCancelledTasks.store(0);
    state.aiDetectorDroppedTasks.store(0);
    state.aiDetectorCancelledTasks.store(0);
    state.aiLastWaitMs.store(0);
    state.aiAverageWaitMs.store(0);
    state.aiAccumulatedWaitMs.store(0);
    state.aiAcquireCount.store(0);

    std::lock_guard<std::mutex> lock(resources.mux);
    Logger::Instance().Log(
        LogLevel::Info,
        "session",
        "open.prepared",
        "Opening prepared demux session",
        {
            { "url", url },
            { "open_latency_ms", std::to_string(measuredOpenLatencyMs) },
        });

    TransitionToOpeningLocked();
    EnsurePipelineObjectsLocked();
    const StreamEpoch epoch = AdvanceEpochLocked();
    ApplyEpochLocked(epoch);

    if (resources.vt->isRunning())
        resources.vt->Close();
    if (resources.at->isRunning())
        resources.at->Close();

    if (resources.recorder)
        resources.recorder->OnSessionClosed();
    delete resources.demux;
    resources.demux = preparedDemux;

    state.isComplete.store(false);
    state.hasError.store(false);
    state.lastError.clear();
    state.audioPlaybackAvailable.store(false);
    state.currentUrl = url;
    resources.currentCall = call;
    state.activeOpenOptions = options;
    state.reconnectCount = 0;
    state.maxReconnect = options.reconnect.maxAttempts;
    state.isLiveStream.store(ShouldTreatNetworkStreamAsLive(url, resources.demux));
    state.liveStartupAudioResyncPending.store(false);
    state.playbackKind.store(ResolveStreamPlaybackKind(isNet, state.isLiveStream.load()));
    TransitionToPrimingLocked();

    ApplyRuntimeOptionsLocked(options, isNet);
    const bool hasAudioStream = resources.demux->GetAudioStreamCount() > 0;
    AVCodecParameters* videoParameters = resources.demux->CopyVPara();
    bool allowVideoHwDecode = true;
    if (videoParameters)
    {
        const StreamSourcePolicy sourcePolicy =
            ResolveStreamSourcePolicy(state.playbackKind.load(), state.sourceType.load(), state.activeOpenOptions);
        const bool liveRtsp = state.isLiveStream.load() && state.sourceType.load() == StreamSourceType::Rtsp;
        const bool fragileLiveCodec =
            videoParameters->codec_id == AV_CODEC_ID_H264 || videoParameters->codec_id == AV_CODEC_ID_HEVC;
        if (liveRtsp && sourcePolicy.disableFragileLiveHardwareDecode && fragileLiveCodec)
        {

            allowVideoHwDecode = false;
            Logger::Instance().Log(
                LogLevel::Info,
                "session",
                "open.video_decode_mode",
                "Disabled CUDA hardware decode for RTSP live video to keep playback stable",
                {
                    { "url", state.currentUrl },
                    { "codec_id", std::to_string(videoParameters->codec_id) },
                });
        }
    }

    if (state.isLiveStream.load())
        resources.at->SetSpeed(1.0);
    if (!resources.vt->Open(videoParameters, call,
        resources.demux->width.load(), resources.demux->height.load(), allowVideoHwDecode))
    {
        state.lastError = "Video decoder failed to open (codec not supported or hardware acceleration unavailable)";
        state.hasError.store(true);
        Logger::Instance().Log(
            LogLevel::Error,
            "session",
            "open.video_decoder_fail",
            state.lastError,
            { { "url", state.currentUrl } });
    }

    if (hasAudioStream && !resources.at->Open(resources.demux->CopyAPara(),
        resources.demux->sampleRate.load(), resources.demux->channels.load()))
    {
        state.audioPlaybackAvailable.store(false);
        const std::string audioOpenError = resources.at->GetLastOpenError();
        const bool missingAudioDevice =
            audioOpenError.find("No audio output device detected") != std::string::npos;

        state.lastError = audioOpenError.empty() ? "Audio decoder failed to open" : audioOpenError;
        if (missingAudioDevice)
        {
            state.hasError.store(false);
            state.statusEventText = "Audio output unavailable: continuing without sound";
            state.statusEventGeneration.fetch_add(1);
            Logger::Instance().Log(
                LogLevel::Warning,
                "session",
                "open.audio_device_missing",
                state.lastError,
                { { "url", state.currentUrl } });
        }
        else
        {
            state.hasError.store(true);
            Logger::Instance().Log(
                LogLevel::Error,
                "session",
                "open.audio_decoder_fail",
                state.lastError,
                { { "url", state.currentUrl } });
        }
    }
    else if (hasAudioStream)
    {
        state.audioPlaybackAvailable.store(true);
    }
    else if (!hasAudioStream)
    {
        Logger::Instance().Log(
            LogLevel::Info,
            "session",
            "open.audio_absent",
            "Opened stream without an audio track",
            { { "url", state.currentUrl } });
    }

    RebindAiEpochLocked(epoch);

    UpdateStreamMetadataLocked();
    if (resources.recorder)
        resources.recorder->OnSessionOpened(*resources.demux, state.currentUrl);
    if (!resources.vt->isRunning())
        resources.vt->start();
    if (!resources.at->isRunning())
        resources.at->start();
    state.primingPending.store(true);
    state.isPause.store(wasPaused);
    SetChildThreadsPauseLocked(true);
    if (wasPaused)
        TransitionToPlaybackLocked();
    else
        TransitionToPrimingLocked();

    Logger::Instance().Log(
        LogLevel::Info,
        "session",
        "open.success",
        "Media opened successfully",
        {
            { "url", state.currentUrl },
            { "playback_kind", StreamPlaybackKindName(state.playbackKind.load()) },
            { "is_live", state.isLiveStream.load() ? "true" : "false" },
            { "open_latency_ms", std::to_string(state.openLatencyMs.load()) },
        });

    return true;
}

bool StreamSessionCore::Open(const char* url, const std::shared_ptr<VideoCallback>& call)
{
    if (!url || url[0] == '\0')
        return false;

    const bool isNet = IsNetworkUrl(url);

    StreamOpenOptions selectedOptions;
    {
        std::lock_guard<std::mutex> lock(resources.mux);
        selectedOptions = isNet ? state.networkOpenOptions : state.fileOpenOptions;
        state.activeOpenOptions = selectedOptions;
    }

    state.openLatencyMs.store(0);
    state.reconnectSuccessCount.store(0);
    state.bufferingEventCount.store(0);
    state.consecutiveReadFailures.store(0);
    {
        std::lock_guard<std::mutex> lock(resources.mux);
        TransitionToOpeningLocked();
    }

    Logger::Instance().Log(
        LogLevel::Info,
        "session",
        "open.begin",
        "Opening media",
        {
            { "url", url },
            { "network", isNet ? "true" : "false" },
        });

    const auto openStart = std::chrono::steady_clock::now();
    std::unique_ptr<Demux> preparedDemux(new Demux());
    const bool ok = preparedDemux->Open(url, selectedOptions);
    const auto openEnd = std::chrono::steady_clock::now();
    const int measuredOpenLatencyMs = static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(openEnd - openStart).count());
    state.openLatencyMs.store(measuredOpenLatencyMs);

    if (!ok)
    {
        std::lock_guard<std::mutex> lock(resources.mux);
        std::string error = preparedDemux->GetLastError();
        if (error.empty())
            error = "Failed to open file";
        TransitionToFailedLocked(error, false);
        Logger::Instance().Log(
            LogLevel::Error,
            "session",
            "open.fail",
            error,
            {
                { "url", url },
                { "open_latency_ms", std::to_string(measuredOpenLatencyMs) },
            });
        return false;
    }

    const StreamOpenOptions effectiveOptions = preparedDemux->GetOpenOptions();
    Demux* releasedDemux = preparedDemux.release();
    return OpenPrepared(releasedDemux, url, effectiveOptions,
        call, measuredOpenLatencyMs);
}

void StreamSessionCore::SetNetworkOpenOptions(const StreamOpenOptions& options)
{
    std::lock_guard<std::mutex> lock(resources.mux);
    state.networkOpenOptions = options;
}

StreamOpenOptions StreamSessionCore::GetNetworkOpenOptions() const
{
    std::lock_guard<std::mutex> lock(resources.mux);
    return state.networkOpenOptions;
}

StreamOpenOptions StreamSessionCore::GetActiveOpenOptions() const
{
    std::lock_guard<std::mutex> lock(resources.mux);
    return state.activeOpenOptions;
}

std::string StreamSessionCore::GetCurrentUrl() const
{
    std::lock_guard<std::mutex> lock(resources.mux);
    return state.currentUrl;
}

std::string StreamSessionCore::GetLastError() const
{
    std::lock_guard<std::mutex> lock(resources.mux);
    return state.lastError;
}
