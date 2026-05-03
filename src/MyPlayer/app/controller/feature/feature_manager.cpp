

#include "feature_manager.h"

#include "../../service/config_service.h"
#include "../../service/playback_service_interfaces.h"
#include "../../view/feature_view_qt.h"
#include "../subtitle/subtitle_controller.h"
#include "../../../core/ai/ai_types.h"

#include <algorithm>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPushButton>
#include <QSet>
#include <utility>

FeatureManager::FeatureManager(std::unique_ptr<IFeatureView> viewValue,
    IPlaybackFeatureService* featureService,
    IPlaybackArchiveService* archiveService,
    ConfigService* configService,
    SubtitleController* subtitleController,
    std::function<int()> videoWidthProvider,
    std::function<int()> heightProvider)
    : view(std::move(viewValue))
    , features(featureService)
    , archive(archiveService)
    , config(configService)
    , subtitles(subtitleController)
    , videoAreaWidthProvider(std::move(videoWidthProvider))
    , hostHeightProvider(std::move(heightProvider))
{
    if (view)
    {
        view->SetAsrToggleHandler([this]() { ToggleASR(); });
        view->SetAnime4KToggleHandler([this]() { ToggleAnime4K(); });
        view->SetDetectorToggleHandler([this]() { ToggleDetector(); });
        view->SetDetectorMenuHandler([this](const QPoint& pos) { ShowDetectorMenu(pos); });
    }
}

QList<QWidget*> FeatureManager::AutoHideWidgets() const
{
    return view ? view->AutoHideWidgets() : QList<QWidget*>{};
}

void FeatureManager::InstallEventFilters(QObject* filter) const
{
    if (view)
        view->InstallEventFilters(filter);
}

bool FeatureManager::IsAsrEnabled() const
{
    return asrEnabled;
}

bool FeatureManager::IsDetectorEnabled() const
{
    return detectorEnabled;
}

QPushButton* FeatureManager::AsrButton() const
{
    return view ? view->AsrButton() : nullptr;
}

QPushButton* FeatureManager::Anime4KButton() const
{
    return view ? view->Anime4KButton() : nullptr;
}

QPushButton* FeatureManager::DetectorButton() const
{
    return view ? view->DetectorButton() : nullptr;
}

void FeatureManager::HandleDetectionsReady(DetectionResult result)
{
    if (!detectorEnabled || !view)
        return;

    const StreamEpoch epoch = features ? features->GetFeatureEpoch(AiCapability::Detector) : StreamEpoch{};
    if (result.generation != 0
        && (result.generation != epoch.generation || result.serial != epoch.serial))
    {
        return;
    }

    view->UpdateDetections(result);

    if (!archive || !archive->IsRecording() || result.boxes.empty())
        return;

    if (detectorEventCooldown.isValid() && detectorEventCooldown.elapsed() < detectorEventCooldownMs)
        return;

    float maxConfidence = 0.0f;
    QSet<QString> labels;
    for (const DetectionBox& box : result.boxes)
    {
        maxConfidence = std::max(maxConfidence, box.confidence);
        if (!box.className.trimmed().isEmpty())
            labels.insert(box.className.trimmed());
    }

    QJsonArray labelArray;
    for (const QString& label : labels)
        labelArray.append(label);

    QJsonObject payload;
    payload.insert("count", static_cast<int>(result.boxes.size()));
    payload.insert("frame_width", result.frameWidth);
    payload.insert("frame_height", result.frameHeight);
    payload.insert("max_confidence", maxConfidence);
    payload.insert("labels", labelArray);

    ArchiveEventRecord event;
    event.occurredAtUtc = QDateTime::currentDateTimeUtc();
    event.type = "detector.detections";
    event.severity = (result.boxes.size() >= 3 || maxConfidence >= 0.9f)
        ? ArchiveEventSeverity::Alarm
        : ArchiveEventSeverity::Warning;
    event.payloadJson = QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact));

    std::string error;
    if (archive->RecordArchiveEvent(event, &error))
    {
        if (detectorEventCooldown.isValid())
            detectorEventCooldown.restart();
        else
            detectorEventCooldown.start();
    }
}

