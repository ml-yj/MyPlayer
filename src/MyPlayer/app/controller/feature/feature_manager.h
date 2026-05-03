#pragma once

#include <QElapsedTimer>
#include <QList>
#include <QPoint>
#include <QString>
#include <functional>
#include <memory>

#include "../../../features/detector/detector_types.h"

class QObject;
class QPushButton;
class QWidget;
class IFeatureView;
class IPlaybackFeatureService;
class IPlaybackArchiveService;
class SubtitleController;
class ConfigService;

class FeatureManager
{
public:
    FeatureManager(std::unique_ptr<IFeatureView> view,
        IPlaybackFeatureService* featureService,
        IPlaybackArchiveService* archiveService,
        ConfigService* configService,
        SubtitleController* subtitleController,
        std::function<int()> videoAreaWidthProvider,
        std::function<int()> hostHeightProvider);

    QList<QWidget*> AutoHideWidgets() const;
    void InstallEventFilters(QObject* filter) const;
    void LoadDetectorSettings();
    void SaveDetectorSettings() const;
    void ResetForMedia();
    void TickOsd();
    void Relayout();
    void SetDetectorEnabled(bool enabled);

    bool IsAsrEnabled() const;
    bool IsDetectorEnabled() const;
    QPushButton* AsrButton() const;
    QPushButton* Anime4KButton() const;
    QPushButton* DetectorButton() const;

    void ToggleASR();
    void ToggleAnime4K();
    void ToggleDetector();
    void ShowDetectorMenu(const QPoint& pos);

private:
    QString ResolveDetectorModelPath() const;
    QString ResolveDetectorLabelsPath(bool* usedAuto = nullptr) const;
    bool StartDetector(QString* errorMessage = nullptr);
    void StopDetector();
    void RestartDetectorIfEnabled(const QString& osdText);
    QString DetectorStatusText(bool enabled) const;
    QString AsrStatusText(bool enabled) const;
    QString Anime4KStatusText(bool enabled) const;
    bool BeginAiToggle();
    void EndAiToggle();
    void UpdateAiToggleButtonsEnabled(bool enabled);
    void ShowA4kOsd(const QString& text);
    void ShowDetectorOsd(const QString& text);
    void OnDetectorModelReady(bool success);
    void HandleDetectionsReady(DetectionResult result);

    std::unique_ptr<IFeatureView> view;
    IPlaybackFeatureService* features = nullptr;
    IPlaybackArchiveService* archive = nullptr;
    ConfigService* config = nullptr;
    SubtitleController* subtitles = nullptr;
    std::function<int()> videoAreaWidthProvider;
    std::function<int()> hostHeightProvider;

    int anime4kOsdTimer = 0;
    int detectorOsdTimer = 0;
    bool asrEnabled = false;
    bool detectorEnabled = false;
    QString detectorModelPath;
    QString detectorLabelsPath;
    QElapsedTimer detectorEventCooldown;
    bool aiToggleBusy = false;
    QElapsedTimer aiToggleCooldown;
    int aiToggleCooldownMs = 500;
    int detectorEventCooldownMs = 2000;
};
