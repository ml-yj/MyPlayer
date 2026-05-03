

#include "video_widget.h"

#include "../../core/video/anime4k_render_stage.h"
#include "../../core/video/cuda_interop_manager.h"
#include "../../core/video/video_frame_bridge.h"
#include "video_overlay_controller.h"
#include "../../core/video/video_renderer_gl.h"
#include "../../common/diagnostics/logger.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <QMoveEvent>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QPaintEvent>
#include <QPainter>
#include <QPointer>
#include <QResizeEvent>
#include <QSurfaceFormat>
#include <QThread>

namespace
{

class VideoWidgetCallbackHandle final : public VideoCallback
{
public:
    VideoWidgetCallbackHandle(
        const std::shared_ptr<VideoFrameBridge>& frameBridge,
        const std::shared_ptr<CudaInteropBridge>& cudaInteropBridge,
        VideoWidget* widget)
        : frameBridge_(frameBridge)
        , cudaInteropBridge_(cudaInteropBridge)
        , widget_(widget)
    {
    }

    void Init(int width, int height) override
    {
        if (width <= 0 || height <= 0)
            return;

        const auto frameBridge = frameBridge_.lock();
        const auto cudaInteropBridge = cudaInteropBridge_.lock();
        if (!frameBridge || !cudaInteropBridge)
            return;

        std::lock_guard<std::mutex> lock(frameBridge->mutex());
        frameBridge->state().ResetForStream(width, height);
        cudaInteropBridge->ResetForStream();
        if (widget_)
            widget_->QueueFrameUpdate();
    }

    void Repaint(AVFrame* frame) override
    {
        if (!frame)
            return;

        const auto frameBridge = frameBridge_.lock();
        const auto cudaInteropBridge = cudaInteropBridge_.lock();
        if (!frameBridge || !cudaInteropBridge)
            return;

        if (frameBridge->IngestFrame(frame, *cudaInteropBridge) && widget_)
            widget_->QueueFrameUpdate();
    }

    void SetCudaContext(void* ctx) override
    {
        const auto frameBridge = frameBridge_.lock();
        const auto cudaInteropBridge = cudaInteropBridge_.lock();
        if (!frameBridge || !cudaInteropBridge)
            return;

        std::lock_guard<std::mutex> lock(frameBridge->mutex());
        cudaInteropBridge->SetContext(ctx);
    }

    void SetClosing(bool closing) override
    {
        if (const auto frameBridge = frameBridge_.lock())
            frameBridge->SetClosing(closing);
    }

private:
    std::weak_ptr<VideoFrameBridge> frameBridge_;
    std::weak_ptr<CudaInteropBridge> cudaInteropBridge_;
    QPointer<VideoWidget> widget_;
};
}

VideoWidget::VideoWidget(QWidget* parent)
    : QOpenGLWidget(parent)
    , frameBridge_(std::make_shared<VideoFrameBridge>())
    , colorPipeline_(std::make_unique<ColorPipeline>())
    , overlayRenderer_(std::make_unique<OverlayRenderer>(this))
    , anime4KStage_(std::make_unique<Anime4KRenderStage>())
    , cudaInteropBridge_(std::make_shared<CudaInteropBridge>())
    , renderer_(std::make_unique<GLVideoRenderer>())
{
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    format.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
    setFormat(format);
    callbackHandle_ = std::make_shared<VideoWidgetCallbackHandle>(frameBridge_, cudaInteropBridge_, this);
}

VideoWidget::~VideoWidget()
{
    SetClosing(true);
    callbackHandle_.reset();
    CleanupGlResources(true);
}

std::shared_ptr<VideoCallback> VideoWidget::CreateCallbackHandle() const
{
    return callbackHandle_;
}

void VideoWidget::Init(int width, int height)
{
    if (width <= 0 || height <= 0)
        return;

    {
        std::lock_guard<std::mutex> lock(frameBridge_->mutex());
        frameBridge_->state().ResetForStream(width, height);
        cudaInteropBridge_->ResetForStream();
    }
    RequestFrameUpdate();
}

void VideoWidget::Repaint(AVFrame* frame)
{
    if (!frame)
        return;
    if (frameBridge_->IngestFrame(frame, *cudaInteropBridge_))
        RequestFrameUpdate();
}

void VideoWidget::SetCudaContext(void* ctx)
{
    std::lock_guard<std::mutex> lock(frameBridge_->mutex());
    cudaInteropBridge_->SetContext(ctx);
}

void VideoWidget::SetClosing(bool closing)
{
    frameBridge_->SetClosing(closing);
}

