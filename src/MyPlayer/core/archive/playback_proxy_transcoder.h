#pragma once

#include "archive_models.h"
#include "archive_path_policy.h"

#include <QString>

struct PlaybackProxyTranscodeRequest
{
    ArchivePathPolicy policy;
    ArchiveSegmentRecord sourceSegment;
    QString sourceAbsolutePath;
    QString codecProfile = "h264_aac_mp4";
};

struct PlaybackProxyTranscodeResult
{
    ArchivePlaybackProxyRecord proxyRecord;
    QString sourceAbsolutePath;
    QString proxyAbsolutePath;
};

class PlaybackProxyTranscoder
{
public:
    static bool Transcode(
        const PlaybackProxyTranscodeRequest& request,
        PlaybackProxyTranscodeResult* result,
        QString* errorMessage = nullptr);
};
