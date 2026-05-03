

#include "cuda_interop_manager.h"

#include <QDebug>
#include <QOpenGLFunctions_3_3_Core>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_cuda.h>
#include <libavutil/pixfmt.h>
}

#include <cuda.h>
#include <cudaGL.h>

#include "video_frame_bridge.h"

namespace {

bool IsHdrFrame(const AVFrame* frame)
{
    return frame->color_trc == AVCOL_TRC_SMPTE2084 ||
        frame->color_trc == AVCOL_TRC_ARIB_STD_B67 ||
        frame->color_trc == AVCOL_TRC_BT2020_10;
}

}

CudaInteropBridge::~CudaInteropBridge()
{
    ReleasePendingFrame();
}

void CudaInteropBridge::SetContext(void* ctx)
{
    cudaCtx_ = ctx;
    if (!ctx)
    {
        cudaResources_[0] = nullptr;
        cudaResources_[1] = nullptr;
        cudaInteropRegistered_ = false;
        cudaInteropAvailable_ = true;
        cudaTexturesValid_ = false;
        ReleasePendingFrame();
        qDebug() << "XVideoWidget: CUDA context cleared, interop resources invalidated";
    }
    else
    {
        cudaInteropAvailable_ = true;
        qDebug() << "XVideoWidget: CUDA context set for zero-copy rendering";
    }
}

void CudaInteropBridge::ResetForStream()
{
    cudaCtx_ = nullptr;
    cudaResources_[0] = nullptr;
    cudaResources_[1] = nullptr;
    cudaInteropRegistered_ = false;
    cudaInteropAvailable_ = true;
    cudaTexturesValid_ = false;
    cudaSwFormat_ = -1;
    ReleasePendingFrame();
}

void CudaInteropBridge::Cleanup()
{
    UnregisterResources();
    ReleasePendingFrame();
    cudaCtx_ = nullptr;
    cudaSwFormat_ = -1;
    cudaTexturesValid_ = false;
    cudaInteropAvailable_ = true;
}

void CudaInteropBridge::InvalidateGlResources()
{
    UnregisterResources();
    ReleasePendingFrame();
    cudaTexturesValid_ = false;
}

bool CudaInteropBridge::HasContext() const
{
    return cudaCtx_ != nullptr;
}

bool CudaInteropBridge::HasPendingFrame() const
{
    return pendingCudaFrame_ != nullptr;
}

bool CudaInteropBridge::HasValidTextures() const
{
    return cudaTexturesValid_;
}

int CudaInteropBridge::CurrentSwFormat() const
{
    return cudaSwFormat_;
}

bool CudaInteropBridge::PrepareFrame(AVFrame* frame, VideoFrameState& state)
{
    if (!frame || !cudaCtx_ || !cudaInteropAvailable_ || !frame->hw_frames_ctx)
        return false;

    auto* framesContext = reinterpret_cast<AVHWFramesContext*>(frame->hw_frames_ctx->data);
    if (!framesContext)
        return false;

    const int swFormat = framesContext->sw_format;
    const int frameWidth = frame->width;
    const int frameHeight = frame->height;
    const bool formatChanged =
        swFormat != cudaSwFormat_ ||
        frameWidth != state.width ||
        frameHeight != state.height;
    if (formatChanged)
    {
        state.width = frameWidth;
        state.height = frameHeight;
        state.curFmt = swFormat;
        state.texNeedRebuild = true;
        state.isInited = true;
        cudaSwFormat_ = swFormat;
    }

    state.curColorspace = frame->colorspace;
    state.curColorRange = frame->color_range;
    state.curIsHDR = IsHdrFrame(frame);

    ReleasePendingFrame();
    pendingCudaFrame_ = av_frame_clone(frame);
    return pendingCudaFrame_ != nullptr;
}

