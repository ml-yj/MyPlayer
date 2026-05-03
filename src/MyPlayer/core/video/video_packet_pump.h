#pragma once

#include <atomic>
#include <memory>
#include <mutex>

#include "../session/stream_config.h"

struct AVFrame;
class DecodeThread;
class DetectorThread;
class VideoCallback;

struct VideoThreadState
{
    std::atomic<long long> synpts{ 0 };
    std::atomic<long long> lastDecodedAtMs{ 0 };
    std::atomic<int> renderedFrames{ 0 };
    std::atomic<long long> lastRenderedAtMs{ 0 };
    std::atomic<bool> isPause{ false };
    std::atomic<StreamPlaybackKind> playbackKind{ StreamPlaybackKind::File };
    std::atomic<bool> dropLateFrames{ false };
    std::atomic<int> maxVideoLeadMs{ 200 };
    std::atomic<int> lateVideoDropMs{ 0 };
    std::atomic<int> syncDeadbandMs{ 18 };
    std::atomic<int> mediumLateDriftMs{ 120 };
    std::atomic<int> hardLateDriftMs{ 480 };
    std::atomic<int> lateFrameDropCount{ 0 };
    std::atomic<int> hardSyncResetCount{ 0 };
};

class VideoOutputBridge
{
public:
    void Bind(std::shared_ptr<VideoCallback> callback, int width, int height);
    void SetCudaContext(void* ctx);
    void Close();
    void SetDetectorThread(DetectorThread* detector);

    bool DispatchFrame(AVFrame* frame, bool paused);
    bool PreviewFrame(AVFrame* frame);

private:
    std::mutex mux_;
    std::shared_ptr<VideoCallback> callback_;
    DetectorThread* detectorThread_ = nullptr;
};

class VideoSyncController
{
public:
    explicit VideoSyncController(VideoThreadState& state);

    bool ShouldThrottleForLead(long long currentSyncPts, long long currentDecodePts, int& leadStallCount) const;
    bool ShouldHardResetForLateDrift(long long currentSyncPts, long long currentDecodePts);
    bool ShouldDropLateFrame(long long currentSyncPts, long long currentDecodePts);
    int ComputeRenderDelayMs(long long currentSyncPts, long long currentDecodePts) const;

    void Configure(StreamPlaybackKind playbackKind, bool dropLateFrames, int maxLeadMs, int lateDropMs);
    int GetLateFrameDropCount() const;
    void ResetLatencyStats();

private:
    VideoThreadState& state_;
};

class VideoPacketPump
{
public:
    VideoPacketPump(DecodeThread& owner, std::atomic<bool>& exitFlag, VideoThreadState& state,
        VideoOutputBridge& outputBridge, VideoSyncController& syncController);

    void Run();

private:
    DecodeThread& owner_;
    std::atomic<bool>& exitFlag_;
    VideoThreadState& state_;
    VideoOutputBridge& outputBridge_;
    VideoSyncController& syncController_;
};