void FeatureManager::LoadDetectorSettings()
{
    const DetectorPreferences preferences = config ? config->LoadDetectorPreferences() : DetectorPreferences{};
    detectorModelPath = preferences.modelPath;
    detectorLabelsPath = preferences.labelsPath;
}

void FeatureManager::SaveDetectorSettings() const
{
    if (!config)
        return;

    config->SaveDetectorPreferences({ detectorModelPath, detectorLabelsPath });
}

QString FeatureManager::ResolveDetectorModelPath() const
{
    const QString configuredPath = QDir::cleanPath(detectorModelPath.trimmed());
    if (!configuredPath.isEmpty())
    {
        QFileInfo configuredInfo(configuredPath);
        if (configuredInfo.exists() && configuredInfo.isFile())
            return configuredInfo.absoluteFilePath();
    }

    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QDir(appDir).filePath("models/rtdetr-l.onnx"),
        QDir(appDir).filePath("../common/models/rtdetr-l.onnx"),
        QDir(QDir::currentPath()).filePath("models/rtdetr-l.onnx"),
        QDir(QDir::currentPath()).filePath("../common/models/rtdetr-l.onnx"),
        QDir(appDir).filePath("../../../../bin/models/rtdetr-l.onnx"),
        QDir(appDir).filePath("../../../../bin/common/models/rtdetr-l.onnx"),
        QDir(appDir).filePath("../../../bin/models/rtdetr-l.onnx"),
        QDir(appDir).filePath("../../../bin/common/models/rtdetr-l.onnx"),
        QDir(appDir).filePath("../../bin/models/rtdetr-l.onnx"),
        QDir(appDir).filePath("../../bin/common/models/rtdetr-l.onnx"),
        QDir(appDir).filePath("../bin/models/rtdetr-l.onnx")
    };

    for (const QString& candidate : candidates)
    {
        QFileInfo info(QDir::cleanPath(candidate));
        if (info.exists() && info.isFile())
            return info.absoluteFilePath();
    }

    return QString();
}

QString FeatureManager::ResolveDetectorLabelsPath(bool* usedAuto) const
{
    if (usedAuto)
        *usedAuto = false;

    const QString explicitPath = QDir::cleanPath(detectorLabelsPath.trimmed());
    if (!explicitPath.isEmpty())
    {
        QFileInfo explicitInfo(explicitPath);
        if (explicitInfo.exists() && explicitInfo.isFile())
            return explicitInfo.absoluteFilePath();
    }

    const QString resolvedModelPath = ResolveDetectorModelPath();
    const QFileInfo modelInfo(resolvedModelPath);
    const QString baseName = modelInfo.completeBaseName();
    const QString appDir = QCoreApplication::applicationDirPath();

    QStringList candidates;
    if (modelInfo.exists())
    {
        const QDir modelDir = modelInfo.dir();
        candidates
            << modelDir.filePath(baseName + ".labels.txt")
            << modelDir.filePath(baseName + ".txt")
            << modelDir.filePath("labels.txt")
            << modelDir.filePath("classes.txt");
    }

    const QStringList labelRoots = {
        QDir(appDir).filePath("labels"),
        QDir(appDir).filePath("../common/labels"),
        QDir(QDir::currentPath()).filePath("labels"),
        QDir(QDir::currentPath()).filePath("../common/labels"),
        QDir(appDir).filePath("../../../../bin/labels"),
        QDir(appDir).filePath("../../../../bin/common/labels"),
        QDir(appDir).filePath("../../../bin/labels"),
        QDir(appDir).filePath("../../../bin/common/labels"),
        QDir(appDir).filePath("../../bin/labels"),
        QDir(appDir).filePath("../../bin/common/labels"),
        QDir(appDir).filePath("../bin/labels")
    };

    for (const QString& root : labelRoots)
    {
        const QDir dir(QDir::cleanPath(root));
        candidates
            << dir.filePath(baseName + ".labels.txt")
            << dir.filePath(baseName + ".txt")
            << dir.filePath("coco80.txt")
            << dir.filePath("labels.txt")
            << dir.filePath("classes.txt");
    }

    candidates.removeDuplicates();
    for (const QString& candidate : candidates)
    {
        QFileInfo info(QDir::cleanPath(candidate));
        if (info.exists() && info.isFile())
        {
            if (usedAuto)
                *usedAuto = true;
            return info.absoluteFilePath();
        }
    }

    return QString();
}

