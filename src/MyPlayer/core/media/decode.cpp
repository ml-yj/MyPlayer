#include "decode.h"

struct DecodeResources;
struct DecodeState;
struct AVCodecContext;

class HardwareDecodeContext
{
public:

    HardwareDecodeContext(DecodeState& state, DecodeResources& resources);

    void Configure(AVCodecContext* codec);

    void* GetCudaContext() const;

private:

    DecodeState& state_;

    DecodeResources& resources_;
};

struct AVCodecParameters;
struct AVFrame;
struct AVPacket;
struct DecodeResources;
struct DecodeState;

class HardwareDecodeContext;

class DecoderContextController
{
public:

    DecoderContextController(
        DecodeState& state,
        DecodeResources& resources,
        HardwareDecodeContext& hardwareDecodeContext);

    bool Open(AVCodecParameters* para);

    void Close();

    void Clear();

    bool Send(AVPacket* pkt);

    bool SendDrain();

    AVFrame* ReceiveFrame();

private:

    void ResetReportState();

    bool TryReopenWithoutHardwareDecodeLocked(const char* phase, int ffmpegError);

    DecodeState& state_;
    DecodeResources& resources_;
    HardwareDecodeContext& hardwareDecodeContext_;
};

struct AVFrame;
struct DecodeResources;
struct DecodeState;

class FrameSurfaceAdapter
{
public:

    FrameSurfaceAdapter(DecodeState& state, DecodeResources& resources);

    AVFrame* Adapt(AVFrame* frame);

private:

    static bool IsRendererNative(int pixelFormat);

    void UpdateVideoState(const AVFrame* frame) const;

    void UpdateCudaState(const AVFrame* frame) const;

    AVFrame* ConvertSoftwareVideoFrame(AVFrame* frame);

    DecodeState& state_;
    DecodeResources& resources_;
};

Decode::Decode()

    : isAudio(state_.isAudio)
    , pts(state_.pts)
    , codecName(state_.codecName)
    , isHwAccel(state_.isHwAccel)
    , pixFmt(state_.pixFmt)
    , colorSpace(state_.colorSpace)
    , colorRange(state_.colorRange)
    , colorTrc(state_.colorTrc)
    , sampleFmtOut(state_.sampleFmtOut)
    , hardwareDecodeEnabled(state_.hardwareDecodeEnabled)
{

    frameSurfaceAdapter_ = std::make_unique<FrameSurfaceAdapter>(state_, resources_);

    hardwareDecodeContext_ = std::make_unique<HardwareDecodeContext>(state_, resources_);

    decoderContextController_ =
        std::make_unique<DecoderContextController>(state_, resources_, *hardwareDecodeContext_);
}

Decode::~Decode()
{
    Close();
}

bool Decode::Open(AVCodecParameters* para)
{

    return decoderContextController_ ? decoderContextController_->Open(para) : false;
}

bool Decode::Send(AVPacket* pkt)
{

    return decoderContextController_ ? decoderContextController_->Send(pkt) : false;
}

bool Decode::SendDrain()
{

    return decoderContextController_ ? decoderContextController_->SendDrain() : false;
}

AVFrame* Decode::Recv()
{

    if (!decoderContextController_ || !frameSurfaceAdapter_)
        return nullptr;

    AVFrame* frame = decoderContextController_->ReceiveFrame();

    if (!frame)
        return nullptr;

    return frameSurfaceAdapter_->Adapt(frame);
}

void Decode::Close()
{

    if (decoderContextController_)
        decoderContextController_->Close();
}

void Decode::Clear()
{

    if (decoderContextController_)
        decoderContextController_->Clear();
}

void* Decode::GetCudaContext() const
{

    return hardwareDecodeContext_ ? hardwareDecodeContext_->GetCudaContext() : nullptr;
}

#include <iostream>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_cuda.h>
#include <libavutil/pixfmt.h>
}

namespace {

