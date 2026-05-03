
#include "video_packet_pump.h"

#include "../media/decode.h"
#include "../media/decode_thread.h"
#include "../../features/detector/detector_thread.h"
#include "video_callback.h"

#include <QThread>

#include <algorithm>
#include <chrono>

extern "C" {
#include <libavcodec/packet.h>
#include <libavutil/frame.h>
}

namespace
{
long long SteadyNowMs()
{
    return static_cast<long long>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}

bool IsBalancedLiveSync(const VideoThreadState& state)
{
    return state.playbackKind.load() == StreamPlaybackKind::Live
        && !state.dropLateFrames.load();
}
}

void VideoOutputBridge::Bind(std::shared_ptr<VideoCallback> callback, int width, int height)
{
    std::lock_guard<std::mutex> lock(mux_);
    callback_ = std::move(callback);
    if (callback_)
    {
        callback_->Init(width, height);
        callback_->SetClosing(false);
    }
}

void VideoOutputBridge::SetCudaContext(void* ctx)
{
    std::lock_guard<std::mutex> lock(mux_);
    if (callback_)
        callback_->SetCudaContext(ctx);
}

void VideoOutputBridge::Close()
{
    std::lock_guard<std::mutex> lock(mux_);
    if (callback_)
    {
        callback_->SetClosing(true);
        callback_->SetCudaContext(nullptr);
    }
    callback_.reset();
    detectorThread_ = nullptr;
}

void VideoOutputBridge::SetDetectorThread(DetectorThread* detector)
{
    std::lock_guard<std::mutex> lock(mux_);
    detectorThread_ = detector;
}

bool VideoOutputBridge::DispatchFrame(AVFrame* frame, bool paused)
{
    bool rendered = false;
    std::lock_guard<std::mutex> lock(mux_);
    if (!paused && callback_)
    {
        callback_->Repaint(frame);
        rendered = true;
    }
    if (detectorThread_)
        detectorThread_->PushFrame(frame);
    return rendered;
}

bool VideoOutputBridge::PreviewFrame(AVFrame* frame)
{
    std::lock_guard<std::mutex> lock(mux_);
    if (!callback_)
        return false;

    callback_->Repaint(frame);
    return true;
}

VideoSyncController::VideoSyncController(VideoThreadState& state)
    : state_(state)
{
}

bool VideoSyncController::ShouldThrottleForLead(long long currentSyncPts, long long currentDecodePts, int& leadStallCount) const
{
    const int maxLeadMs = state_.maxVideoLeadMs.load();
    const bool balancedLiveSync = IsBalancedLiveSync(state_);
    const int deadbandMs = balancedLiveSync
        ? std::max(24, state_.syncDeadbandMs.load())
        : state_.syncDeadbandMs.load();
    const int extraLeadSlackMs = balancedLiveSync ? std::max(40, deadbandMs) : 0;
    if (currentSyncPts > 0
        && currentDecodePts > currentSyncPts + maxLeadMs + deadbandMs + extraLeadSlackMs)
    {
        if (leadStallCount < 200)
        {
            ++leadStallCount;
            return true;
        }
        return false;
    }

    leadStallCount = 0;
    return false;
}

bool VideoSyncController::ShouldHardResetForLateDrift(long long currentSyncPts, long long currentDecodePts)
{
    if (state_.playbackKind.load() != StreamPlaybackKind::Live
        || currentSyncPts <= 0
        || currentDecodePts <= 0)
    {
        return false;
    }

    if (!state_.dropLateFrames.load())
        return false;

    const long long latenessMs = currentSyncPts - currentDecodePts;
    if (latenessMs < state_.hardLateDriftMs.load())
        return false;

    state_.hardSyncResetCount.fetch_add(1);
    return true;
}

bool VideoSyncController::ShouldDropLateFrame(long long currentSyncPts, long long currentDecodePts)
{
    if (currentSyncPts <= 0 || currentDecodePts <= 0)
        return false;

    if (!state_.dropLateFrames.load())
        return false;

    const long long latenessMs = currentSyncPts - currentDecodePts;
    const int mediumLateMs = state_.mediumLateDriftMs.load();
    const int lateDropMs = state_.lateVideoDropMs.load();
    const int dropThresholdMs = lateDropMs > 0
        ? std::min(mediumLateMs, lateDropMs)
        : mediumLateMs;
    if (latenessMs >= dropThresholdMs)
    {
        state_.lateFrameDropCount.fetch_add(1);
        return true;
    }

    return false;
}

int VideoSyncController::ComputeRenderDelayMs(long long currentSyncPts, long long currentDecodePts) const
{
    const bool balancedLiveSync = IsBalancedLiveSync(state_);
    const int deadbandMs = balancedLiveSync
        ? std::max(24, state_.syncDeadbandMs.load())
        : state_.syncDeadbandMs.load();
    if (currentSyncPts > 0 && currentDecodePts > currentSyncPts + deadbandMs)
    {
        const int maxLeadMs = state_.maxVideoLeadMs.load();
        const long long driftMs = currentDecodePts - currentSyncPts - deadbandMs;
        const int divisor = balancedLiveSync
            ? 5
            : (state_.playbackKind.load() == StreamPlaybackKind::Live ? 3 : 2);
        const long long correctedDelayMs = driftMs / std::max(1, divisor);
        if (correctedDelayMs > 0)
        {
            const int delayCapMs = balancedLiveSync ? 8 : maxLeadMs;
            return std::min(delayCapMs, static_cast<int>(correctedDelayMs));
        }
    }

    return 0;
}

