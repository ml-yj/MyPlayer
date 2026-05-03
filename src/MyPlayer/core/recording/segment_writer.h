#pragma once

#include "recording_models.h"

#include <QString>

extern "C" {
#include <libavutil/rational.h>
}

struct AVPacket;
struct AVCodecParameters;
struct AVFormatContext;
class Demux;

class SegmentWriter
{
public:
    SegmentWriter();
    ~SegmentWriter();

    bool Open(const RecordingActiveSegment& segment, Demux& demux, std::string* errorMessage = nullptr);
    bool WritePacket(const AVPacket* packet, bool isAudio, std::string* errorMessage = nullptr);
    qint64 Close();

    bool IsOpen() const;
    QString VideoCodecName() const;
    QString AudioCodecName() const;

private:

    struct StreamMapping
    {
        bool active = false;
        int inputStreamIndex = -1;
        int outputStreamIndex = -1;
        AVRational* inputTimeBase = nullptr;
        AVRational* outputTimeBase = nullptr;
        QString codecName;
    };

    bool AddStream(bool audio, Demux& demux, StreamMapping& mapping, std::string* errorMessage);
    static std::string AvErrorText(int errorCode);

    RecordingActiveSegment segment_;
    AVFormatContext* formatContext_ = nullptr;
    StreamMapping videoMapping_;
    StreamMapping audioMapping_;
    AVRational videoInputTimeBase_{};
    AVRational videoOutputTimeBase_{};
    AVRational audioInputTimeBase_{};
    AVRational audioOutputTimeBase_{};
};