    AVPixelFormat SelectHardwareFormat(AVCodecContext* context, const AVPixelFormat* pixelFormats)
    {

        (void)context;

        if (!pixelFormats)
            return AV_PIX_FMT_NONE;

        for (const AVPixelFormat* format = pixelFormats; *format != AV_PIX_FMT_NONE; ++format)
        {

            if (*format == AV_PIX_FMT_CUDA)
                return *format;
        }

        return pixelFormats[0];
    }
}

HardwareDecodeContext::HardwareDecodeContext(DecodeState& state, DecodeResources& resources)
    : state_(state)
    , resources_(resources)
{}

void HardwareDecodeContext::Configure(AVCodecContext* codec)
{

    if (!codec || state_.isAudio)
        return;

    if (!state_.hardwareDecodeEnabled.load())
    {
        std::cout << "CUDA hardware decoding disabled for this stream" << std::endl;
        return;
    }

    const int result =
        av_hwdevice_ctx_create(&resources_.hwDeviceCtx, AV_HWDEVICE_TYPE_CUDA, nullptr, nullptr, 0);

    if (result == 0 && resources_.hwDeviceCtx)
    {

        codec->hw_device_ctx = av_buffer_ref(resources_.hwDeviceCtx);

        codec->get_format = SelectHardwareFormat;

        std::cout << "CUDA hardware decoding enabled" << std::endl;
        return;
    }

    if (resources_.hwDeviceCtx)
        av_buffer_unref(&resources_.hwDeviceCtx);

    std::cout << "CUDA not available, using software decoding" << std::endl;
}

void* HardwareDecodeContext::GetCudaContext() const
{

    std::lock_guard<std::mutex> lock(resources_.mux);

    if (!resources_.hwDeviceCtx)
        return nullptr;

    auto* deviceContext = reinterpret_cast<AVHWDeviceContext*>(resources_.hwDeviceCtx->data);

    auto* cudaContext = reinterpret_cast<AVCUDADeviceContext*>(deviceContext->hwctx);

    return cudaContext->cuda_ctx;
}

#include "../../common/diagnostics/logger.h"

#include <iostream>
#include <string>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/error.h>
#include <libavutil/version.h>
#include <libswscale/swscale.h>
}

namespace {

std::string DescribeAvError(int errorCode)
{
    char buffer[1024] = { 0 };
    av_strerror(errorCode, buffer, sizeof(buffer) - 1);
    return buffer;
}

int AudioFrameChannels(const AVFrame* frame)
{
    if (!frame)
        return 0;
#if LIBAVUTIL_VERSION_MAJOR >= 57
    return frame->ch_layout.nb_channels;
#else
    return frame->channels;
#endif
}

void ResetScalerState(DecodeResources& resources)
{
    if (resources.swsCtx)
        sws_freeContext(resources.swsCtx);

    resources.swsCtx = nullptr;
    resources.swsWidth = 0;
    resources.swsHeight = 0;
    resources.swsSrcFmt = -1;
}

}

DecoderContextController::DecoderContextController(
    DecodeState& state,
    DecodeResources& resources,
    HardwareDecodeContext& hardwareDecodeContext)
    : state_(state)
    , resources_(resources)
    , hardwareDecodeContext_(hardwareDecodeContext)
{}

