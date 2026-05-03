#pragma once

#include <atomic>
#include <mutex>

struct AVFrame;
class CudaInteropBridge;

bool IsSemiPlanarFormat(int fmt);
bool Is10BitFormat(int fmt);

struct VideoFrameState
{
    unsigned char* datas[3] = { nullptr, nullptr, nullptr };
    int bufBytes[3] = { 0, 0, 0 };

    int width = 0;
    int height = 0;
    int curFmt = -1;
    int curColorspace = -1;
    int curColorRange = -1;
    bool curIsHDR = false;
    bool isInited = false;
    bool isClosing = false;
    bool texNeedRebuild = false;
    bool loggedFirstAcceptedFrame = false;
    bool loggedCudaTransferFallback = false;

    void ClearCpuBuffers();
    void ResetForStream(int newWidth, int newHeight);
};

bool CopyVideoFrameToState(const AVFrame* frame, VideoFrameState& state);

class VideoFrameBridge
{
public:
    VideoFrameBridge() = default;
    ~VideoFrameBridge();

    std::mutex& mutex();
    std::mutex& mutex() const;
    VideoFrameState& state();
    const VideoFrameState& state() const;

    void InitState(int width, int height);
    void SetClosing(bool closing);
    bool IngestFrame(AVFrame* frame, CudaInteropBridge& cudaInterop);

    bool RequestFrameUpdate();
    bool ScheduleFrameUpdateIfDirty();
    bool ConsumePendingUpdate();
    void MarkPaintStarted();

private:
    mutable std::mutex mux_;
    VideoFrameState state_;
    std::atomic<bool> frameUpdateQueued_{ false };
    std::atomic<bool> frameDirty_{ false };
};
