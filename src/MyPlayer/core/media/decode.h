
#pragma once

#include <atomic>
#include <mutex>
#include <memory>
#include <string>

struct AVBufferRef;
struct AVCodecContext;
struct AVCodecParameters;
struct AVFrame;
struct AVPacket;
struct SwsContext;

struct DecodeResources
{
    AVCodecContext* codec = nullptr;
    AVBufferRef* hwDeviceCtx = nullptr;
    SwsContext* swsCtx = nullptr;
    int swsWidth = 0;
    int swsHeight = 0;
    int swsSrcFmt = -1;
    AVCodecParameters* reopenParameters = nullptr;
    bool runtimeHwFallbackAttempted = false;
    bool loggedFirstDecodedFrame = false;
    mutable std::mutex mux;
};

struct DecodeState
{
    bool isAudio = false;
    std::atomic<long long> pts{ 0 };
    std::string codecName;
    std::atomic<bool> isHwAccel{ false };
    std::atomic<int> pixFmt{ -1 };
    std::atomic<int> colorSpace{ -1 };
    std::atomic<int> colorRange{ -1 };
    std::atomic<int> colorTrc{ -1 };
    std::atomic<int> sampleFmtOut{ -1 };
    std::atomic<bool> hardwareDecodeEnabled{ true };
};

class DecoderContextController;
class FrameSurfaceAdapter;
class HardwareDecodeContext;

class Decode
{
public:

    Decode();

    virtual ~Decode();

    bool& isAudio;
    std::atomic<long long>& pts;
    std::string& codecName;
    std::atomic<bool>& isHwAccel;
    std::atomic<int>& pixFmt;
    std::atomic<int>& colorSpace;
    std::atomic<int>& colorRange;
    std::atomic<int>& colorTrc;
    std::atomic<int>& sampleFmtOut;
    std::atomic<bool>& hardwareDecodeEnabled;

    virtual bool Open(AVCodecParameters* para);
    virtual bool Send(AVPacket* pkt);
    virtual bool SendDrain();
    virtual AVFrame* Recv();
    virtual void Close();
    virtual void Clear();

    void* GetCudaContext() const;

    void SetHardwareDecodeEnabled(bool enabled) { hardwareDecodeEnabled.store(enabled); }

private:

    DecodeState state_;
    DecodeResources resources_;

    std::unique_ptr<FrameSurfaceAdapter> frameSurfaceAdapter_;
    std::unique_ptr<HardwareDecodeContext> hardwareDecodeContext_;
    std::unique_ptr<DecoderContextController> decoderContextController_;
};