bool DecoderContextController::Open(AVCodecParameters* para)
{

    if (!para)
        return false;

    AVCodecParameters* reopenParameters = avcodec_parameters_alloc();
    if (!reopenParameters)
    {
        avcodec_parameters_free(&para);
        return false;
    }
    if (avcodec_parameters_copy(reopenParameters, para) < 0)
    {
        avcodec_parameters_free(&reopenParameters);
        avcodec_parameters_free(&para);
        return false;
    }

    Close();

    ResetReportState();

    const AVCodec* codec = avcodec_find_decoder(para->codec_id);

    if (!codec)
    {
        std::cout << "Can't find codec id " << para->codec_id << std::endl;
        avcodec_parameters_free(&reopenParameters);
        avcodec_parameters_free(&para);
        return false;
    }

    std::lock_guard<std::mutex> lock(resources_.mux);

    resources_.codec = avcodec_alloc_context3(codec);

    if (!resources_.codec)
    {
        avcodec_parameters_free(&reopenParameters);
        avcodec_parameters_free(&para);
        return false;
    }

    if (avcodec_parameters_to_context(resources_.codec, para) < 0)
    {
        avcodec_parameters_free(&reopenParameters);
        avcodec_parameters_free(&para);
        avcodec_free_context(&resources_.codec);
        return false;
    }

    avcodec_parameters_free(&para);

    resources_.codec->thread_count = 8;

    hardwareDecodeContext_.Configure(resources_.codec);

    const int result = avcodec_open2(resources_.codec, nullptr, nullptr);
    if (result != 0)
    {
        const std::string message = DescribeAvError(result);
        std::cout << "avcodec_open2 failed: " << message << std::endl;
        avcodec_parameters_free(&reopenParameters);

        avcodec_free_context(&resources_.codec);

        if (resources_.hwDeviceCtx)
            av_buffer_unref(&resources_.hwDeviceCtx);
        return false;
    }

    std::cout << "avcodec_open2 success!" << std::endl;

    if (resources_.reopenParameters)
        avcodec_parameters_free(&resources_.reopenParameters);
    resources_.reopenParameters = reopenParameters;
    resources_.runtimeHwFallbackAttempted = false;
    resources_.loggedFirstDecodedFrame = false;

    if (resources_.codec->codec)
        state_.codecName = resources_.codec->codec->name;

    state_.isHwAccel.store(resources_.hwDeviceCtx != nullptr);

    if (state_.isAudio)
        state_.sampleFmtOut.store(resources_.codec->sample_fmt);

    return true;
}

void DecoderContextController::Close()
{

    std::lock_guard<std::mutex> lock(resources_.mux);

    if (resources_.codec)
        avcodec_free_context(&resources_.codec);

    if (resources_.hwDeviceCtx)
        av_buffer_unref(&resources_.hwDeviceCtx);

    if (resources_.reopenParameters)
        avcodec_parameters_free(&resources_.reopenParameters);

    ResetScalerState(resources_);
    resources_.runtimeHwFallbackAttempted = false;
    resources_.loggedFirstDecodedFrame = false;

    state_.pts.store(0);
}

void DecoderContextController::Clear()
{

    std::lock_guard<std::mutex> lock(resources_.mux);

    if (resources_.codec)
        avcodec_flush_buffers(resources_.codec);
}

bool DecoderContextController::Send(AVPacket* pkt)
{

    if (!pkt || pkt->size <= 0 || !pkt->data)
        return false;

    std::lock_guard<std::mutex> lock(resources_.mux);
    if (!resources_.codec)
        return false;

    const int result = avcodec_send_packet(resources_.codec, pkt);
    if (result == 0)
        return true;
    if (result == AVERROR(EAGAIN))
        return false;

    Logger::Instance().Log(
        LogLevel::Warning,
        "decode",
        "send.failed",
        "Decoder rejected compressed packet",
        {
            { "phase", "send_packet" },
            { "ffmpeg_error", DescribeAvError(result) },
            { "hw_accel", state_.isHwAccel.load() ? "true" : "false" },
        });

    if (TryReopenWithoutHardwareDecodeLocked("send_packet", result))
    {
        const int retryResult = avcodec_send_packet(resources_.codec, pkt);
        if (retryResult == 0)
            return true;

        Logger::Instance().Log(
            LogLevel::Warning,
            "decode",
            "send.retry_failed",
            "Software fallback reopened decoder but the current packet still could not be queued",
            {
                { "phase", "send_packet" },
                { "ffmpeg_error", DescribeAvError(retryResult) },
            });
    }

    return false;
}

