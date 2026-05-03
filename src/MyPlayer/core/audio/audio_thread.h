#pragma once

#include <atomic>
#include <memory>
#include <string>

#include "../media/decode_thread.h"
#include "../session/stream_config.h"

struct AVCodecParameters;
class AudioAsrBridge;
class AudioClockController;
class AudioOutputBridge;
class AudioPacketPump;
class AudioResampleStage;
class WhisperThread;

struct AudioThreadState
{
    std::atomic<long long> pts{ 0 };
    std::atomic<bool> isPause{ false };
    std::atomic<StreamPlaybackKind> playbackKind{ StreamPlaybackKind::File };
    std::atomic<double> playbackSpeed{ 1.0 };
    std::atomic<int> targetOutputBufferMs{ 0 };
    std::atomic<int> liveCatchUpCount{ 0 };
    std::atomic<long long> lastLiveCatchUpMs{ 0 };
    std::atomic<long long> lastQueuedFramePtsMs{ -1 };
    std::atomic<int> progressiveThrottleCount{ 0 };
    std::atomic<bool> progressiveThrottleActive{ false };
};

class AudioThread : public DecodeThread
{
public:
    AudioThread();
    ~AudioThread() override;

    bool Open(AVCodecParameters* para, int sampleRate, int channels);
    void Close() override;
    void Clear() override;
    void DiscardQueuedDataKeepOutput();
    void DiscardQueuedDataAndResetOutput();
    void run() override;

    void SetPause(bool isPause);
    void SetVolume(double volume);
    void SetSpeed(double speed);
    void SetOutputBufferMs(int bufferMs);
    void SetPlaybackKind(StreamPlaybackKind playbackKind);

    long long GetAudioDeviceBufferMs();
    int GetLiveCatchUpCount() const;
    int GetProgressiveThrottleCount() const;
    void ResetLiveLatencyStats();

    void SetWhisperThread(WhisperThread* thread);
    void SetPts(long long pts);
    long long GetPts() const;
    std::string GetLastOpenError() const;

private:
    std::unique_ptr<AudioThreadState> state_;
    std::unique_ptr<AudioOutputBridge> outputBridge_;
    std::unique_ptr<AudioResampleStage> resampleStage_;
    std::unique_ptr<AudioClockController> clockController_;
    std::unique_ptr<AudioAsrBridge> asrBridge_;
    std::unique_ptr<AudioPacketPump> packetPump_;
    std::string lastOpenError_;
};
