#pragma once

#include "subtitle_types.h"

#include <QString>
#include <QStringList>
#include <QVector>
#include <memory>

class SubtitleTrackRegistry;
class SubtitleCueTimeline;
class SubtitleSrtCodec;

class SubtitleManager
{
public:
    SubtitleManager();
    ~SubtitleManager();

    void Clear();
    void ClearCues(SubtitleSourceType source);
    void RemoveTracks(SubtitleSourceType source);

    int EnsureTrack(SubtitleSourceType source, const QString& name, const QString& filePath = QString());
    int LoadSrt(const QString& path, QString* error = nullptr);
    bool AddCue(int trackId, const SubtitleCue& cue);

    const SubtitleTrack* FindTrack(int trackId) const;
    SubtitleTrack* FindTrack(int trackId);
    int FindTrackBySource(SubtitleSourceType source) const;
    const QVector<SubtitleTrack>& Tracks() const;

    QString FindMatchingSrt(const QString& mediaPath) const;
    QStringList FindMatchingSrts(const QString& mediaPath) const;
    QString TextAt(int trackId, long long clockMs, long long offsetMs) const;
    bool ExportTrackToSrt(int trackId, const QString& path, QString* error = nullptr) const;

private:
    std::unique_ptr<SubtitleTrackRegistry> registry_;
    std::unique_ptr<SubtitleCueTimeline> cueTimeline_;
    std::unique_ptr<SubtitleSrtCodec> srtCodec_;
};