bool DecoderContextController::SendDrain()
{

    std::lock_guard<std::mutex> lock(resources_.mux);
    if (!resources_.codec)
        return false;

    const int result = avcodec_send_packet(resources_.codec, nullptr);

    if (result == 0 || result == AVERROR_EOF)
        return true;
    if (result == AVERROR(EAGAIN))
        return false;

    Logger::Instance().Log(
        LogLevel::Warning,
        "decode",
        "drain.failed",
        "Decoder rejected drain request",
        {
            { "phase", "send_drain" },
            { "ffmpeg_error", DescribeAvError(result) },
            { "hw_accel", state_.isHwAccel.load() ? "true" : "false" },
        });

    if (TryReopenWithoutHardwareDecodeLocked("send_drain", result))
    {
        const int retryResult = avcodec_send_packet(resources_.codec, nullptr);
        return retryResult == 0 || retryResult == AVERROR_EOF;
    }

    return false;
}

AVFrame* DecoderContextController::ReceiveFrame()
{
    std::lock_guard<std::mutex> lock(resources_.mux);

    if (!resources_.codec)
        return nullptr;

    AVFrame* frame = av_frame_alloc();
    if (!frame)
        return nullptr;

    const int result = avcodec_receive_frame(resources_.codec, frame);
    if (result != 0)
    {

        av_frame_free(&frame);
        if (result == AVERROR(EAGAIN) || result == AVERROR_EOF)
            return nullptr;

        Logger::Instance().Log(
            LogLevel::Warning,
            "decode",
            "receive.failed",
            "Decoder failed while receiving a decompressed frame",
            {
                { "phase", "receive_frame" },
                { "ffmpeg_error", DescribeAvError(result) },
                { "hw_accel", state_.isHwAccel.load() ? "true" : "false" },
            });

        TryReopenWithoutHardwareDecodeLocked("receive_frame", result);
        return nullptr;
    }

    if (frame->pts == AV_NOPTS_VALUE)
    {
        if (frame->best_effort_timestamp != AV_NOPTS_VALUE)
            frame->pts = frame->best_effort_timestamp;
        else if (frame->pkt_dts != AV_NOPTS_VALUE)
            frame->pts = frame->pkt_dts;
    }

    if (!resources_.loggedFirstDecodedFrame)
    {
        if (state_.isAudio)
        {
            Logger::Instance().Log(
                LogLevel::Info,
                "decode",
                "frame.first_audio_decoded",
                "Decoder produced the first audio frame",
                {
                    { "pts", std::to_string(frame->pts) },
                    { "sample_rate", std::to_string(frame->sample_rate) },
                    { "sample_format", std::to_string(frame->format) },
                    { "channels", std::to_string(AudioFrameChannels(frame)) },
                });
        }
        else
        {
            Logger::Instance().Log(
                LogLevel::Info,
                "decode",
                "frame.first_decoded",
                "Decoder produced the first video frame",
                {
                    { "pts", std::to_string(frame->pts) },
                    { "width", std::to_string(frame->width) },
                    { "height", std::to_string(frame->height) },
                    { "pixel_format", std::to_string(frame->format) },
                    { "hw_accel", state_.isHwAccel.load() ? "true" : "false" },
                });
        }
        resources_.loggedFirstDecodedFrame = true;
    }

    return frame;
}

