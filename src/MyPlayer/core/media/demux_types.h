#pragma once

#include <QtGlobal>

#include <string>

struct StreamEpoch
{
    quint64 generation = 0;
    quint64 serial = 0;
};

struct AudioStreamInfo
{
    int streamIndex = -1;
    std::string language;
    std::string title;
    std::string codecName;
    int sampleRate = 0;
    int channels = 0;
};

struct SubtitleStreamInfo
{
    int streamIndex = -1;
    std::string language;
    std::string title;
    std::string codecName;
    bool isTextBased = false;
};

struct SubtitleCueData
{
    long long startMs = 0;
    long long endMs = 0;
    std::string text;
    std::string assText;
};
