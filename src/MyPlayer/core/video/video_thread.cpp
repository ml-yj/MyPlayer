

#include "video_thread.h"

#include "../media/decode.h"
#include "../../features/detector/detector_thread.h"
#include "video_callback.h"
#include "../../common/diagnostics/logger.h"

extern "C" {
#include <libavutil/frame.h>
}

VideoThread::VideoThread()
{
    outputBridge_ = std::make_unique<VideoOutputBridge>();
    syncController_ = std::make_unique<VideoSyncController>(state_);
    packetPump_ = std::make_unique<VideoPacketPump>(*this, isExit, state_, *outputBridge_, *syncController_);
}

VideoThread::~VideoThread()
{
    isExit.store(true);
    cv.notify_all();
    wait();
}

bool VideoThread::Open(AVCodecParameters* para, const std::shared_ptr<VideoCallback>& call, int width, int height,
    bool allowHardwareDecode)
{
    if (!para)
        return false;

    if (!decode)
        decode = new Decode();
    decode->SetHardwareDecodeEnabled(allowHardwareDecode);
    isExit.store(false);
    Clear();

    state_.synpts.store(0);
    state_.lastDecodedAtMs.store(0);
    state_.renderedFrames.store(0);
    state_.lastRenderedAtMs.store(0);
    outputBridge_->Bind(call, width, height);

    if (!decode->Open(para))
        return false;

    const void* cudaContext = decode->GetCudaContext();
    if (cudaContext)
        outputBridge_->SetCudaContext(const_cast<void*>(cudaContext));

    return true;
}

void VideoThread::Close()
{
    isExit.store(true);
    cv.notify_all();
    wait();

    if (outputBridge_)
        outputBridge_->Close();

    DecodeThread::Close();
}

void VideoThread::SetPause(bool isPause)
{
    state_.isPause.store(isPause);
}

void VideoThread::run()
{
    Logger::Instance().Log(
        LogLevel::Info,
        "video",
        "thread.run",
        "Video thread started");
    if (packetPump_)
        packetPump_->Run();
}

bool VideoThread::RepaintPts(AVPacket* pkt, long long seekpts)
{
    Decode* activeDecode = GetDecode();
    if (!activeDecode || !outputBridge_)
        return false;
    const StreamEpoch epoch = GetQueueEpoch();
    if (epoch.generation == 0 || epoch.serial == 0)
        return false;
    if (!activeDecode->Send(pkt))
        return false;

    AVFrame* frame = activeDecode->Recv();
    if (!frame)
        return false;

    if (GetQueueGeneration() != epoch.generation || GetQueueSerial() != epoch.serial)
    {
        av_frame_free(&frame);
        return false;
    }

    bool reached = false;
    if (activeDecode->pts.load() >= seekpts)
        reached = outputBridge_->PreviewFrame(frame);

    av_frame_free(&frame);
    return reached;
}

void VideoThread::SetDetectorThread(DetectorThread* dt)
{
    if (outputBridge_)
        outputBridge_->SetDetectorThread(dt);
}

void VideoThread::SetLatencyTuning(
    StreamPlaybackKind playbackKind, bool dropLateFrames, int maxLeadMs, int lateDropMs)
{
    if (syncController_)
        syncController_->Configure(playbackKind, dropLateFrames, maxLeadMs, lateDropMs);
}

int VideoThread::GetLateFrameDropCount() const
{
    return syncController_ ? syncController_->GetLateFrameDropCount() : 0;
}

void VideoThread::ResetLatencyStats()
{
    if (syncController_)
        syncController_->ResetLatencyStats();
}

void VideoThread::SetSyncPts(long long pts)
{
    state_.synpts.store(pts);
}

long long VideoThread::GetSyncPts() const
{
    return state_.synpts.load();
}

long long VideoThread::GetLastDecodedAtMs() const
{
    return state_.lastDecodedAtMs.load();
}

int VideoThread::TakeRenderedFrames()
{
    return state_.renderedFrames.exchange(0);
}

long long VideoThread::GetLastRenderedAtMs() const
{
    return state_.lastRenderedAtMs.load();
}
