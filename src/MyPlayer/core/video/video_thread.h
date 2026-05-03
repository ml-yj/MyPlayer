#pragma once

#include <memory>

#include "../media/decode_thread.h"
#include "video_packet_pump.h"

struct AVCodecParameters;
struct AVPacket;
class DetectorThread;
class VideoCallback;
class VideoSyncController;

class VideoThread : public DecodeThread
{
public:
    VideoThread();
    ~VideoThread() override;

    bool Open(AVCodecParameters* para, const std::shared_ptr<VideoCallback>& call, int width, int height,
        bool allowHardwareDecode = true);
    void Close() override;

    void SetPause(bool isPause);
    void run() override;
    bool RepaintPts(AVPacket* pkt, long long seekpts);

    void SetDetectorThread(DetectorThread* dt);
    void SetLatencyTuning(StreamPlaybackKind playbackKind, bool dropLateFrames, int maxLeadMs, int lateDropMs);
    int GetLateFrameDropCount() const;
    void ResetLatencyStats();

    void SetSyncPts(long long pts);
    long long GetSyncPts() const;
    long long GetLastDecodedAtMs() const;
    int TakeRenderedFrames();
    long long GetLastRenderedAtMs() const;

private:
    VideoThreadState state_;
    std::unique_ptr<VideoOutputBridge> outputBridge_;
    std::unique_ptr<VideoSyncController> syncController_;
    std::unique_ptr<VideoPacketPump> packetPump_;
};