void VideoWidget::UpdateSlot()
{
    if (QThread::currentThread() != thread())
    {
        QMetaObject::invokeMethod(this, [this]() { UpdateSlot(); }, Qt::QueuedConnection);
        return;
    }

    if (!frameBridge_->ConsumePendingUpdate())
        return;
    update();
}

void VideoWidget::OnContextAboutToBeDestroyed()
{
    CleanupGlResources(false);
}

void VideoWidget::initializeGL()
{
    if (contextCleanupConnection_)
        QObject::disconnect(contextCleanupConnection_);
    if (context())
    {
        contextCleanupConnection_ = QObject::connect(
            context(),
            &QOpenGLContext::aboutToBeDestroyed,
            this,
            &VideoWidget::OnContextAboutToBeDestroyed,
            Qt::DirectConnection);
    }
    if (!renderer_->Initialize())
        return;
    anime4KStage_->Initialize(renderer_->Functions(), QCoreApplication::applicationDirPath());
}

void VideoWidget::paintEvent(QPaintEvent* event)
{
    QOpenGLWidget::paintEvent(event);

    if (!overlayRenderer_)
        return;

    QPainter painter(this);
    if (painter.isActive())
        overlayRenderer_->Paint(painter);
}

void VideoWidget::paintGL()
{
    {
        std::lock_guard<std::mutex> lock(frameBridge_->mutex());
        frameBridge_->MarkPaintStarted();
        const VideoFrameState& state = frameBridge_->state();
        if (!loggedFirstRenderablePaint_
            && state.isInited
            && (state.datas[0] || cudaInteropBridge_->HasPendingFrame() || cudaInteropBridge_->HasValidTextures()))
        {
            Logger::Instance().Log(
                LogLevel::Info,
                "video",
                "paint.first_renderable",
                "VideoWidget paintGL observed the first renderable frame",
                {
                    { "width", std::to_string(state.width) },
                    { "height", std::to_string(state.height) },
                    { "pixel_format", std::to_string(state.curFmt) },
                });
            loggedFirstRenderablePaint_ = true;
        }
        renderer_->Render(frameBridge_->state(), *cudaInteropBridge_, *anime4KStage_, *colorPipeline_,
            width(), height(), devicePixelRatioF(), defaultFramebufferObject());
    }

    ScheduleFrameUpdateIfDirty();
}

void VideoWidget::resizeGL(int width, int height)
{
    renderer_->Resize(width, height);
}

void VideoWidget::resizeEvent(QResizeEvent* event)
{
    QOpenGLWidget::resizeEvent(event);
    overlayRenderer_->OnHostResized();
}

void VideoWidget::moveEvent(QMoveEvent* event)
{
    QOpenGLWidget::moveEvent(event);
    overlayRenderer_->OnHostResized();
}

void VideoWidget::PrepareForWindowTransfer()
{
    if (QThread::currentThread() != thread())
    {
        QMetaObject::invokeMethod(this, [this]() { PrepareForWindowTransfer(); }, Qt::BlockingQueuedConnection);
        return;
    }

    CleanupGlResources(false);
}

void VideoWidget::QueueFrameUpdate()
{
    RequestFrameUpdate();
}

void VideoWidget::setAnime4KEnabled(bool enabled)
{
    if (QThread::currentThread() != thread())
    {
        QMetaObject::invokeMethod(this, [this, enabled]() { setAnime4KEnabled(enabled); }, Qt::QueuedConnection);
        return;
    }

    if (!enabled && anime4KStage_->IsEnabled() && isValid() && context())
    {
        makeCurrent();
        if (auto* current = QOpenGLContext::currentContext())
            current->functions()->glFinish();
        anime4KStage_->ReleaseWorkingSet();
        doneCurrent();
    }

    anime4KStage_->SetEnabled(enabled);
    RequestFrameUpdate();
}

void VideoWidget::setPreferLiveRendering(bool enabled)
{
    if (QThread::currentThread() != thread())
    {
        QMetaObject::invokeMethod(this, [this, enabled]() { setPreferLiveRendering(enabled); }, Qt::QueuedConnection);
        return;
    }

    anime4KStage_->SetPreferLiveRendering(enabled);
    RequestFrameUpdate();
}

bool VideoWidget::isAnime4KEnabled() const
{
    return anime4KStage_->IsEnabled();
}

