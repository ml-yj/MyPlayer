#pragma once

#include <mutex>

#include <QOpenGLBuffer>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>

class Anime4KRenderStage;
class ColorPipeline;
class CudaInteropBridge;
struct VideoFrameState;

class IVideoRenderer
{
public:
    virtual ~IVideoRenderer() = default;

    virtual bool Initialize() = 0;
    virtual void Cleanup() = 0;
    virtual void Resize(int width, int height) = 0;
    virtual QOpenGLFunctions_3_3_Core* Functions() = 0;
    virtual void Render(
        VideoFrameState& state,
        CudaInteropBridge& cudaInterop,
        Anime4KRenderStage& anime4kStage,
        const ColorPipeline& colorPipeline,
        int widgetWidth,
        int widgetHeight,
        qreal devicePixelRatio,
        unsigned int defaultFramebuffer) = 0;
};

class ColorPipeline
{
public:
    struct Snapshot
    {
        float brightness = 0.0f;
        float contrast = 1.0f;
        float saturation = 1.0f;
    };

    static void GetColorParams(int colorspace, int colorRange, bool isHdr,
        float matOut[9], float offsetOut[3], float& hdrPeak);

    void SetBrightness(float value);
    void SetContrast(float value);
    void SetSaturation(float value);
    void SetBCS(float brightness, float contrast, float saturation);
    Snapshot GetSnapshot() const;

private:
    mutable std::mutex mux_;
    Snapshot values_;
};

class GLVideoRenderer final : public IVideoRenderer, protected QOpenGLFunctions_3_3_Core
{
public:
    GLVideoRenderer();
    ~GLVideoRenderer() override = default;

    bool Initialize() override;
    void Cleanup() override;
    void Resize(int width, int height) override;
    QOpenGLFunctions_3_3_Core* Functions() override;
    void Render(VideoFrameState& state,
        CudaInteropBridge& cudaInterop,
        Anime4KRenderStage& anime4kStage,
        const ColorPipeline& colorPipeline,
        int widgetWidth,
        int widgetHeight,
        qreal devicePixelRatio,
        unsigned int defaultFramebuffer) override;

private:
    void RenderWithBoundTextures(const VideoFrameState& state, const ColorPipeline& colorPipeline, bool semiPlanar);
    bool RenderCuda(VideoFrameState& state, CudaInteropBridge& cudaInterop, const ColorPipeline& colorPipeline);
    void RenderCpu(VideoFrameState& state, const ColorPipeline& colorPipeline);

    bool initialized_ = false;
    QOpenGLShaderProgram program_;
    QOpenGLVertexArrayObject vao_;
    QOpenGLBuffer vboVertices_;
    QOpenGLBuffer vboTexCoords_;
    GLuint texs_[3] = { 0, 0, 0 };

    GLint uTexY_ = -1;
    GLint uTexU_ = -1;
    GLint uTexV_ = -1;
    GLint uSemiPlanar_ = -1;
    GLint uColorMat_ = -1;
    GLint uYuvOffset_ = -1;
    GLint uDoToneMap_ = -1;
    GLint uHdrPeak_ = -1;
    GLint uBrightness_ = -1;
    GLint uContrast_ = -1;
    GLint uSaturation_ = -1;
};
