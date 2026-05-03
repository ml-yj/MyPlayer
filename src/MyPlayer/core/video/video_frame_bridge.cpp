

#include "video_frame_bridge.h"

#include "../../common/diagnostics/logger.h"

#include <cstring>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/hwcontext.h>
#include <libavutil/pixfmt.h>
}

#include "cuda_interop_manager.h"

namespace {

bool IsHdrFrame(const AVFrame* frame)
{
    return frame->color_trc == AVCOL_TRC_SMPTE2084 ||
        frame->color_trc == AVCOL_TRC_ARIB_STD_B67 ||
        frame->color_trc == AVCOL_TRC_BT2020_10;
}

}

bool IsSemiPlanarFormat(int fmt)
{
    return fmt == AV_PIX_FMT_NV12 || fmt == AV_PIX_FMT_P010LE;
}

bool Is10BitFormat(int fmt)
{
    return fmt == AV_PIX_FMT_P010LE || fmt == AV_PIX_FMT_YUV420P10LE;
}

void VideoFrameState::ClearCpuBuffers()
{
    for (int i = 0; i < 3; ++i)
    {
        delete[] datas[i];
        datas[i] = nullptr;
        bufBytes[i] = 0;
    }
}

void VideoFrameState::ResetForStream(int newWidth, int newHeight)
{
    ClearCpuBuffers();
    width = newWidth;
    height = newHeight;
    curFmt = -1;
    curColorspace = -1;
    curColorRange = -1;
    curIsHDR = false;
    isInited = false;
    texNeedRebuild = true;
    loggedFirstAcceptedFrame = false;
    loggedCudaTransferFallback = false;
}

VideoFrameBridge::~VideoFrameBridge()
{
    std::lock_guard<std::mutex> lock(mux_);
    state_.ClearCpuBuffers();
}

std::mutex& VideoFrameBridge::mutex()
{
    return mux_;
}

std::mutex& VideoFrameBridge::mutex() const
{
    return const_cast<std::mutex&>(mux_);
}

VideoFrameState& VideoFrameBridge::state()
{
    return state_;
}

const VideoFrameState& VideoFrameBridge::state() const
{
    return state_;
}

void VideoFrameBridge::InitState(int width, int height)
{
    if (width <= 0 || height <= 0)
        return;

    std::lock_guard<std::mutex> lock(mux_);
    state_.ResetForStream(width, height);
}

void VideoFrameBridge::SetClosing(bool closing)
{
    std::lock_guard<std::mutex> lock(mux_);
    state_.isClosing = closing;
}

bool CopyVideoFrameToState(const AVFrame* sourceFrame, VideoFrameState& state)
{
    if (!sourceFrame)
        return false;

    const int frameWidth = sourceFrame->width;
    const int frameHeight = sourceFrame->height;
    if (frameWidth <= 0 || frameHeight <= 0)
        return false;

    const int fmt = sourceFrame->format;
    const bool semiPlanar = IsSemiPlanarFormat(fmt);
    const bool bit10 = Is10BitFormat(fmt);
    const int bytesPerPixel = bit10 ? 2 : 1;
    const bool formatChanged =
        frameWidth != state.width ||
        frameHeight != state.height ||
        fmt != state.curFmt;

    if (formatChanged)
    {
        state.ClearCpuBuffers();
        state.width = frameWidth;
        state.height = frameHeight;
        state.curFmt = fmt;
        state.texNeedRebuild = true;

        const int yBytes = frameWidth * frameHeight * bytesPerPixel;
        state.bufBytes[0] = yBytes;
        state.datas[0] = new unsigned char[yBytes];

        const int uvPixels = (frameWidth / 2) * (frameHeight / 2);
        if (semiPlanar)
        {
            state.bufBytes[1] = uvPixels * 2 * bytesPerPixel;
            state.datas[1] = new unsigned char[state.bufBytes[1]];
        }
        else
        {
            const int planeBytes = uvPixels * bytesPerPixel;
            state.bufBytes[1] = planeBytes;
            state.bufBytes[2] = planeBytes;
            state.datas[1] = new unsigned char[planeBytes];
            state.datas[2] = new unsigned char[planeBytes];
        }
    }

    state.curColorspace = sourceFrame->colorspace;
    state.curColorRange = sourceFrame->color_range;
    state.curIsHDR = IsHdrFrame(sourceFrame);
    state.isInited = true;

    const int yRowBytes = frameWidth * bytesPerPixel;
    if (sourceFrame->linesize[0] == yRowBytes)
    {
        std::memcpy(state.datas[0], sourceFrame->data[0], yRowBytes * frameHeight);
    }
    else
    {
        for (int i = 0; i < frameHeight; ++i)
        {
            std::memcpy(state.datas[0] + yRowBytes * i,
                sourceFrame->data[0] + sourceFrame->linesize[0] * i, yRowBytes);
        }
    }

    if (state.curFmt == AV_PIX_FMT_YUV420P10LE)
    {
        auto* plane = reinterpret_cast<uint16_t*>(state.datas[0]);
        const int count = frameWidth * frameHeight;
        for (int i = 0; i < count; ++i)
            plane[i] <<= 6;
    }

    if (semiPlanar && state.datas[1])
    {
        const int uvWidth = frameWidth / 2;
        const int uvHeight = frameHeight / 2;
        const int uvRowBytes = uvWidth * 2 * bytesPerPixel;
        if (sourceFrame->linesize[1] == uvRowBytes)
        {
            std::memcpy(state.datas[1], sourceFrame->data[1], uvRowBytes * uvHeight);
        }
        else
        {
            for (int i = 0; i < uvHeight; ++i)
            {
                std::memcpy(state.datas[1] + uvRowBytes * i,
                    sourceFrame->data[1] + sourceFrame->linesize[1] * i, uvRowBytes);
            }
        }
    }
    else
    {
        const int uvWidth = frameWidth / 2;
        const int uvHeight = frameHeight / 2;
        const int uvRowBytes = uvWidth * bytesPerPixel;

        if (state.datas[1])
        {
            if (sourceFrame->linesize[1] == uvRowBytes)
            {
                std::memcpy(state.datas[1], sourceFrame->data[1], uvRowBytes * uvHeight);
            }
            else
            {
                for (int i = 0; i < uvHeight; ++i)
                {
                    std::memcpy(state.datas[1] + uvRowBytes * i,
                        sourceFrame->data[1] + sourceFrame->linesize[1] * i, uvRowBytes);
                }
            }

            if (state.curFmt == AV_PIX_FMT_YUV420P10LE)
            {
                auto* plane = reinterpret_cast<uint16_t*>(state.datas[1]);
                const int count = uvWidth * uvHeight;
                for (int i = 0; i < count; ++i)
                    plane[i] <<= 6;
            }
        }

        if (state.datas[2])
        {
            if (sourceFrame->linesize[2] == uvRowBytes)
            {
                std::memcpy(state.datas[2], sourceFrame->data[2], uvRowBytes * uvHeight);
            }
            else
            {
                for (int i = 0; i < uvHeight; ++i)
                {
                    std::memcpy(state.datas[2] + uvRowBytes * i,
                        sourceFrame->data[2] + sourceFrame->linesize[2] * i, uvRowBytes);
                }
            }

            if (state.curFmt == AV_PIX_FMT_YUV420P10LE)
            {
                auto* plane = reinterpret_cast<uint16_t*>(state.datas[2]);
                const int count = uvWidth * uvHeight;
                for (int i = 0; i < count; ++i)
                    plane[i] <<= 6;
            }
        }
    }

    return true;
}