QString VideoWidget::GetRenderBackendSummary() const
{
    if (!frameBridge_ || !cudaInteropBridge_)
        return "Unknown";

    std::lock_guard<std::mutex> lock(frameBridge_->mutex());
    const VideoFrameState& state = frameBridge_->state();
    if (!state.isInited)
        return "Pending";

    if (cudaInteropBridge_->HasContext()
        && (cudaInteropBridge_->HasPendingFrame() || cudaInteropBridge_->HasValidTextures()))
    {
        return "GPU (OpenGL + CUDA interop)";
    }

    return "GPU (OpenGL, CPU upload)";
}

QString VideoWidget::GetAnime4KBackendSummary() const
{
    return anime4KStage_->BackendSummary();
}

void VideoWidget::getAnime4KOutputSize(int& outW, int& outH) const
{
    std::lock_guard<std::mutex> lock(frameBridge_->mutex());
    anime4KStage_->GetOutputSize(frameBridge_->state().width, frameBridge_->state().height, outW, outH);
}

void VideoWidget::setBrightness(float value)
{
    if (QThread::currentThread() != thread())
    {
        QMetaObject::invokeMethod(this, [this, value]() { setBrightness(value); }, Qt::QueuedConnection);
        return;
    }

    colorPipeline_->SetBrightness(value);
    RequestFrameUpdate();
}

void VideoWidget::setContrast(float value)
{
    if (QThread::currentThread() != thread())
    {
        QMetaObject::invokeMethod(this, [this, value]() { setContrast(value); }, Qt::QueuedConnection);
        return;
    }

    colorPipeline_->SetContrast(value);
    RequestFrameUpdate();
}

void VideoWidget::setSaturation(float value)
{
    if (QThread::currentThread() != thread())
    {
        QMetaObject::invokeMethod(this, [this, value]() { setSaturation(value); }, Qt::QueuedConnection);
        return;
    }

    colorPipeline_->SetSaturation(value);
    RequestFrameUpdate();
}

void VideoWidget::setBCS(float brightness, float contrast, float saturation)
{
    if (QThread::currentThread() != thread())
    {
        QMetaObject::invokeMethod(
            this,
            [this, brightness, contrast, saturation]() { setBCS(brightness, contrast, saturation); },
            Qt::QueuedConnection);
        return;
    }

    colorPipeline_->SetBCS(brightness, contrast, saturation);
    RequestFrameUpdate();
}

float VideoWidget::getBrightness() const
{
    return colorPipeline_->GetSnapshot().brightness;
}

float VideoWidget::getContrast() const
{
    return colorPipeline_->GetSnapshot().contrast;
}

float VideoWidget::getSaturation() const
{
    return colorPipeline_->GetSnapshot().saturation;
}

void VideoWidget::setSubtitleTrack(const SubtitleTrack* track)
{
    if (QThread::currentThread() != thread())
    {
        auto trackCopy = track ? std::make_shared<SubtitleTrack>(*track) : std::shared_ptr<SubtitleTrack>{};
        QMetaObject::invokeMethod(
            this,
            [this, trackCopy]() { overlayRenderer_->SetSubtitleTrack(trackCopy ? trackCopy.get() : nullptr); },
            Qt::QueuedConnection);
        return;
    }

    overlayRenderer_->SetSubtitleTrack(track);
}

void VideoWidget::clearSubtitleTrack()
{
    if (QThread::currentThread() != thread())
    {
        QMetaObject::invokeMethod(this, [this]() { clearSubtitleTrack(); }, Qt::QueuedConnection);
        return;
    }

    overlayRenderer_->ClearSubtitleTrack();
}

void VideoWidget::setSubtitleStyle(int fontPointSize, int bottomMarginPx)
{
    if (QThread::currentThread() != thread())
    {
        QMetaObject::invokeMethod(
            this,
            [this, fontPointSize, bottomMarginPx]() { setSubtitleStyle(fontPointSize, bottomMarginPx); },
            Qt::QueuedConnection);
        return;
    }

    overlayRenderer_->SetSubtitleStyle(fontPointSize, bottomMarginPx);
}

void VideoWidget::setSubtitleClock(long long clockMs, long long offsetMs)
{
    if (QThread::currentThread() != thread())
    {
        QMetaObject::invokeMethod(
            this,
            [this, clockMs, offsetMs]() { setSubtitleClock(clockMs, offsetMs); },
            Qt::QueuedConnection);
        return;
    }

    overlayRenderer_->SetSubtitleClock(clockMs, offsetMs);
}

void VideoWidget::setDetectionOverlay(bool enabled)
{
    if (QThread::currentThread() != thread())
    {
        QMetaObject::invokeMethod(this, [this, enabled]() { setDetectionOverlay(enabled); }, Qt::QueuedConnection);
        return;
    }

    overlayRenderer_->SetDetectionOverlay(enabled);
}