void FeatureManager::ResetForMedia()
{
    if (features)
        features->DisableAllAiFeatures();

    if (view)
    {
        view->SetDetectionOverlayEnabled(false);
        view->ClearDetections();
        view->SetAnime4KEnabled(false);
    }

    detectorEnabled = false;
    asrEnabled = false;

    if (view)
    {
        view->SetDetectorChecked(false);
        view->SetAsrChecked(false);
        view->SetAnime4KChecked(false);
        view->HideDetectorOsd();
        view->HideAnime4KOsd();
    }

    detectorOsdTimer = 0;
    anime4kOsdTimer = 0;
    detectorEventCooldown.invalidate();
    aiToggleBusy = false;
    UpdateAiToggleButtonsEnabled(true);
}

void FeatureManager::TickOsd()
{
    if (anime4kOsdTimer > 0)
    {
        --anime4kOsdTimer;
        if (anime4kOsdTimer == 0 && view)
            view->HideAnime4KOsd();
    }

    if (detectorOsdTimer > 0)
    {
        --detectorOsdTimer;
        if (detectorOsdTimer == 0 && view)
            view->HideDetectorOsd();
    }
}

void FeatureManager::Relayout()
{
    const int videoWidth = videoAreaWidthProvider ? videoAreaWidthProvider() : 0;
    const int height = hostHeightProvider ? hostHeightProvider() : 0;
    if (view)
        view->RelayoutOsd(videoWidth, height);
}

void FeatureManager::SetDetectorEnabled(bool enabled)
{
    if (enabled == detectorEnabled)
        return;
    ToggleDetector();
}

QString FeatureManager::DetectorStatusText(bool enabled) const
{
    if (!enabled)
        return "DET OFF";

    const QFileInfo modelInfo(ResolveDetectorModelPath());
    const QString modelName = modelInfo.fileName().isEmpty() ? "model" : modelInfo.fileName();
    const AiModelRecord detectorRecord = features ? features->GetAiModelRecord(AiCapability::Detector) : AiModelRecord{};
    const QString backend = QString::fromStdString(
        detectorRecord.loaded
            ? (detectorRecord.backendSummary.empty() ? std::string("unknown") : detectorRecord.backendSummary)
            : std::string("loading"));
    return QString("DET ON | %1 infer | %2").arg(backend, modelName);
}

QString FeatureManager::AsrStatusText(bool enabled) const
{
    if (!enabled)
        return "ASR OFF";

    const AiModelRecord asrRecord = features ? features->GetAiModelRecord(AiCapability::Asr) : AiModelRecord{};
    const AiModelRecord vadRecord = features ? features->GetAiModelRecord(AiCapability::Vad) : AiModelRecord{};
    const QString backend = QString::fromStdString(
        asrRecord.backendSummary.empty() ? std::string("unknown") : asrRecord.backendSummary);
    QString vad = "VAD off";
    if (vadRecord.active)
    {
        const QString vadBackend = QString::fromStdString(
            vadRecord.backendSummary.empty() ? std::string("heuristic") : vadRecord.backendSummary);
        vad = QString("VAD %1").arg(vadBackend);
    }
    return QString("ASR ON | %1 infer | %2").arg(backend, vad);
}

QString FeatureManager::Anime4KStatusText(bool enabled) const
{
    if (!enabled)
        return "A4K OFF";
    return QString("A4K ON | %1").arg(view ? view->Anime4KBackendSummary() : "GPU");
}

void FeatureManager::ShowA4kOsd(const QString& text)
{
    if (!view)
        return;

    view->ShowAnime4KOsd(text);
    Relayout();
    anime4kOsdTimer = 50;
}

void FeatureManager::ShowDetectorOsd(const QString& text)
{
    if (!view)
        return;

    view->ShowDetectorOsd(text);
    Relayout();
    detectorOsdTimer = 50;
}

bool FeatureManager::BeginAiToggle()
{
    if (aiToggleBusy)
        return false;

    if (aiToggleCooldown.isValid() && aiToggleCooldown.elapsed() < aiToggleCooldownMs)
        return false;

    aiToggleBusy = true;
    UpdateAiToggleButtonsEnabled(false);
    return true;
}

