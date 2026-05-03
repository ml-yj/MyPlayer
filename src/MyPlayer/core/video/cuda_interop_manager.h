#pragma once

struct AVFrame;
struct VideoFrameState;
class QOpenGLFunctions_3_3_Core;

class CudaInteropBridge
{
public:

    CudaInteropBridge() = default;

    ~CudaInteropBridge();

    void SetContext(void* ctx);

    void ResetForStream();

    void Cleanup();

    void InvalidateGlResources();

    bool HasContext() const;
    bool HasPendingFrame() const;
    bool HasValidTextures() const;
    int CurrentSwFormat() const;

    bool PrepareFrame(AVFrame* frame, VideoFrameState& state);

    bool EnsureInteropTextures(QOpenGLFunctions_3_3_Core* gl, unsigned int texs[3], VideoFrameState& state);

    bool UploadPendingFrame(const VideoFrameState& state);
    bool TransferPendingFrameToState(VideoFrameState& state);

private:

    void ReleasePendingFrame();

    void UnregisterResources();
    void DisableInteropForCurrentContext();

    void* cudaCtx_ = nullptr;

    void* cudaResources_[2] = { nullptr, nullptr };

    bool cudaInteropRegistered_ = false;

    bool cudaTexturesValid_ = false;

    bool cudaInteropAvailable_ = true;

    AVFrame* pendingCudaFrame_ = nullptr;

    int cudaSwFormat_ = -1;
};