void VideoWidget::updateDetections(const DetectionResult& result)
{
    if (QThread::currentThread() != thread())
    {
        QMetaObject::invokeMethod(this, [this, result]() { updateDetections(result); }, Qt::QueuedConnection);
        return;
    }

    overlayRenderer_->UpdateDetections(result);
}

void VideoWidget::setDebugOverlayVisible(bool visible)
{
    if (QThread::currentThread() != thread())
    {
        QMetaObject::invokeMethod(this, [this, visible]() { setDebugOverlayVisible(visible); }, Qt::QueuedConnection);
        return;
    }

    overlayRenderer_->SetDebugOverlayVisible(visible);
}

bool VideoWidget::isDebugOverlayVisible() const
{
    return overlayRenderer_ && overlayRenderer_->IsDebugOverlayVisible();
}

void VideoWidget::setDebugOverlayText(const QString& text)
{
    if (QThread::currentThread() != thread())
    {
        QMetaObject::invokeMethod(this, [this, text]() { setDebugOverlayText(text); }, Qt::QueuedConnection);
        return;
    }

    overlayRenderer_->SetDebugOverlayText(text);
}

void VideoWidget::showSubtitleStatusOsd(const QString& text)
{
    if (QThread::currentThread() != thread())
    {
        QMetaObject::invokeMethod(this, [this, text]() { showSubtitleStatusOsd(text); }, Qt::QueuedConnection);
        return;
    }

    overlayRenderer_->ShowSubtitleStatusOsd(text);
}

void VideoWidget::hideSubtitleStatusOsd()
{
    if (QThread::currentThread() != thread())
    {
        QMetaObject::invokeMethod(this, [this]() { hideSubtitleStatusOsd(); }, Qt::QueuedConnection);
        return;
    }

    overlayRenderer_->HideSubtitleStatusOsd();
}

void VideoWidget::showFeatureStatusOsd(const QString& text)
{
    if (QThread::currentThread() != thread())
    {
        QMetaObject::invokeMethod(this, [this, text]() { showFeatureStatusOsd(text); }, Qt::QueuedConnection);
        return;
    }

    overlayRenderer_->ShowFeatureStatusOsd(text);
}

void VideoWidget::hideFeatureStatusOsd()
{
    if (QThread::currentThread() != thread())
    {
        QMetaObject::invokeMethod(this, [this]() { hideFeatureStatusOsd(); }, Qt::QueuedConnection);
        return;
    }

    overlayRenderer_->HideFeatureStatusOsd();
}

void VideoWidget::showDisplayStatusOsd(const QString& text)
{
    if (QThread::currentThread() != thread())
    {
        QMetaObject::invokeMethod(this, [this, text]() { showDisplayStatusOsd(text); }, Qt::QueuedConnection);
        return;
    }

    overlayRenderer_->ShowDisplayStatusOsd(text);
}

void VideoWidget::hideDisplayStatusOsd()
{
    if (QThread::currentThread() != thread())
    {
        QMetaObject::invokeMethod(this, [this]() { hideDisplayStatusOsd(); }, Qt::QueuedConnection);
        return;
    }

    overlayRenderer_->HideDisplayStatusOsd();
}

void VideoWidget::RequestFrameUpdate()
{
    if (frameBridge_->RequestFrameUpdate())
        QMetaObject::invokeMethod(this, [this]() { UpdateSlot(); }, Qt::QueuedConnection);
}

void VideoWidget::ScheduleFrameUpdateIfDirty()
{
    if (frameBridge_->ScheduleFrameUpdateIfDirty())
        QMetaObject::invokeMethod(this, [this]() { UpdateSlot(); }, Qt::QueuedConnection);
}

void VideoWidget::CleanupGlResources(bool shuttingDown)
{
    if (contextCleanupConnection_)
        QObject::disconnect(contextCleanupConnection_);

    const bool hasContext = context() && isValid();
    if (hasContext)
        makeCurrent();

    if (hasContext)
    {
        cudaInteropBridge_->InvalidateGlResources();
        renderer_->Cleanup();
        anime4KStage_->Cleanup();
        doneCurrent();
    }
    else if (shuttingDown)
    {
        cudaInteropBridge_->Cleanup();
    }
    else
    {
        cudaInteropBridge_->InvalidateGlResources();
    }

    MarkTextureRebuildRequired();
}

void VideoWidget::MarkTextureRebuildRequired()
{
    std::lock_guard<std::mutex> lock(frameBridge_->mutex());
    frameBridge_->state().texNeedRebuild = true;
}