void FeatureManager::EndAiToggle()
{
    aiToggleBusy = false;
    if (aiToggleCooldown.isValid())
        aiToggleCooldown.restart();
    else
        aiToggleCooldown.start();
    UpdateAiToggleButtonsEnabled(true);
}

void FeatureManager::UpdateAiToggleButtonsEnabled(bool enabled)
{
    if (view)
        view->SetAiButtonsEnabled(enabled);
}

void FeatureManager::ToggleASR()
{
    if (!BeginAiToggle())
        return;

    const bool enableAsr = !asrEnabled;
    asrEnabled = enableAsr;
    if (view)
        view->SetAsrChecked(enableAsr);
    if (subtitles)
        subtitles->ClearRenderedTrack();

    if (enableAsr)
    {
        const QString relativeModelPath = "models/ggml-small.bin";
        const QString appDir = QCoreApplication::applicationDirPath();
        const QStringList candidates = {
            QDir(appDir).filePath(relativeModelPath),
            QDir(appDir).filePath("../common/" + relativeModelPath),
            QDir(QDir::currentPath()).filePath(relativeModelPath),
            QDir(QDir::currentPath()).filePath("../common/" + relativeModelPath),
            QDir(appDir).filePath("../../../../bin/models/ggml-small.bin"),
            QDir(appDir).filePath("../../../../bin/common/models/ggml-small.bin"),
            QDir(appDir).filePath("../../../bin/models/ggml-small.bin"),
            QDir(appDir).filePath("../../../bin/common/models/ggml-small.bin"),
            QDir(appDir).filePath("../../bin/models/ggml-small.bin"),
            QDir(appDir).filePath("../../bin/common/models/ggml-small.bin"),
            QDir(appDir).filePath("../bin/models/ggml-small.bin")
        };

        QString resolvedModelPath;
        for (const QString& candidate : candidates)
        {
            QFileInfo info(QDir::cleanPath(candidate));
            if (info.exists() && info.isFile())
            {
                resolvedModelPath = info.absoluteFilePath();
                break;
            }
        }

        if (resolvedModelPath.isEmpty())
        {
            asrEnabled = false;
            if (view)
                view->SetAsrChecked(false);
            if (subtitles)
                subtitles->SetEpoch(StreamEpoch{});
            EndAiToggle();
            if (view)
                view->ShowWarning("ASR", "Whisper model not found. Expected ggml-small.bin in a models directory.");
            return;
        }

        if (features)
        {
            AiFeatureConfig config;
            config.capability = AiCapability::Asr;
            config.modelPath = resolvedModelPath.toStdString();
            config.preferGpu = true;
            config.allowCpuFallback = true;
            std::string error;
            if (!features->SetFeatureEnabled(config, true, &error))
            {
                asrEnabled = false;
                if (view)
                    view->SetAsrChecked(false);
                if (subtitles)
                    subtitles->SetEpoch(StreamEpoch{});
                EndAiToggle();
                if (view)
                    view->ShowWarning("ASR", QString("Failed to load Whisper model:\n%1").arg(resolvedModelPath));
                return;
            }
        }

        const AiModelRecord asrRecord = features ? features->GetAiModelRecord(AiCapability::Asr) : AiModelRecord{};
        if (!asrRecord.loaded)
        {
            asrEnabled = false;
            if (view)
                view->SetAsrChecked(false);
            if (subtitles)
                subtitles->SetEpoch(StreamEpoch{});
            EndAiToggle();
            if (view)
                view->ShowWarning("ASR", QString("Failed to load Whisper model:\n%1").arg(resolvedModelPath));
            return;
        }

        if (features)
        {
            features->BindAsrSubtitleHandler(
                view ? view->ContextObject() : nullptr,
                [this](const QString& text, long long startMs, long long endMs, quint64 generation, quint64 serial)
                {
                    if (subtitles)
                        subtitles->OnSubtitleReady(text, startMs, endMs, generation, serial);
                });
            features->ClearAsrOutput();
        }
        if (subtitles)
        {
            subtitles->ActivateAsrTrack(features ? features->GetFeatureEpoch(AiCapability::Asr) : StreamEpoch{});
            subtitles->UpdateDisplay();
            subtitles->UpdateTrackButton();
            subtitles->ShowOsd(AsrStatusText(true));
        }
        EndAiToggle();
        return;
    }

    if (features)
        features->SetFeatureEnabled(AiCapability::Asr, false, nullptr);
    if (subtitles)
    {
        subtitles->SetEpoch(StreamEpoch{});
        subtitles->UpdateTrackButton();
        subtitles->ShowOsd(AsrStatusText(false));
    }
    EndAiToggle();
}

