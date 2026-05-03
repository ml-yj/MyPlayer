#pragma once

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
}

#include <memory>
#include <chrono>
#include <thread>

struct AVPacketDeleter {
    void operator()(AVPacket* pkt) {
        if (pkt) av_packet_free(&pkt);
    }
};

using AVPacketPtr = std::unique_ptr<AVPacket, AVPacketDeleter>;

inline AVPacketPtr MakeAVPacket() {
    return AVPacketPtr(av_packet_alloc());
}

struct AVFrameDeleter {
    void operator()(AVFrame* frame) {
        if (frame) av_frame_free(&frame);
    }
};

using AVFramePtr = std::unique_ptr<AVFrame, AVFrameDeleter>;

inline AVFramePtr MakeAVFrame() {
    return AVFramePtr(av_frame_alloc());
}

inline void MSleep(int ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

inline long long NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

inline std::string FormatTime(long long ms) {
    if (ms < 0) ms = 0;
    int sec = (int)(ms / 1000);
    int m = sec / 60;
    int s = sec % 60;
    char buf[32];
    snprintf(buf, sizeof(buf), "%02d:%02d", m, s);
    return buf;
}
