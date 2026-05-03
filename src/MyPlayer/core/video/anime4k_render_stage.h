#pragma once

#include <atomic>

#include <QString>

#include "../../features/upscale/anime4k_pipeline.h"

class QOpenGLFunctions_3_3_Core;

class Anime4KRenderStage
{
public:
    Anime4KRenderStage() = default;
    ~Anime4KRenderStage() = default;

    void Initialize(QOpenGLFunctions_3_3_Core* gl, const QString& appDir);
    void Cleanup();
    void ReleaseWorkingSet();

    bool BeginFrame(int framebufferWidth, int framebufferHeight,
        int videoWidth, int videoHeight, GLuint defaultFramebuffer);
    void EndFrame(int framebufferWidth, int framebufferHeight,
        int videoWidth, int videoHeight, GLuint defaultFramebuffer);

    void SetEnabled(bool enabled);
    void SetPreferLiveRendering(bool enabled);
    bool IsEnabled() const;
    QString BackendSummary() const;
    void GetOutputSize(int inputWidth, int inputHeight, int& outWidth, int& outHeight) const;

private:
    void EnsureBlitResources();
    void EnsureInputTarget(int videoWidth, int videoHeight, bool& active);
    void ApplyViewport(int framebufferWidth, int framebufferHeight, int videoWidth, int videoHeight);

    QOpenGLFunctions_3_3_Core* gl_ = nullptr;
    Anime4KPipeline pipeline_;
    std::atomic<bool> preferLightweightLiveRender_{ false };
    bool frameActive_ = false;

    GLuint inputFbo_ = 0;
    GLuint inputTex_ = 0;
    int inputWidth_ = 0;
    int inputHeight_ = 0;

    GLuint blitProgram_ = 0;
    GLuint blitVao_ = 0;
    GLuint blitVbo_ = 0;
};