bool DecoderContextController::TryReopenWithoutHardwareDecodeLocked(const char* phase, int ffmpegError)
{
    if (state_.isAudio || !resources_.codec || !resources_.hwDeviceCtx ||
        resources_.runtimeHwFallbackAttempted || !resources_.reopenParameters)
        return false;

    const bool receivePhase = phase && std::strcmp(phase, "receive_frame") == 0;
    if (receivePhase && ffmpegError == AVERROR_INVALIDDATA)
    {
        Logger::Instance().Log(
            LogLevel::Warning,
            "decode",
            "hw_fallback.skipped_invalid_data",
            "Ignored a corrupt compressed frame while keeping CUDA hardware decode active",
            {
                { "phase", phase },
                { "ffmpeg_error", DescribeAvError(ffmpegError) },
            });
        return false;
    }

    resources_.runtimeHwFallbackAttempted = true;
    Logger::Instance().Log(
        LogLevel::Warning,
        "decode",
        "hw_fallback.begin",
        "CUDA hardware decode failed at runtime; retrying with software decoder",
        {
            { "phase", phase ? phase : "unknown" },
            { "ffmpeg_error", DescribeAvError(ffmpegError) },
        });

    AVCodecParameters* parameters = avcodec_parameters_alloc();
    if (!parameters)
    {
        Logger::Instance().Log(
            LogLevel::Error,
            "decode",
            "hw_fallback.alloc_failed",
            "Unable to allocate codec parameters for software fallback");
        return false;
    }

    const int copyResult = avcodec_parameters_copy(parameters, resources_.reopenParameters);
    if (copyResult < 0)
    {
        Logger::Instance().Log(
            LogLevel::Error,
            "decode",
            "hw_fallback.copy_failed",
            "Unable to copy codec parameters for software fallback",
            {
                { "ffmpeg_error", DescribeAvError(copyResult) },
            });
        avcodec_parameters_free(&parameters);
        return false;
    }

    const AVCodec* codec = avcodec_find_decoder(parameters->codec_id);
    if (!codec)
    {
        Logger::Instance().Log(
            LogLevel::Error,
            "decode",
            "hw_fallback.decoder_missing",
            "Software fallback could not find a matching decoder");
        avcodec_parameters_free(&parameters);
        return false;
    }

    if (resources_.codec)
        avcodec_free_context(&resources_.codec);
    if (resources_.hwDeviceCtx)
        av_buffer_unref(&resources_.hwDeviceCtx);
    ResetScalerState(resources_);

    state_.hardwareDecodeEnabled.store(false);
    state_.isHwAccel.store(false);
    state_.pixFmt.store(-1);
    state_.colorSpace.store(-1);
    state_.colorRange.store(-1);
    state_.colorTrc.store(-1);
    resources_.loggedFirstDecodedFrame = false;

    resources_.codec = avcodec_alloc_context3(codec);
    if (!resources_.codec)
    {
        Logger::Instance().Log(
            LogLevel::Error,
            "decode",
            "hw_fallback.context_alloc_failed",
            "Software fallback could not allocate a new AVCodecContext");
        avcodec_parameters_free(&parameters);
        return false;
    }

    const int contextResult = avcodec_parameters_to_context(resources_.codec, parameters);
    if (contextResult < 0)
    {
        Logger::Instance().Log(
            LogLevel::Error,
            "decode",
            "hw_fallback.context_copy_failed",
            "Software fallback could not restore codec parameters into the new decoder context",
            {
                { "ffmpeg_error", DescribeAvError(contextResult) },
            });
        avcodec_parameters_free(&parameters);
        avcodec_free_context(&resources_.codec);
        return false;
    }

    resources_.codec->thread_count = 8;
    const int openResult = avcodec_open2(resources_.codec, nullptr, nullptr);
    avcodec_parameters_free(&parameters);
    if (openResult != 0)
    {
        Logger::Instance().Log(
            LogLevel::Error,
            "decode",
            "hw_fallback.open_failed",
            "Software fallback failed while reopening the decoder",
            {
                { "ffmpeg_error", DescribeAvError(openResult) },
            });
        avcodec_free_context(&resources_.codec);
        return false;
    }

    if (resources_.codec->codec)
        state_.codecName = resources_.codec->codec->name;
    if (state_.isAudio)
        state_.sampleFmtOut.store(resources_.codec->sample_fmt);

    Logger::Instance().Log(
        LogLevel::Info,
        "decode",
        "hw_fallback.success",
        "Switched the current stream from CUDA hardware decode to software decode",
        {
            { "phase", phase ? phase : "unknown" },
            { "codec", state_.codecName },
        });
    return true;
}

void DecoderContextController::ResetReportState()
{

    state_.pts.store(0);
    state_.codecName.clear();
    state_.isHwAccel.store(false);
    state_.pixFmt.store(-1);
    state_.colorSpace.store(-1);
    state_.colorRange.store(-1);
    state_.colorTrc.store(-1);
    state_.sampleFmtOut.store(-1);
}

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
#include <libavutil/hwcontext.h>
#include <libavutil/pixdesc.h>
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>
}

FrameSurfaceAdapter::FrameSurfaceAdapter(DecodeState& state, DecodeResources& resources)
    : state_(state)
    , resources_(resources)
{
}

