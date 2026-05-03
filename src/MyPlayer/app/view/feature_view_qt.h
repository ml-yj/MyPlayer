#pragma once

#include <QList>
#include <QPoint>
#include <QString>

#include <functional>

struct DetectionResult;
class QObject;
class QPushButton;
class QWidget;
class VideoWidget;

enum class DetectorMenuCommand
{
    None,
    LoadModel,
    LoadLabels,
    ClearLabels
};

class IFeatureView
{
public:
    virtual ~IFeatureView() = default;

    virtual void SetAsrToggleHandler(std::function<void()> handler) = 0;
    virtual void SetAnime4KToggleHandler(std::function<void()> handler) = 0;
    virtual void SetDetectorToggleHandler(std::function<void()> handler) = 0;
    virtual void SetDetectorMenuHandler(std::function<void(const QPoint&)> handler) = 0;
    virtual QList<QWidget*> AutoHideWidgets() const = 0;
    virtual void InstallEventFilters(QObject* filter) const = 0;
    virtual QObject* ContextObject() const = 0;
    virtual QPushButton* AsrButton() const = 0;
    virtual QPushButton* Anime4KButton() const = 0;
    virtual QPushButton* DetectorButton() const = 0;
    virtual void SetAsrChecked(bool checked) = 0;
    virtual void SetAnime4KChecked(bool checked) = 0;
    virtual void SetDetectorChecked(bool checked) = 0;
    virtual void SetAiButtonsEnabled(bool enabled) = 0;
    virtual void SetAnime4KEnabled(bool enabled) = 0;
    virtual bool IsAnime4KEnabled() const = 0;
    virtual QString Anime4KBackendSummary() const = 0;
    virtual void SetDetectionOverlayEnabled(bool enabled) = 0;
    virtual void UpdateDetections(const DetectionResult& result) = 0;
    virtual void ClearDetections() = 0;
    virtual void ShowAnime4KOsd(const QString& text) = 0;
    virtual void HideAnime4KOsd() = 0;
    virtual void ShowDetectorOsd(const QString& text) = 0;
    virtual void HideDetectorOsd() = 0;
    virtual void RelayoutOsd(int videoWidth, int hostHeight) = 0;
    virtual void ShowWarning(const QString& title, const QString& text) = 0;
    virtual DetectorMenuCommand ShowDetectorMenu(
        const QPoint& pos,
        const QString& modelStatus,
        const QString& labelsStatus,
        bool canClearLabels) = 0;
    virtual QString SelectDetectorModelPath(const QString& initialDir) = 0;
    virtual QString SelectDetectorLabelsPath(const QString& initialDir) = 0;
};

class FeatureViewQt final : public IFeatureView
{
public:
    FeatureViewQt(QWidget* host, VideoWidget* videoWidget);

    void SetAsrToggleHandler(std::function<void()> handler) override;
    void SetAnime4KToggleHandler(std::function<void()> handler) override;
    void SetDetectorToggleHandler(std::function<void()> handler) override;
    void SetDetectorMenuHandler(std::function<void(const QPoint&)> handler) override;
    QList<QWidget*> AutoHideWidgets() const override;
    void InstallEventFilters(QObject* filter) const override;
    QObject* ContextObject() const override;
    QPushButton* AsrButton() const override;
    QPushButton* Anime4KButton() const override;
    QPushButton* DetectorButton() const override;
    void SetAsrChecked(bool checked) override;
    void SetAnime4KChecked(bool checked) override;
    void SetDetectorChecked(bool checked) override;
    void SetAiButtonsEnabled(bool enabled) override;
    void SetAnime4KEnabled(bool enabled) override;
    bool IsAnime4KEnabled() const override;
    QString Anime4KBackendSummary() const override;
    void SetDetectionOverlayEnabled(bool enabled) override;
    void UpdateDetections(const DetectionResult& result) override;
    void ClearDetections() override;
    void ShowAnime4KOsd(const QString& text) override;
    void HideAnime4KOsd() override;
    void ShowDetectorOsd(const QString& text) override;
    void HideDetectorOsd() override;
    void RelayoutOsd(int videoWidth, int hostHeight) override;
    void ShowWarning(const QString& title, const QString& text) override;
    DetectorMenuCommand ShowDetectorMenu(
        const QPoint& pos,
        const QString& modelStatus,
        const QString& labelsStatus,
        bool canClearLabels) override;
    QString SelectDetectorModelPath(const QString& initialDir) override;
    QString SelectDetectorLabelsPath(const QString& initialDir) override;

private:
    QWidget* host_ = nullptr;
    VideoWidget* videoWidget_ = nullptr;
    QPushButton* asrButton_ = nullptr;
    QPushButton* anime4kButton_ = nullptr;
    QPushButton* detectorButton_ = nullptr;
    std::function<void()> asrToggleHandler_;
    std::function<void()> anime4kToggleHandler_;
    std::function<void()> detectorToggleHandler_;
    std::function<void(const QPoint&)> detectorMenuHandler_;
};
