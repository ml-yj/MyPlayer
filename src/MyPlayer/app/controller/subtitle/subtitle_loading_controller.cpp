
#include "subtitle_controller.h"

#include "../../view/subtitle_view_qt.h"
#include "../../service/playback_service_interfaces.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

void SubtitleController::SetEpoch(const StreamEpoch& epoch)
{
    subtitleEpoch = epoch;
}

StreamEpoch SubtitleController::Epoch() const
{
    return subtitleEpoch;
}

void SubtitleController::ResetAsrCues(const StreamEpoch& epoch)
{
    subtitleManager.ClearCues(SubtitleSourceType::Whisper);
    ClearRenderedTrack();
    subtitleEpoch = epoch;
}

void SubtitleController::ActivateAsrTrack(const StreamEpoch& epoch)
{
    subtitleEpoch = epoch;
    subtitleManager.ClearCues(SubtitleSourceType::Whisper);
    subtitleManager.EnsureTrack(SubtitleSourceType::Whisper, "Whisper ASR");
    activeSubtitleTrackId = subtitleManager.FindTrackBySource(SubtitleSourceType::Whisper);
    UpdateDisplay();
    UpdateTrackButton();
}

void SubtitleController::AutoLoadEmbedded(const QString& mediaPath)
{
    if (!tracks)
        return;

    subtitleManager.RemoveTracks(SubtitleSourceType::Embedded);

    if (mediaPath.isEmpty())
    {
        if (activeSubtitleTrackId != 0)
        {
            const SubtitleTrack* activeTrack = subtitleManager.FindTrack(activeSubtitleTrackId);
            if (!activeTrack || activeTrack->source == SubtitleSourceType::Embedded)
                activeSubtitleTrackId = 0;
        }
        UpdateTrackButton();
        return;
    }

    const int count = tracks->GetSubtitleStreamCount();
    if (count <= 0)
    {
        const SubtitleTrack* activeTrack = subtitleManager.FindTrack(activeSubtitleTrackId);
        if (activeTrack && activeTrack->source == SubtitleSourceType::Embedded)
            activeSubtitleTrackId = 0;
        UpdateTrackButton();
        return;
    }

    int firstTrackId = 0;
    for (int i = 0; i < count; ++i)
    {
        const SubtitleStreamInfo info = tracks->GetSubtitleStreamInfo(i);
        if (!info.isTextBased)
            continue;

        std::vector<SubtitleCueData> cues;
        if (!tracks->LoadSubtitleTrack(i, cues) || cues.empty())
            continue;

        QString trackName;
        if (!info.title.empty())
        {
            trackName = QString::fromUtf8(info.title.c_str());
        }
        else
        {
            trackName = QString("Subtitle %1").arg(i + 1);
            if (!info.codecName.empty())
                trackName += QString(" (%1)").arg(QString::fromUtf8(info.codecName.c_str()));
        }

        if (!info.language.empty())
            trackName += QString(" [%1]").arg(QString::fromUtf8(info.language.c_str()));

        const QString pseudoPath = QString("embedded:%1:%2")
            .arg(QFileInfo(mediaPath).fileName())
            .arg(info.streamIndex);
        const int trackId = subtitleManager.EnsureTrack(SubtitleSourceType::Embedded, trackName, pseudoPath);
        SubtitleTrack* track = subtitleManager.FindTrack(trackId);
        if (!track)
            continue;

        const QString codecName = QString::fromUtf8(info.codecName.c_str());
        track->name = trackName;
        track->filePath = pseudoPath;
        track->renderWithLibass =
            codecName.compare("ass", Qt::CaseInsensitive) == 0 ||
            codecName.compare("ssa", Qt::CaseInsensitive) == 0;
        track->cues.clear();

        for (const SubtitleCueData& cue : cues)
        {
            subtitleManager.AddCue(trackId, SubtitleCue{
                cue.startMs,
                cue.endMs,
                QString::fromUtf8(cue.text.c_str()),
                QString::fromUtf8(cue.assText.c_str())
            });
        }

        if (!track->cues.isEmpty() && firstTrackId == 0)
            firstTrackId = trackId;
    }

    if (firstTrackId == 0)
    {
        const SubtitleTrack* activeTrack = subtitleManager.FindTrack(activeSubtitleTrackId);
        if (activeTrack && activeTrack->source == SubtitleSourceType::Embedded)
            activeSubtitleTrackId = 0;
    }
    UpdateTrackButton();
}

void SubtitleController::AutoLoadExternal(const QString& mediaPath)
{
    subtitleManager.RemoveTracks(SubtitleSourceType::ExternalFile);

    if (mediaPath.isEmpty())
    {
        if (activeSubtitleTrackId != 0)
        {
            const SubtitleTrack* activeTrack = subtitleManager.FindTrack(activeSubtitleTrackId);
            if (!activeTrack || activeTrack->source == SubtitleSourceType::ExternalFile)
                activeSubtitleTrackId = 0;
        }
        UpdateTrackButton();
        return;
    }

    const QStringList srtPaths = subtitleManager.FindMatchingSrts(mediaPath);
    if (srtPaths.isEmpty())
    {
        const SubtitleTrack* activeTrack = subtitleManager.FindTrack(activeSubtitleTrackId);
        if (activeTrack && activeTrack->source == SubtitleSourceType::ExternalFile)
            activeSubtitleTrackId = 0;
        UpdateTrackButton();
        return;
    }

    int firstTrackId = 0;
    QStringList loadedNames;
    for (const QString& srtPath : srtPaths)
    {
        QString error;
        const int trackId = subtitleManager.LoadSrt(srtPath, &error);
        if (trackId == 0)
        {
            qDebug() << "Failed to auto-load subtitle:" << error;
            continue;
        }

        if (firstTrackId == 0)
            firstTrackId = trackId;
        loadedNames.push_back(QFileInfo(srtPath).fileName());
    }

    if (firstTrackId == 0)
    {
        UpdateTrackButton();
        return;
    }

    UpdateTrackButton();
    ShowOsd(QString("Subtitle available: %1").arg(loadedNames.join(", ")));
}