void VideoSyncController::Configure(
    StreamPlaybackKind playbackKind, bool dropLateFrames, int maxLeadMs, int lateDropMs)
{
    state_.playbackKind.store(playbackKind);
    state_.dropLateFrames.store(dropLateFrames);
    const bool balancedLiveSync = playbackKind == StreamPlaybackKind::Live && !dropLateFrames;
    const int configuredLeadMs = maxLeadMs > 0
        ? maxLeadMs
        : (playbackKind == StreamPlaybackKind::Live ? 80 : 200);
    state_.maxVideoLeadMs.store(configuredLeadMs);

    int configuredLateDropMs = lateDropMs;
    if (configuredLateDropMs <= 0)
    {
        if (playbackKind == StreamPlaybackKind::Live)
            configuredLateDropMs = 160;
        else if (playbackKind == StreamPlaybackKind::NetworkVod)
            configuredLateDropMs = 240;
    }
    state_.lateVideoDropMs.store(configuredLateDropMs > 0 ? configuredLateDropMs : 0);

    const int deadbandMs = playbackKind == StreamPlaybackKind::Live
        ? (balancedLiveSync ? 24 : 12)
        : 18;
    state_.syncDeadbandMs.store(deadbandMs);
    state_.mediumLateDriftMs.store(
        playbackKind == StreamPlaybackKind::Live
            ? (balancedLiveSync
                ? std::max(90, configuredLeadMs / 2)
                : std::max(60, state_.lateVideoDropMs.load() > 0 ? state_.lateVideoDropMs.load() / 2 : 80))
            : (playbackKind == StreamPlaybackKind::NetworkVod
                ? std::max(90, state_.lateVideoDropMs.load() > 0 ? state_.lateVideoDropMs.load() / 2 : 120)
                : std::max(140, configuredLeadMs / 2)));
    state_.hardLateDriftMs.store(
        playbackKind == StreamPlaybackKind::Live
            ? (balancedLiveSync
                ? std::max(320, configuredLeadMs + 160)
                : std::max(220, std::max(configuredLeadMs + 120, state_.lateVideoDropMs.load() * 2)))
            : (playbackKind == StreamPlaybackKind::NetworkVod
                ? std::max(320, std::max(configuredLeadMs + 160, state_.lateVideoDropMs.load() * 2))
                : std::max(480, configuredLeadMs * 2)));
}

int VideoSyncController::GetLateFrameDropCount() const
{
    return state_.lateFrameDropCount.load();
}

void VideoSyncController::ResetLatencyStats()
{
    state_.lateFrameDropCount.store(0);
    state_.hardSyncResetCount.store(0);
}

VideoPacketPump::VideoPacketPump(DecodeThread& owner, std::atomic<bool>& exitFlag,
    VideoThreadState& state, VideoOutputBridge& outputBridge, VideoSyncController& syncController)
    : owner_(owner)
    , exitFlag_(exitFlag)
    , state_(state)
    , outputBridge_(outputBridge)
    , syncController_(syncController)
{
}

void VideoPacketPump::Run()
{
    int failCount = 0;
    int syncLeadStallCount = 0;

    while (!exitFlag_.load())
    {
        if (state_.isPause.load())
        {
            syncLeadStallCount = 0;
            QThread::msleep(5);
            continue;
        }

        Decode* decode = owner_.GetDecode();
        if (!decode)
        {
            QThread::msleep(1);
            continue;
        }

        const long long currentSyncPts = state_.synpts.load();
        const long long currentDecodePts = decode->pts.load();
        if (syncController_.ShouldThrottleForLead(currentSyncPts, currentDecodePts, syncLeadStallCount))
        {
            QThread::msleep(1);
            continue;
        }

        PacketEnvelope item = owner_.Pop();
        if (item.kind == PacketEnvelopeKind::Empty)
        {
            QThread::msleep(1);
            continue;
        }

        if (item.kind == PacketEnvelopeKind::Flush)
        {
            if (Decode* flushDecode = owner_.GetDecode())
                flushDecode->Clear();
            failCount = 0;
            syncLeadStallCount = 0;
            continue;
        }

        if (item.generation != owner_.GetQueueGeneration()
            || item.serial != owner_.GetQueueSerial())
        {
            av_packet_free(&item.packet);
            continue;
        }

        const bool isDrain = item.kind == PacketEnvelopeKind::Drain;
        const bool sendOk = isDrain ? decode->SendDrain() : decode->Send(item.packet);
        if (item.packet)
            av_packet_free(&item.packet);
        if (!sendOk)
        {
            ++failCount;
            const int sleepMs = (failCount < 10) ? 1 : (failCount < 50) ? 10 : 50;
            QThread::msleep(sleepMs);
            continue;
        }

        failCount = 0;

        while (!exitFlag_.load())
        {
            AVFrame* frame = decode->Recv();
            if (!frame)
                break;

            if (item.generation != owner_.GetQueueGeneration()
                || item.serial != owner_.GetQueueSerial())
            {
                av_frame_free(&frame);
                continue;
            }

            state_.lastDecodedAtMs.store(SteadyNowMs());

            const long long syncPts = state_.synpts.load();
            const long long decodePts = decode->pts.load();
            if (syncController_.ShouldHardResetForLateDrift(syncPts, decodePts))
            {
                owner_.DecodeThread::Clear();
                syncLeadStallCount = 0;
                av_frame_free(&frame);
                break;
            }
            if (syncController_.ShouldDropLateFrame(syncPts, decodePts))
            {
                av_frame_free(&frame);
                continue;
            }

            const int delayMs = syncController_.ComputeRenderDelayMs(syncPts, decodePts);
            if (delayMs > 0)
                QThread::msleep(static_cast<unsigned long>(delayMs));

            if (outputBridge_.DispatchFrame(frame, state_.isPause.load()))
            {
                state_.renderedFrames.fetch_add(1);
                state_.lastRenderedAtMs.store(SteadyNowMs());
            }

            av_frame_free(&frame);
        }
    }
}