bool VideoFrameBridge::IngestFrame(AVFrame* frame, CudaInteropBridge& cudaInterop)
{
    if (!frame)
        return false;

    AVFrame* cpuFallbackFrame = nullptr;
    auto releaseFallbackFrame = [&]() {
        if (cpuFallbackFrame)
            av_frame_free(&cpuFallbackFrame);
    };

    std::lock_guard<std::mutex> lock(mux_);
    if (state_.isClosing)
        return false;

    const AVFrame* sourceFrame = frame;
    if (frame->format == AV_PIX_FMT_CUDA)
    {
        if (cudaInterop.HasContext() && cudaInterop.PrepareFrame(frame, state_))
            return true;

        if (!state_.loggedCudaTransferFallback)
        {
            Logger::Instance().Log(
                LogLevel::Warning,
                "video",
                "frame.cuda_transfer_fallback",
                "CUDA frame could not use zero-copy interop; falling back to CPU transfer for rendering",
                {
                    { "has_cuda_context", cudaInterop.HasContext() ? "true" : "false" },
                });
            state_.loggedCudaTransferFallback = true;
        }

        cpuFallbackFrame = av_frame_alloc();
        if (!cpuFallbackFrame)
            return false;
        if (av_hwframe_transfer_data(cpuFallbackFrame, frame, 0) < 0)
        {
            releaseFallbackFrame();
            return false;
        }
        if (av_frame_copy_props(cpuFallbackFrame, frame) < 0)
        {
            releaseFallbackFrame();
            return false;
        }
        sourceFrame = cpuFallbackFrame;
    }

    const bool copied = CopyVideoFrameToState(sourceFrame, state_);

    releaseFallbackFrame();
    if (!copied)
        return false;

    if (!state_.loggedFirstAcceptedFrame)
    {
        Logger::Instance().Log(
            LogLevel::Info,
            "video",
            "frame.accepted",
            "Video frame accepted by VideoFrameBridge",
            {
                { "width", std::to_string(state_.width) },
                { "height", std::to_string(state_.height) },
                { "pixel_format", std::to_string(state_.curFmt) },
            });
        state_.loggedFirstAcceptedFrame = true;
    }
    return true;
}

bool VideoFrameBridge::RequestFrameUpdate()
{
    frameDirty_.store(true, std::memory_order_relaxed);
    return !frameUpdateQueued_.exchange(true, std::memory_order_acq_rel);
}

bool VideoFrameBridge::ScheduleFrameUpdateIfDirty()
{
    if (!frameDirty_.load(std::memory_order_relaxed))
        return false;
    return !frameUpdateQueued_.exchange(true, std::memory_order_acq_rel);
}

bool VideoFrameBridge::ConsumePendingUpdate()
{
    frameUpdateQueued_.store(false, std::memory_order_release);
    return frameDirty_.exchange(false, std::memory_order_acq_rel);
}

void VideoFrameBridge::MarkPaintStarted()
{
    frameDirty_.store(false, std::memory_order_relaxed);
}