void FeatureManager::ToggleAnime4K()
{
    if (!BeginAiToggle())
        return;

    const bool enabled = view ? !view->IsAnime4KEnabled() : false;
    if (view)
    {
        view->SetAnime4KEnabled(enabled);
        view->SetAnime4KChecked(enabled);
    }
    ShowA4kOsd(Anime4KStatusText(enabled));
    EndAiToggle();
}

void FeatureManager::ToggleDetector()
{
    if (!BeginAiToggle())
        return;

    const bool enableDetector = !detectorEnabled;
    detectorEnabled = enableDetector;
    if (view)
        view->SetDetectorChecked(enableDetector);

    if (enableDetector)
    {
        QString error;
        if (!StartDetector(&error))
        {
            detectorEnabled = false;
            if (view)
                view->SetDetectorChecked(false);
            EndAiToggle();
            if (view)
                view->ShowWarning("Detector", error);
            return;
        }
    }
    else
    {
        StopDetector();
    }

    ShowDetectorOsd(DetectorStatusText(detectorEnabled));
    EndAiToggle();
}

bool FeatureManager::StartDetector(QString* errorMessage)
{
    const QString resolvedModelPath = ResolveDetectorModelPath();
    if (resolvedModelPath.isEmpty())
    {
        if (errorMessage)
            *errorMessage = "Detection model not found. Load an ONNX model first.";
        return false;
    }

    const QString explicitLabelsPath = QDir::cleanPath(detectorLabelsPath.trimmed());
    const QString resolvedLabelsPath = ResolveDetectorLabelsPath();
    if (!explicitLabelsPath.isEmpty() && resolvedLabelsPath.isEmpty())
    {
        if (errorMessage)
            *errorMessage = QString("Labels file not found:\n%1").arg(explicitLabelsPath);
        return false;
    }

    if (features)
    {
        AiFeatureConfig config;
        config.capability = AiCapability::Detector;
        config.modelPath = resolvedModelPath.toStdString();
        config.auxModelPath = resolvedLabelsPath.toStdString();
        config.preferGpu = true;
        config.allowCpuFallback = true;
        std::string error;
        if (!features->SetFeatureEnabled(config, true, &error))
        {
            if (errorMessage && error.empty())
                *errorMessage = "Failed to enable detector capability.";
            else if (errorMessage)
                *errorMessage = QString::fromStdString(error);
            return false;
        }
    }

    if (!explicitLabelsPath.isEmpty() && !resolvedLabelsPath.isEmpty())
    {
        const QString explicitAbsolute = QFileInfo(explicitLabelsPath).absoluteFilePath();
        const QString resolvedAbsolute = QFileInfo(resolvedLabelsPath).absoluteFilePath();
        if (!explicitAbsolute.isEmpty() && !resolvedAbsolute.isEmpty() && explicitAbsolute != resolvedAbsolute)
            detectorLabelsPath = resolvedAbsolute;
    }

    if (view)
        view->SetDetectionOverlayEnabled(true);

    if (features)
    {
        features->BindDetectorResultHandler(
            view ? view->ContextObject() : nullptr,
            [this](DetectionResult result)
            {
                HandleDetectionsReady(std::move(result));
            });
        features->BindDetectorModelReadyHandler(
            view ? view->ContextObject() : nullptr,
            [this](bool success)
            {
                OnDetectorModelReady(success);
            });
        if (features->GetAiModelRecord(AiCapability::Detector).loaded)
            OnDetectorModelReady(true);
    }

    detectorModelPath = resolvedModelPath;
    SaveDetectorSettings();
    return true;
}