void SubtitleController::OpenSubtitleFile()
{
    if (!view)
        return;

    const QString currentMediaPath = currentMediaPathProvider ? currentMediaPathProvider() : QString();
    const QString startDir = currentMediaPath.isEmpty() ? QString() : QFileInfo(currentMediaPath).absolutePath();
    const QStringList paths = view->SelectSubtitleFiles(startDir);
    if (paths.isEmpty())
        return;

    int firstTrackId = 0;
    QStringList loadedNames;
    for (const QString& path : paths)
    {
        QString error;
        const int trackId = subtitleManager.LoadSrt(path, &error);
        if (trackId == 0)
        {
            view->ShowWarningMessage("Subtitle", error);
            continue;
        }

        if (firstTrackId == 0)
            firstTrackId = trackId;
        loadedNames.push_back(QFileInfo(path).fileName());
    }

    if (firstTrackId != 0)
    {
        activeSubtitleTrackId = firstTrackId;
        ClearRenderedTrack();
        UpdateTrackButton();
        ShowOsd(QString("Subtitle loaded: %1").arg(loadedNames.join(", ")));
    }
}

void SubtitleController::ExportAsrSubtitle()
{
    if (!view)
        return;

    const int asrTrackId = subtitleManager.FindTrackBySource(SubtitleSourceType::Whisper);
    const SubtitleTrack* asrTrack = subtitleManager.FindTrack(asrTrackId);
    if (!asrTrack || asrTrack->cues.isEmpty())
    {
        view->ShowInfoMessage("ASR", "No ASR subtitles are available to export yet.");
        return;
    }

    QString defaultName = "asr_export.srt";
    const QString currentMediaPath = currentMediaPathProvider ? currentMediaPathProvider() : QString();
    QFileInfo mediaInfo(currentMediaPath);
    QString startDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (mediaInfo.exists())
    {
        startDir = mediaInfo.absolutePath();
        defaultName = mediaInfo.completeBaseName() + ".asr.srt";
    }

    const QString savePath = view->SelectSubtitleSavePath(QDir(startDir).filePath(defaultName));
    if (savePath.isEmpty())
        return;

    QString error;
    if (!subtitleManager.ExportTrackToSrt(asrTrackId, savePath, &error))
    {
        view->ShowWarningMessage("ASR", error);
        return;
    }

    ShowOsd(QString("ASR exported: %1").arg(QFileInfo(savePath).fileName()));
}

void SubtitleController::OnSubtitleReady(
    const QString& text, long long startMs, long long endMs, quint64 generation, quint64 serial)
{
    const QString trimmed = text.trimmed();
    if (!(asrEnabledProvider && asrEnabledProvider())
        || generation != subtitleEpoch.generation
        || serial != subtitleEpoch.serial
        || trimmed.isEmpty())
    {
        qDebug().noquote()
            << QString("[subdbg] asr cue ignored enabled=%1 generation=%2/%3 expected=%4/%5 empty=%6 start=%7 end=%8 text=\"%9\"")
                   .arg((asrEnabledProvider && asrEnabledProvider()) ? 1 : 0)
                   .arg(generation)
                   .arg(serial)
                   .arg(subtitleEpoch.generation)
                   .arg(subtitleEpoch.serial)
                   .arg(trimmed.isEmpty() ? 1 : 0)
                   .arg(startMs)
                   .arg(endMs)
                   .arg(trimmed.left(80));
        return;
    }

    if (endMs <= startMs)
    {
        qDebug().noquote()
            << QString("[subdbg] asr cue rejected invalid-range start=%1 end=%2 text=\"%3\"")
                   .arg(startMs)
                   .arg(endMs)
                   .arg(trimmed.left(80));
        return;
    }

    const int asrTrackId = subtitleManager.EnsureTrack(SubtitleSourceType::Whisper, "Whisper ASR");
    const bool added = subtitleManager.AddCue(asrTrackId, SubtitleCue{ startMs, endMs, trimmed });
    const SubtitleTrack* asrTrack = subtitleManager.FindTrack(asrTrackId);
    qDebug().noquote()
        << QString("[subdbg] asr cue %1 track=%2 cues=%3 rev=%4 start=%5 end=%6 text=\"%7\"")
               .arg(added ? "accepted" : "deduped")
               .arg(asrTrackId)
               .arg(asrTrack ? asrTrack->cues.size() : 0)
               .arg(asrTrack ? asrTrack->revision : 0)
               .arg(startMs)
               .arg(endMs)
               .arg(trimmed.left(80));
    if (!added)
        return;

    if (activeSubtitleTrackId == 0)
    {
        activeSubtitleTrackId = asrTrackId;
        UpdateTrackButton();
    }

    if (activeSubtitleTrackId == asrTrackId)
        UpdateDisplay();
}