AVFrame* FrameSurfaceAdapter::Adapt(AVFrame* frame)
{

    if (!frame)
        return nullptr;

    if (frame->format == AV_PIX_FMT_CUDA)
    {

        state_.pts.store(frame->pts);

        UpdateCudaState(frame);

        return frame;
    }

    if (!state_.isAudio && !IsRendererNative(frame->format))
    {

        frame = ConvertSoftwareVideoFrame(frame);
        if (!frame)
            return nullptr;
    }

    state_.pts.store(frame->pts);
    if (!state_.isAudio)
        UpdateVideoState(frame);

    return frame;
}

bool FrameSurfaceAdapter::IsRendererNative(int pixelFormat)
{
    const AVPixelFormat format = static_cast<AVPixelFormat>(pixelFormat);

    return format == AV_PIX_FMT_YUV420P ||
        format == AV_PIX_FMT_NV12 ||
        format == AV_PIX_FMT_P010LE ||
        format == AV_PIX_FMT_YUV420P10LE;
}

void FrameSurfaceAdapter::UpdateVideoState(const AVFrame* frame) const
{
    if (!frame)
        return;

    state_.pixFmt.store(frame->format);
    state_.colorSpace.store(frame->colorspace);
    state_.colorRange.store(frame->color_range);
    state_.colorTrc.store(frame->color_trc);
}

void FrameSurfaceAdapter::UpdateCudaState(const AVFrame* frame) const
{
    if (!frame)
        return;

    if (frame->hw_frames_ctx)
    {

        const auto* hwContext = reinterpret_cast<const AVHWFramesContext*>(frame->hw_frames_ctx->data);

        state_.pixFmt.store(hwContext->sw_format);
    }

    state_.colorSpace.store(frame->colorspace);
    state_.colorRange.store(frame->color_range);
    state_.colorTrc.store(frame->color_trc);
}

AVFrame* FrameSurfaceAdapter::ConvertSoftwareVideoFrame(AVFrame* frame)
{
    if (!frame || state_.isAudio)
        return frame;

    const AVPixFmtDescriptor* descriptor =
        av_pix_fmt_desc_get(static_cast<AVPixelFormat>(frame->format));
    const int bitDepth = descriptor ? descriptor->comp[0].depth : 8;

    const AVPixelFormat targetFormat = bitDepth > 8 ? AV_PIX_FMT_YUV420P10LE : AV_PIX_FMT_YUV420P;

    std::lock_guard<std::mutex> lock(resources_.mux);

    if (!resources_.swsCtx ||
        resources_.swsWidth != frame->width ||
        resources_.swsHeight != frame->height ||
        resources_.swsSrcFmt != frame->format)
    {
        if (resources_.swsCtx)
            sws_freeContext(resources_.swsCtx);

        resources_.swsCtx = sws_getContext(
            frame->width,
            frame->height,
            static_cast<AVPixelFormat>(frame->format),
            frame->width,
            frame->height,
            targetFormat,
            SWS_BILINEAR,
            nullptr, nullptr, nullptr);

        if (!resources_.swsCtx)
            return frame;

        resources_.swsWidth = frame->width;
        resources_.swsHeight = frame->height;
        resources_.swsSrcFmt = frame->format;
    }

    AVFrame* converted = av_frame_alloc();
    if (!converted)
        return frame;

    converted->format = targetFormat;
    converted->width = frame->width;
    converted->height = frame->height;

    if (av_frame_get_buffer(converted, 32) < 0)
    {
        av_frame_free(&converted);
        return frame;
    }

    if (sws_scale(resources_.swsCtx,
        frame->data,
        frame->linesize,
        0,
        frame->height,
        converted->data,
        converted->linesize) < 0)
    {
        av_frame_free(&converted);
        return frame;
    }

    converted->pts = frame->pts;
    converted->color_primaries = frame->color_primaries;
    converted->color_trc = frame->color_trc;
    converted->colorspace = frame->colorspace;
    converted->color_range = frame->color_range;

    av_frame_free(&frame);

    return converted;
}