void FeatureManager::StopDetector()
{
    if (features)
        features->SetFeatureEnabled(AiCapability::Detector, false, nullptr);
    if (view)
    {
        view->SetDetectionOverlayEnabled(false);
        view->ClearDetections();
    }
}

void FeatureManager::RestartDetectorIfEnabled(const QString& osdText)
{
    SaveDetectorSettings();
    if (!detectorEnabled)
        return;

    StopDetector();

    QString error;
    if (!StartDetector(&error))
    {
        detectorEnabled = false;
        if (view)
        {
            view->SetDetectorChecked(false);
            view->ShowWarning("Detector", error);
        }
        return;
    }

    QString statusText = osdText;
    const AiModelRecord detectorRecord = features ? features->GetAiModelRecord(AiCapability::Detector) : AiModelRecord{};
    const QString backend = QString::fromStdString(
        detectorRecord.loaded
            ? (detectorRecord.backendSummary.empty() ? std::string("unknown") : detectorRecord.backendSummary)
            : std::string("loading"));
    statusText += QString(" | %1 infer").arg(backend);
    ShowDetectorOsd(statusText);
}

void FeatureManager::OnDetectorModelReady(bool success)
{
    if (!detectorEnabled)
        return;

    if (!success)
    {
        detectorEnabled = false;
        if (view)
            view->SetDetectorChecked(false);
        StopDetector();
        ShowDetectorOsd("DET load failed");
    }
    else
    {
        ShowDetectorOsd(DetectorStatusText(true));
    }
    detectorOsdTimer = 50;
}

void FeatureManager::ShowDetectorMenu(const QPoint& pos)
{
    if (!view)
        return;

    const QString resolvedModelPath = ResolveDetectorModelPath();
    bool autoLabels = false;
    const QString resolvedLabelsPath = ResolveDetectorLabelsPath(&autoLabels);
    const QFileInfo modelInfo(resolvedModelPath);
    const QFileInfo labelsInfo(resolvedLabelsPath);

    QString labelsText = "Labels: auto/default";
    if (!detectorLabelsPath.trimmed().isEmpty())
    {
        labelsText = resolvedLabelsPath.isEmpty()
            ? QString("Labels: missing (%1)").arg(QFileInfo(detectorLabelsPath).fileName())
            : QString("Labels: %1").arg(QFileInfo(detectorLabelsPath).fileName());
    }
    else if (!resolvedLabelsPath.isEmpty())
    {
        labelsText = autoLabels
            ? QString("Labels: auto (%1)").arg(labelsInfo.fileName())
            : QString("Labels: %1").arg(labelsInfo.fileName());
    }

    const DetectorMenuCommand command = view->ShowDetectorMenu(
        pos,
        resolvedModelPath.isEmpty()
            ? QString("Model: not set")
            : QString("Model: %1").arg(modelInfo.fileName()),
        labelsText,
        !detectorLabelsPath.trimmed().isEmpty());

    if (command == DetectorMenuCommand::LoadModel)
    {
        const QString initialDir = detectorModelPath.trimmed().isEmpty()
            ? QCoreApplication::applicationDirPath()
            : QFileInfo(detectorModelPath).absolutePath();
        const QString filePath = view->SelectDetectorModelPath(initialDir);
        if (filePath.isEmpty())
            return;

        detectorModelPath = QDir::cleanPath(filePath);
        RestartDetectorIfEnabled(QString("Detector model: %1").arg(QFileInfo(detectorModelPath).fileName()));
        return;
    }

    if (command == DetectorMenuCommand::LoadLabels)
    {
        const QString initialDir = detectorLabelsPath.trimmed().isEmpty()
            ? QCoreApplication::applicationDirPath()
            : QFileInfo(detectorLabelsPath).absolutePath();
        const QString filePath = view->SelectDetectorLabelsPath(initialDir);
        if (filePath.isEmpty())
            return;

        detectorLabelsPath = QDir::cleanPath(filePath);
        RestartDetectorIfEnabled(QString("Detector labels: %1").arg(QFileInfo(detectorLabelsPath).fileName()));
        return;
    }

    if (command == DetectorMenuCommand::ClearLabels)
    {
        detectorLabelsPath.clear();
        RestartDetectorIfEnabled("Detector labels: auto/default");
    }
}
