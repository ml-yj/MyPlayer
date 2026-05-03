
#pragma once

#include <QtGlobal>
#include <QString>
#include <QVector>

enum class SubtitleSourceType
{
    ExternalFile,
    Embedded,
    Whisper
};

struct SubtitleCue
{

    long long startMs = 0;
    long long endMs = 0;

    QString text;

    QString assText;
};

struct SubtitleTrack
{
    int id = 0;

    SubtitleSourceType source = SubtitleSourceType::ExternalFile;

    QString name;
    QString filePath;

    bool renderWithLibass = false;

    QVector<SubtitleCue> cues;

    quint64 revision = 0;
};
