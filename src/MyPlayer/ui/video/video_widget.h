#pragma once

#include <QOpenGLWidget>
#include <QString>
#include <QMetaObject>
#include <memory>

#include "../../core/video/video_callback.h"

class QResizeEvent;
class QMoveEvent;
class QPaintEvent;
class Anime4KRenderStage;
class CudaInteropBridge;
class ColorPipeline;
struct DetectionResult;
class IVideoRenderer;
class OverlayRenderer;
struct SubtitleTrack;
class VideoFrameBridge;

class VideoWidget : public QOpenGLWidget, public VideoCallback
{
    Q_OBJECT

public:
    explicit VideoWidget(QWidget* parent = nullptr);
    ~VideoWidget() override;

    std::shared_ptr<VideoCallback> CreateCallbackHandle() const;
    void Init(int width, int height) override;
    void Repaint(AVFrame* frame) override;
    void SetCudaContext(void* ctx) override;
    void SetClosing(bool closing) override;
    void PrepareForWindowTransfer();
    void QueueFrameUpdate();

signals:
    void ReceiveFrameSignal();

private slots:
    void UpdateSlot();
    void OnContextAboutToBeDestroyed();

protected:
    void initializeGL() override;
    void paintEvent(QPaintEvent* event) override;
    void paintGL() override;
    void resizeGL(int width, int height) override;
    void resizeEvent(QResizeEvent* event) override;
    void moveEvent(QMoveEvent* event) override;

public:
    void setAnime4KEnabled(bool enabled);
    void setPreferLiveRendering(bool enabled);
    bool isAnime4KEnabled() const;
    QString GetRenderBackendSummary() const;
    QString GetAnime4KBackendSummary() const;
    void getAnime4KOutputSize(int& outW, int& outH) const;

    void setBrightness(float value);
    void setContrast(float value);
    void setSaturation(float value);
    void setBCS(float brightness, float contrast, float saturation);
    float getBrightness() const;
    float getContrast() const;
    float getSaturation() const;

    void setSubtitleTrack(const SubtitleTrack* track);
    void clearSubtitleTrack();
    void setSubtitleStyle(int fontPointSize, int bottomMarginPx);
    void setSubtitleClock(long long clockMs, long long offsetMs);

    void setDetectionOverlay(bool enabled);
    void updateDetections(const DetectionResult& result);
    void setDebugOverlayVisible(bool visible);
    bool isDebugOverlayVisible() const;
    void setDebugOverlayText(const QString& text);
    void showSubtitleStatusOsd(const QString& text);
    void hideSubtitleStatusOsd();
    void showFeatureStatusOsd(const QString& text);
    void hideFeatureStatusOsd();
    void showDisplayStatusOsd(const QString& text);
    void hideDisplayStatusOsd();

private:
    void CleanupGlResources(bool shuttingDown);
    void MarkTextureRebuildRequired();
    void RequestFrameUpdate();
    void ScheduleFrameUpdateIfDirty();

    std::shared_ptr<VideoFrameBridge> frameBridge_;
    std::unique_ptr<ColorPipeline> colorPipeline_;
    std::unique_ptr<OverlayRenderer> overlayRenderer_;
    std::unique_ptr<Anime4KRenderStage> anime4KStage_;
    std::shared_ptr<CudaInteropBridge> cudaInteropBridge_;
    std::unique_ptr<IVideoRenderer> renderer_;
    QMetaObject::Connection contextCleanupConnection_;
    std::shared_ptr<VideoCallback> callbackHandle_;
    bool loggedFirstRenderablePaint_ = false;
};