bool CudaInteropBridge::EnsureInteropTextures(QOpenGLFunctions_3_3_Core* gl, unsigned int texs[3], VideoFrameState& state)
{
    if (!state.texNeedRebuild)
        return true;
    if (!cudaCtx_)
        return false;

    const bool bit10 = Is10BitFormat(state.curFmt);
    const GLenum internalY = bit10 ? GL_R16 : GL_R8;
    const GLenum internalUV = bit10 ? GL_RG16 : GL_RG8;
    const GLenum typeValue = bit10 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_BYTE;
    const int uvWidth = state.width / 2;
    const int uvHeight = state.height / 2;

    UnregisterResources();
    cudaTexturesValid_ = false;

    gl->glDeleteTextures(3, texs);
    gl->glGenTextures(3, texs);
    for (int i = 0; i < 3; ++i)
    {
        gl->glBindTexture(GL_TEXTURE_2D, texs[i]);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    gl->glBindTexture(GL_TEXTURE_2D, texs[0]);
    gl->glTexImage2D(GL_TEXTURE_2D, 0, internalY,
        state.width, state.height, 0, GL_RED, typeValue, nullptr);
    gl->glBindTexture(GL_TEXTURE_2D, texs[1]);
    gl->glTexImage2D(GL_TEXTURE_2D, 0, internalUV,
        uvWidth, uvHeight, 0, GL_RG, typeValue, nullptr);

    if (cuCtxPushCurrent(reinterpret_cast<CUcontext>(cudaCtx_)) != CUDA_SUCCESS)
        return false;

    CUgraphicsResource resourceY = nullptr;
    CUgraphicsResource resourceUv = nullptr;
    CUresult result = cuGraphicsGLRegisterImage(&resourceY, texs[0], GL_TEXTURE_2D,
        CU_GRAPHICS_REGISTER_FLAGS_WRITE_DISCARD);
    if (result != CUDA_SUCCESS)
    {
        cuCtxPopCurrent(nullptr);
        qWarning("CUDA: Failed to register Y texture (error %d); switching to CPU texture upload", static_cast<int>(result));
        DisableInteropForCurrentContext();
        return false;
    }

    result = cuGraphicsGLRegisterImage(&resourceUv, texs[1], GL_TEXTURE_2D,
        CU_GRAPHICS_REGISTER_FLAGS_WRITE_DISCARD);
    if (result != CUDA_SUCCESS)
    {
        cuGraphicsUnregisterResource(resourceY);
        cuCtxPopCurrent(nullptr);
        qWarning("CUDA: Failed to register UV texture (error %d); switching to CPU texture upload", static_cast<int>(result));
        DisableInteropForCurrentContext();
        return false;
    }

    cudaResources_[0] = resourceY;
    cudaResources_[1] = resourceUv;
    cudaInteropRegistered_ = true;
    state.texNeedRebuild = false;
    cuCtxPopCurrent(nullptr);
    return true;
}

bool CudaInteropBridge::UploadPendingFrame(const VideoFrameState& state)
{
    if (!pendingCudaFrame_ || !cudaInteropRegistered_ || !cudaCtx_)
        return cudaTexturesValid_;

    const bool bit10 = Is10BitFormat(state.curFmt);
    const int bytesPerPixel = bit10 ? 2 : 1;
    const int uvHeight = state.height / 2;

    if (cuCtxPushCurrent(reinterpret_cast<CUcontext>(cudaCtx_)) != CUDA_SUCCESS)
        return false;

    CUgraphicsResource resources[2] = {
        reinterpret_cast<CUgraphicsResource>(cudaResources_[0]),
        reinterpret_cast<CUgraphicsResource>(cudaResources_[1])
    };
    cuGraphicsMapResources(2, resources, 0);

    CUarray yArray = nullptr;
    cuGraphicsSubResourceGetMappedArray(&yArray, resources[0], 0, 0);
    CUDA_MEMCPY2D copyY = {};
    copyY.srcMemoryType = CU_MEMORYTYPE_DEVICE;
    copyY.srcDevice = reinterpret_cast<CUdeviceptr>(pendingCudaFrame_->data[0]);
    copyY.srcPitch = pendingCudaFrame_->linesize[0];
    copyY.dstMemoryType = CU_MEMORYTYPE_ARRAY;
    copyY.dstArray = yArray;
    copyY.WidthInBytes = state.width * bytesPerPixel;
    copyY.Height = state.height;
    cuMemcpy2D(&copyY);

    CUarray uvArray = nullptr;
    cuGraphicsSubResourceGetMappedArray(&uvArray, resources[1], 0, 0);
    CUDA_MEMCPY2D copyUv = {};
    copyUv.srcMemoryType = CU_MEMORYTYPE_DEVICE;
    copyUv.srcDevice = reinterpret_cast<CUdeviceptr>(pendingCudaFrame_->data[1]);
    copyUv.srcPitch = pendingCudaFrame_->linesize[1];
    copyUv.dstMemoryType = CU_MEMORYTYPE_ARRAY;
    copyUv.dstArray = uvArray;
    copyUv.WidthInBytes = state.width * bytesPerPixel;
    copyUv.Height = uvHeight;
    cuMemcpy2D(&copyUv);

    cuGraphicsUnmapResources(2, resources, 0);
    cuCtxPopCurrent(nullptr);

    ReleasePendingFrame();
    cudaTexturesValid_ = true;
    return true;
}

bool CudaInteropBridge::TransferPendingFrameToState(VideoFrameState& state)
{
    if (!pendingCudaFrame_)
        return false;

    AVFrame* cpuFrame = av_frame_alloc();
    if (!cpuFrame)
        return false;

    const bool ok =
        av_hwframe_transfer_data(cpuFrame, pendingCudaFrame_, 0) >= 0 &&
        av_frame_copy_props(cpuFrame, pendingCudaFrame_) >= 0 &&
        CopyVideoFrameToState(cpuFrame, state);

    av_frame_free(&cpuFrame);
    if (ok)
    {
        ReleasePendingFrame();
        cudaTexturesValid_ = false;
        state.texNeedRebuild = true;
    }
    return ok;
}

void CudaInteropBridge::ReleasePendingFrame()
{
    if (pendingCudaFrame_)
        av_frame_free(&pendingCudaFrame_);
}

void CudaInteropBridge::UnregisterResources()
{
    if (!cudaInteropRegistered_ || !cudaCtx_)
        return;

    if (cuCtxPushCurrent(reinterpret_cast<CUcontext>(cudaCtx_)) != CUDA_SUCCESS)
        return;

    for (void*& resource : cudaResources_)
    {
        if (resource)
        {
            cuGraphicsUnregisterResource(reinterpret_cast<CUgraphicsResource>(resource));
            resource = nullptr;
        }
    }

    cuCtxPopCurrent(nullptr);
    cudaInteropRegistered_ = false;
}

void CudaInteropBridge::DisableInteropForCurrentContext()
{
    UnregisterResources();
    cudaInteropAvailable_ = false;
    cudaTexturesValid_ = false;
}
