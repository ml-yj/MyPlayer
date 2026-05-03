

#include "segment_writer.h"

#include "../archive/archive_path_policy.h"
#include "../media/demux.h"

#include <QFileInfo>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/codec_par.h>
#include <libavcodec/packet.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/rational.h>
}

namespace
{

const char* OutputFormatName(const QString& container)
{
    if (ArchivePathPolicy::IsFragmentedMp4Container(container))
        return "mp4";
    if (ArchivePathPolicy::NormalizeRecordingContainer(container) == QLatin1String("mkv"))
    {
        return "matroska";
    }
    return nullptr;
}
}

SegmentWriter::SegmentWriter() = default;

SegmentWriter::~SegmentWriter()
{
    Close();
}

bool SegmentWriter::Open(const RecordingActiveSegment& segment, Demux& demux, std::string* errorMessage)
{
    Close();
    segment_ = segment;
    segment_.container = ArchivePathPolicy::NormalizeRecordingContainer(segment_.container);

    const QByteArray pathBytes = segment.absolutePath.toUtf8();
    const int allocResult = avformat_alloc_output_context2(
        &formatContext_, nullptr, OutputFormatName(segment.container), pathBytes.constData());
    if (allocResult < 0 || !formatContext_)
    {
        if (errorMessage)
            *errorMessage = AvErrorText(allocResult);
        return false;
    }

    if (!AddStream(false, demux, videoMapping_, errorMessage))
    {
        Close();
        return false;
    }
    if (!AddStream(true, demux, audioMapping_, errorMessage))
    {
        Close();
        return false;
    }

    if (!videoMapping_.active && !audioMapping_.active)
    {
        if (errorMessage)
            *errorMessage = "No audio/video streams available for recording.";
        Close();
        return false;
    }

    if (!(formatContext_->oformat->flags & AVFMT_NOFILE))
    {
        const int openResult = avio_open(&formatContext_->pb, pathBytes.constData(), AVIO_FLAG_WRITE);
        if (openResult < 0)
        {
            if (errorMessage)
                *errorMessage = AvErrorText(openResult);
            Close();
            return false;
        }
    }

    formatContext_->flags |= AVFMT_FLAG_FLUSH_PACKETS;

    AVDictionary* muxerOptions = nullptr;
    if (ArchivePathPolicy::IsFragmentedMp4Container(segment_.container))
    {
        av_dict_set(&muxerOptions, "movflags", "frag_keyframe+empty_moov+default_base_moof+faststart", 0);
    }

    const int headerResult = avformat_write_header(formatContext_, &muxerOptions);
    av_dict_free(&muxerOptions);
    if (headerResult < 0)
    {
        if (errorMessage)
        {
            const std::string detail = AvErrorText(headerResult);
            if (ArchivePathPolicy::IsFragmentedMp4Container(segment_.container))
            {
                *errorMessage = "Fragmented MP4 rejected the current stream codecs. Try MKV. " + detail;
            }
            else
            {
                *errorMessage = detail;
            }
        }
        Close();
        return false;
    }

    return true;
}

bool SegmentWriter::WritePacket(const AVPacket* packet, bool isAudio, std::string* errorMessage)
{
    if (!formatContext_ || !packet)
        return false;

    StreamMapping& mapping = isAudio ? audioMapping_ : videoMapping_;
    if (!mapping.active)
        return true;

    AVPacket* copy = av_packet_clone(packet);
    if (!copy)
    {
        if (errorMessage)
            *errorMessage = "Failed to clone packet for recording.";
        return false;
    }

    copy->stream_index = mapping.outputStreamIndex;
    av_packet_rescale_ts(copy, *mapping.inputTimeBase, *mapping.outputTimeBase);
    copy->pos = -1;

    const int writeResult = av_interleaved_write_frame(formatContext_, copy);
    if (writeResult >= 0 && formatContext_->pb && ArchivePathPolicy::IsFragmentedMp4Container(segment_.container))
        avio_flush(formatContext_->pb);
    av_packet_free(&copy);
    if (writeResult < 0)
    {
        if (errorMessage)
            *errorMessage = AvErrorText(writeResult);
        return false;
    }
    return true;
}

qint64 SegmentWriter::Close()
{
    const QString filePath = segment_.absolutePath;
    if (formatContext_)
    {
        if (formatContext_->pb)
            av_write_trailer(formatContext_);
        if (!(formatContext_->oformat->flags & AVFMT_NOFILE) && formatContext_->pb)
            avio_closep(&formatContext_->pb);
        avformat_free_context(formatContext_);
        formatContext_ = nullptr;
    }

    videoMapping_ = {};
    audioMapping_ = {};
    videoInputTimeBase_ = {};
    videoOutputTimeBase_ = {};
    audioInputTimeBase_ = {};
    audioOutputTimeBase_ = {};

    if (filePath.isEmpty())
        return 0;
    return QFileInfo(filePath).size();
}

bool SegmentWriter::IsOpen() const
{
    return formatContext_ != nullptr;
}

QString SegmentWriter::VideoCodecName() const
{
    return videoMapping_.codecName;
}

QString SegmentWriter::AudioCodecName() const
{
    return audioMapping_.codecName;
}

bool SegmentWriter::AddStream(bool audio, Demux& demux, StreamMapping& mapping, std::string* errorMessage)
{
    AVCodecParameters* parameters = nullptr;
    AVRational inputTimeBase{};
    int inputStreamIndex = -1;
    std::string codecName;

    const bool copied = audio
        ? demux.CopyRecordingAudioStream(&parameters, &inputTimeBase, &inputStreamIndex, &codecName)
        : demux.CopyRecordingVideoStream(&parameters, &inputTimeBase, &inputStreamIndex, &codecName);
    if (!copied)
        return true;

    AVStream* outputStream = avformat_new_stream(formatContext_, nullptr);
    if (!outputStream)
    {
        if (errorMessage)
            *errorMessage = "Failed to allocate recording output stream.";
        avcodec_parameters_free(&parameters);
        return false;
    }

    const int copyResult = avcodec_parameters_copy(outputStream->codecpar, parameters);
    avcodec_parameters_free(&parameters);
    if (copyResult < 0)
    {
        if (errorMessage)
            *errorMessage = AvErrorText(copyResult);
        return false;
    }

    outputStream->codecpar->codec_tag = 0;
    outputStream->time_base = inputTimeBase;

    mapping.active = true;
    mapping.inputStreamIndex = inputStreamIndex;
    mapping.outputStreamIndex = outputStream->index;
    if (audio)
    {
        audioInputTimeBase_ = inputTimeBase;
        audioOutputTimeBase_ = outputStream->time_base;
        mapping.inputTimeBase = &audioInputTimeBase_;
        mapping.outputTimeBase = &audioOutputTimeBase_;
    }
    else
    {
        videoInputTimeBase_ = inputTimeBase;
        videoOutputTimeBase_ = outputStream->time_base;
        mapping.inputTimeBase = &videoInputTimeBase_;
        mapping.outputTimeBase = &videoOutputTimeBase_;
    }
    mapping.codecName = QString::fromStdString(codecName);
    return true;
}

std::string SegmentWriter::AvErrorText(int errorCode)
{
    char buffer[AV_ERROR_MAX_STRING_SIZE] = { 0 };
    av_strerror(errorCode, buffer, sizeof(buffer));
    return buffer;
}
