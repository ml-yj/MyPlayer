#include "demux.h"

struct AVCodecParameters;
struct AVRational;
struct DemuxResources;
struct DemuxState;

class DemuxTrackCatalog
{
public:

    DemuxTrackCatalog(DemuxState& state, DemuxResources& resources);

    void ResetLocked();
    void RebuildLocked();
    void UpdateSelectedAudioMetadataLocked();

    int GetAudioStreamCount();
    AudioStreamInfo GetAudioStreamInfo(int idx);

    void SetAudioStream(int idx);
    int GetCurrentAudioIndex();

    int GetSubtitleStreamCount();
    SubtitleStreamInfo GetSubtitleStreamInfo(int idx);

    AVCodecParameters* CopyVPara();

    AVCodecParameters* CopyAPara();

    bool CopyRecordingVideoStream(
        AVCodecParameters** outParameters, AVRational* outTimeBase, int* outStreamIndex,
        std::string* outCodecName = nullptr);

    bool CopyRecordingAudioStream(
        AVCodecParameters** outParameters, AVRational* outTimeBase, int* outStreamIndex,
        std::string* outCodecName = nullptr);

private:

    DemuxState& state_;
    DemuxResources& resources_;
};

#include <string>

class DemuxTrackCatalog;
struct DemuxResources;
struct DemuxState;

class DemuxOpenController
{
public:

    DemuxOpenController(DemuxState& state, DemuxResources& resources, DemuxTrackCatalog& trackCatalog);

    bool Open(const char* url);

    bool Open(const char* url, const StreamOpenOptions& options);

    void Close();

    void SetOpenOptions(const StreamOpenOptions& options);

    StreamOpenOptions GetOpenOptions();

    std::string GetLastError();

private:

    DemuxState& state_;
    DemuxResources& resources_;
    DemuxTrackCatalog& trackCatalog_;
};

#include <algorithm>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/packet.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
}

class DemuxPacketReader
{
public:
    DemuxPacketReader(DemuxState& state, DemuxResources& resources)
        : state_(state)
        , resources_(resources)
    {
    }

    AVPacket* Read()
    {
        std::lock_guard<std::mutex> lock(resources_.mux);
        if (!resources_.ic)
            return nullptr;

        AVPacket* pkt = av_packet_alloc();
        while (true)
        {
            const int result = av_read_frame(resources_.ic, pkt);
            if (result != 0)
            {
                av_packet_free(&pkt);
                return nullptr;
            }

            if (pkt->stream_index == state_.videoStream || pkt->stream_index == state_.audioStream)
            {
                NormalizePacketTimestamps(pkt);
                return pkt;
            }

            av_packet_unref(pkt);
        }
    }

    AVPacket* ReadVideo()
    {
        std::lock_guard<std::mutex> lock(resources_.mux);
        if (!resources_.ic)
            return nullptr;

        AVPacket* pkt = av_packet_alloc();
        while (true)
        {
            const int result = av_read_frame(resources_.ic, pkt);
            if (result != 0)
            {
                av_packet_free(&pkt);
                return nullptr;
            }

            if (pkt->stream_index == state_.videoStream)
            {
                NormalizePacketTimestamps(pkt);
                return pkt;
            }

            av_packet_unref(pkt);
        }
    }

    bool IsAudio(const AVPacket* pkt)
    {
        std::lock_guard<std::mutex> lock(resources_.mux);
        return pkt && pkt->stream_index == state_.audioStream;
    }

private:
    static int64_t PacketTimeToMs(int64_t value, AVRational timeBase)
    {
        if (value == AV_NOPTS_VALUE)
            return value;
        return av_rescale_q(value, timeBase, AVRational{ 1, 1000 });
    }

    void NormalizePacketTimestamps(AVPacket* pkt) const
    {
        if (!pkt || !resources_.ic || pkt->stream_index < 0 ||
            pkt->stream_index >= static_cast<int>(resources_.ic->nb_streams))
        {
            return;
        }

        const AVRational timeBase = resources_.ic->streams[pkt->stream_index]->time_base;
        pkt->pts = PacketTimeToMs(pkt->pts, timeBase);
        pkt->dts = PacketTimeToMs(pkt->dts, timeBase);
        pkt->duration = static_cast<int>(PacketTimeToMs(pkt->duration, timeBase));
    }

    DemuxState& state_;
    DemuxResources& resources_;
};

namespace
{
std::string TrimCopy(const std::string& value)
{
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return {};

    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

void ReplaceAll(std::string& value, const std::string& from, const std::string& to)
{
    if (from.empty())
        return;

    size_t pos = 0;
    while ((pos = value.find(from, pos)) != std::string::npos)
    {
        value.replace(pos, from.size(), to);
        pos += to.size();
    }
}

std::string NormalizePlainText(std::string text)
{
    ReplaceAll(text, "\r\n", "\n");
    ReplaceAll(text, "\r", "\n");

    std::istringstream stream(text);
    std::string line;
    std::vector<std::string> lines;
    while (std::getline(stream, line))
    {
        line = TrimCopy(line);
        if (!line.empty())
            lines.push_back(line);
    }

    std::ostringstream out;
    for (size_t i = 0; i < lines.size(); ++i)
    {
        if (i > 0)
            out << '\n';
        out << lines[i];
    }
    return out.str();
}

std::string AssToPlainText(const char* assText)
{
    std::string text = assText ? assText : "";
    const size_t colonPos = text.find(':');
    if (colonPos != std::string::npos && text.rfind("Dialogue", 0) == 0)
        text = text.substr(colonPos + 1);

    size_t payloadStart = 0;
    int commaCount = 0;
    for (size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] != ',')
            continue;
        ++commaCount;
        if (commaCount == 9)
        {
            payloadStart = i + 1;
            break;
        }
    }

    if (commaCount == 9 && payloadStart < text.size())
        text = text.substr(payloadStart);

    ReplaceAll(text, "\\N", "\n");
    ReplaceAll(text, "\\n", "\n");
    ReplaceAll(text, "\\h", " ");

    std::string cleaned;
    cleaned.reserve(text.size());
    bool inTag = false;
    for (char c : text)
    {
        if (c == '{')
        {
            inTag = true;
            continue;
        }
        if (c == '}')
        {
            inTag = false;
            continue;
        }
        if (!inTag)
            cleaned.push_back(c);
    }

    return NormalizePlainText(cleaned);
}

std::string AssToEventText(const char* assText)
{
    std::string text = assText ? assText : "";
    const size_t colonPos = text.find(':');
    if (colonPos != std::string::npos && text.rfind("Dialogue", 0) == 0)
        text = text.substr(colonPos + 1);

    size_t payloadStart = 0;
    int commaCount = 0;
    for (size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] != ',')
            continue;
        ++commaCount;
        if (commaCount == 9)
        {
            payloadStart = i + 1;
            break;
        }
    }

    if (commaCount == 9 && payloadStart < text.size())
        text = text.substr(payloadStart);

    ReplaceAll(text, "\r\n", "\\N");
    ReplaceAll(text, "\r", "\\N");
    ReplaceAll(text, "\n", "\\N");
    return TrimCopy(text);
}

std::string SubtitleRectText(const AVSubtitleRect* rect)
{
    if (!rect)
        return {};
    if (rect->text)
        return NormalizePlainText(rect->text);
    if (rect->ass)
        return AssToPlainText(rect->ass);
    return {};
}

std::string SubtitleRectAssText(const AVSubtitleRect* rect)
{
    if (!rect || !rect->ass)
        return {};
    return AssToEventText(rect->ass);
}

long long SubtitleBaseMs(const AVPacket* pkt, const AVSubtitle& sub, AVRational timeBase)
{
    if (sub.pts != AV_NOPTS_VALUE)
        return av_rescale_q(sub.pts, AV_TIME_BASE_Q, AVRational{ 1, 1000 });
    if (pkt && pkt->pts != AV_NOPTS_VALUE)
        return av_rescale_q(pkt->pts, timeBase, AVRational{ 1, 1000 });
    if (pkt && pkt->dts != AV_NOPTS_VALUE)
        return av_rescale_q(pkt->dts, timeBase, AVRational{ 1, 1000 });
    return 0;
}
}

class SubtitleTrackLoader
{
public:
    SubtitleTrackLoader(DemuxState& state, DemuxResources& resources)
        : state_(state)
        , resources_(resources)
    {
    }

    bool LoadSubtitleTrack(int idx, std::vector<SubtitleCueData>& cues)
    {
        cues.clear();

        std::string localUrl;
        SubtitleStreamInfo info;
        {
            std::lock_guard<std::mutex> lock(resources_.mux);
            if (idx < 0 || idx >= static_cast<int>(state_.subtitleStreams.size()) || state_.sourceUrl.empty())
                return false;

            localUrl = state_.sourceUrl;
            info = state_.subtitleStreams[idx];
        }

        if (!info.isTextBased)
            return false;

        AVFormatContext* subInput = nullptr;
        if (avformat_open_input(&subInput, localUrl.c_str(), nullptr, nullptr) != 0)
            return false;

        const auto cleanup = [&]() {
            if (subInput)
                avformat_close_input(&subInput);
        };

        if (avformat_find_stream_info(subInput, nullptr) < 0)
        {
            cleanup();
            return false;
        }

        if (info.streamIndex < 0 || info.streamIndex >= static_cast<int>(subInput->nb_streams))
        {
            cleanup();
            return false;
        }

        AVStream* stream = subInput->streams[info.streamIndex];
        if (!stream || !stream->codecpar || stream->codecpar->codec_type != AVMEDIA_TYPE_SUBTITLE)
        {
            cleanup();
            return false;
        }

        const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
        if (!codec)
        {
            cleanup();
            return false;
        }

        AVCodecContext* codecCtx = avcodec_alloc_context3(codec);
        if (!codecCtx)
        {
            cleanup();
            return false;
        }

        if (avcodec_parameters_to_context(codecCtx, stream->codecpar) < 0)
        {
            avcodec_free_context(&codecCtx);
            cleanup();
            return false;
        }

        codecCtx->pkt_timebase = stream->time_base;
        if (avcodec_open2(codecCtx, codec, nullptr) < 0)
        {
            avcodec_free_context(&codecCtx);
            cleanup();
            return false;
        }

        AVPacket* pkt = av_packet_alloc();
        if (!pkt)
        {
            avcodec_free_context(&codecCtx);
            cleanup();
            return false;
        }

        while (av_read_frame(subInput, pkt) >= 0)
        {
            if (pkt->stream_index != info.streamIndex)
            {
                av_packet_unref(pkt);
                continue;
            }

            AVSubtitle subtitle{};
            int gotSubtitle = 0;
            const int decodeResult = avcodec_decode_subtitle2(codecCtx, &subtitle, &gotSubtitle, pkt);
            if (decodeResult >= 0 && gotSubtitle)
            {
                std::ostringstream textStream;
                std::ostringstream assStream;
                bool hasText = false;
                bool hasAss = false;

                for (unsigned i = 0; i < subtitle.num_rects; ++i)
                {
                    const std::string rectText = SubtitleRectText(subtitle.rects[i]);
                    if (rectText.empty())
                    {
                        const std::string rectAss = SubtitleRectAssText(subtitle.rects[i]);
                        if (!rectAss.empty())
                        {
                            if (hasAss)
                                assStream << "\\N";
                            assStream << rectAss;
                            hasAss = true;
                        }
                        continue;
                    }

                    if (hasText)
                        textStream << '\n';
                    textStream << rectText;
                    hasText = true;

                    const std::string rectAss = SubtitleRectAssText(subtitle.rects[i]);
                    if (!rectAss.empty())
                    {
                        if (hasAss)
                            assStream << "\\N";
                        assStream << rectAss;
                        hasAss = true;
                    }
                }

                if (hasText)
                {
                    const long long baseMs = SubtitleBaseMs(pkt, subtitle, stream->time_base);
                    const long long startMs =
                        baseMs + std::max(0LL, static_cast<long long>(subtitle.start_display_time));

                    long long endMs = baseMs;
                    if (subtitle.end_display_time > subtitle.start_display_time)
                    {
                        endMs += subtitle.end_display_time;
                    }
                    else if (pkt->duration > 0)
                    {
                        endMs = baseMs +
                            av_rescale_q(pkt->duration, stream->time_base, AVRational{ 1, 1000 });
                    }
                    else
                    {
                        endMs = startMs + 3000;
                    }

                    if (endMs <= startMs)
                        endMs = startMs + 3000;

                    cues.push_back(SubtitleCueData{
                        startMs,
                        endMs,
                        textStream.str(),
                        hasAss ? assStream.str() : std::string{},
                    });
                }

                avsubtitle_free(&subtitle);
            }

            av_packet_unref(pkt);
        }

        av_packet_free(&pkt);
        avcodec_free_context(&codecCtx);
        cleanup();

        std::sort(cues.begin(), cues.end(), [](const SubtitleCueData& a, const SubtitleCueData& b) {
            if (a.startMs != b.startMs)
                return a.startMs < b.startMs;
            return a.endMs < b.endMs;
        });

        return !cues.empty();
    }

private:
    DemuxState& state_;
    DemuxResources& resources_;
};

Demux::Demux()
    : audioStreams(state_.audioStreams)
    , subtitleStreams(state_.subtitleStreams)
    , totalMs(state_.totalMs)
    , width(state_.width)
    , height(state_.height)
    , sampleRate(state_.sampleRate)
    , channels(state_.channels)
    , videoFpsNum(state_.videoFpsNum)
    , videoFpsDen(state_.videoFpsDen)
    , bitrate(state_.bitrate)
{
    static bool isFirst = true;
    static std::mutex initMux;
    std::lock_guard<std::mutex> lock(initMux);

    if (isFirst)
    {
        avformat_network_init();
        isFirst = false;
    }

    subtitleTrackLoader_ = std::make_unique<SubtitleTrackLoader>(state_, resources_);
    trackCatalog_ = std::make_unique<DemuxTrackCatalog>(state_, resources_);
    packetReader_ = std::make_unique<DemuxPacketReader>(state_, resources_);
    openController_ = std::make_unique<DemuxOpenController>(state_, resources_, *trackCatalog_);
}

Demux::~Demux()
{
    Close();
}

bool Demux::Open(const char* url)
{
    return openController_ ? openController_->Open(url) : false;
}

bool Demux::Open(const char* url, const StreamOpenOptions& options)
{
    return openController_ ? openController_->Open(url, options) : false;
}

AVPacket* Demux::Read()
{
    return packetReader_ ? packetReader_->Read() : nullptr;
}

AVPacket* Demux::ReadVideo()
{
    return packetReader_ ? packetReader_->ReadVideo() : nullptr;
}

bool Demux::IsAudio(AVPacket* pkt)
{
    return packetReader_ ? packetReader_->IsAudio(pkt) : false;
}

AVCodecParameters* Demux::CopyVPara()
{
    return trackCatalog_ ? trackCatalog_->CopyVPara() : nullptr;
}

AVCodecParameters* Demux::CopyAPara()
{
    return trackCatalog_ ? trackCatalog_->CopyAPara() : nullptr;
}

bool Demux::CopyRecordingVideoStream(
    AVCodecParameters** outParameters, AVRational* outTimeBase, int* outStreamIndex, std::string* outCodecName)
{
    return trackCatalog_
        ? trackCatalog_->CopyRecordingVideoStream(outParameters, outTimeBase, outStreamIndex, outCodecName)
        : false;
}

bool Demux::CopyRecordingAudioStream(
    AVCodecParameters** outParameters, AVRational* outTimeBase, int* outStreamIndex, std::string* outCodecName)
{
    return trackCatalog_
        ? trackCatalog_->CopyRecordingAudioStream(outParameters, outTimeBase, outStreamIndex, outCodecName)
        : false;
}

bool Demux::Seek(double pos)
{
    const int total = state_.totalMs.load();
    if (total <= 0)
        return false;

    if (pos < 0.0)
        pos = 0.0;
    if (pos > 1.0)
        pos = 1.0;

    return SeekMs(static_cast<long long>(pos * total));
}

bool Demux::SeekMs(long long ms)
{
    std::lock_guard<std::mutex> lock(resources_.mux);
    if (!resources_.ic)
        return false;

    if (ms < 0)
        ms = 0;

    avformat_flush(resources_.ic);
    const int64_t seekTarget = av_rescale_q(ms, AVRational{ 1, 1000 }, AV_TIME_BASE_Q);
    int result =
        avformat_seek_file(resources_.ic, -1, INT64_MIN, seekTarget, INT64_MAX, AVSEEK_FLAG_BACKWARD);

    if (result < 0)
        result = av_seek_frame(resources_.ic, -1, seekTarget, AVSEEK_FLAG_BACKWARD);

    return result >= 0;
}

void Demux::Clear()
{
    std::lock_guard<std::mutex> lock(resources_.mux);
    if (resources_.ic)
        avformat_flush(resources_.ic);
}

void Demux::Close()
{
    if (openController_)
        openController_->Close();
}

int Demux::GetAudioStreamCount()
{
    return trackCatalog_ ? trackCatalog_->GetAudioStreamCount() : 0;
}

AudioStreamInfo Demux::GetAudioStreamInfo(int idx)
{
    return trackCatalog_ ? trackCatalog_->GetAudioStreamInfo(idx) : AudioStreamInfo{};
}

void Demux::SetAudioStream(int idx)
{
    if (trackCatalog_)
        trackCatalog_->SetAudioStream(idx);
}

int Demux::GetCurrentAudioIndex()
{
    return trackCatalog_ ? trackCatalog_->GetCurrentAudioIndex() : -1;
}

int Demux::GetSubtitleStreamCount()
{
    return trackCatalog_ ? trackCatalog_->GetSubtitleStreamCount() : 0;
}

SubtitleStreamInfo Demux::GetSubtitleStreamInfo(int idx)
{
    return trackCatalog_ ? trackCatalog_->GetSubtitleStreamInfo(idx) : SubtitleStreamInfo{};
}

bool Demux::LoadSubtitleTrack(int idx, std::vector<SubtitleCueData>& cues)
{
    return subtitleTrackLoader_ ? subtitleTrackLoader_->LoadSubtitleTrack(idx, cues) : false;
}

void Demux::SetOpenOptions(const StreamOpenOptions& options)
{
    if (openController_)
        openController_->SetOpenOptions(options);
}

StreamOpenOptions Demux::GetOpenOptions()
{
    return openController_ ? openController_->GetOpenOptions() : StreamOpenOptions::DefaultFile();
}

std::string Demux::GetLastError()
{
    return openController_ ? openController_->GetLastError() : std::string();
}

bool Demux::IsOpen() const
{
    std::lock_guard<std::mutex> lock(resources_.mux);
    return resources_.ic != nullptr;
}

bool Demux::HasVideoStream() const
{
    std::lock_guard<std::mutex> lock(resources_.mux);
    return state_.videoStream >= 0;
}

bool Demux::IsSeekable() const
{
    std::lock_guard<std::mutex> lock(resources_.mux);
    if (!resources_.ic)
        return false;

    if (resources_.ic->ctx_flags & AVFMTCTX_UNSEEKABLE)
        return false;

    if (!resources_.ic->pb)
        return false;

    return (resources_.ic->pb->seekable & AVIO_SEEKABLE_NORMAL) != 0;
}

#include <string>

extern "C" {
#include <libavcodec/avcodec.h>

#include <libavcodec/codec_desc.h>

#include <libavformat/avformat.h>
#include <libavutil/version.h>
}

using namespace std;

namespace {

    int AudioCodecParameterChannels(const AVCodecParameters* parameters)
    {
        if (!parameters)
            return 0;
#if LIBAVUTIL_VERSION_MAJOR >= 57
        return parameters->ch_layout.nb_channels;
#else
        return parameters->channels;
#endif
    }

    string MetadataValue(AVDictionary* metadata, const char* key)
    {

        if (!metadata || !key)
            return {};

        AVDictionaryEntry* entry = av_dict_get(metadata, key, nullptr, 0);

        return entry && entry->value ? entry->value : "";
    }

    bool IsTextSubtitleCodec(AVCodecID codecId)
    {

        const AVCodecDescriptor* desc = avcodec_descriptor_get(codecId);

        return desc && (desc->props & AV_CODEC_PROP_TEXT_SUB);
    }

    void FillAudioStreamInfo(AVStream* stream, AudioStreamInfo& info)
    {

        info.streamIndex = static_cast<int>(stream->index);

        info.sampleRate = stream->codecpar->sample_rate;

        info.channels = AudioCodecParameterChannels(stream->codecpar);

        info.language = MetadataValue(stream->metadata, "language");

        info.title = MetadataValue(stream->metadata, "title");

        const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);

        if (codec && codec->name)
            info.codecName = codec->name;
    }

    void FillSubtitleStreamInfo(AVStream* stream, SubtitleStreamInfo& info)
    {

        info.streamIndex = static_cast<int>(stream->index);

        info.language = MetadataValue(stream->metadata, "language");

        info.title = MetadataValue(stream->metadata, "title");

        info.isTextBased = IsTextSubtitleCodec(stream->codecpar->codec_id);

        const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);

        if (codec && codec->name)
            info.codecName = codec->name;
    }
}

DemuxTrackCatalog::DemuxTrackCatalog(DemuxState& state, DemuxResources& resources)
    : state_(state)
    , resources_(resources)
{}

void DemuxTrackCatalog::ResetLocked()
{

    state_.audioStreams.clear();
    state_.subtitleStreams.clear();
}

void DemuxTrackCatalog::RebuildLocked()
{
    ResetLocked();

    if (!resources_.ic)
        return;

    for (unsigned i = 0; i < resources_.ic->nb_streams; ++i)
    {

        AVStream* stream = resources_.ic->streams[i];

        if (!stream || !stream->codecpar)
            continue;

        if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
        {

            AudioStreamInfo info;

            FillAudioStreamInfo(stream, info);

            state_.audioStreams.push_back(info);

            continue;
        }

        if (stream->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE)
        {

            SubtitleStreamInfo info;
            FillSubtitleStreamInfo(stream, info);
            state_.subtitleStreams.push_back(info);
        }
    }
}

void DemuxTrackCatalog::UpdateSelectedAudioMetadataLocked()
{

    if (!resources_.ic || state_.audioStream < 0)
    {

        state_.sampleRate.store(0);

        state_.channels.store(0);
        return;
    }

    AVStream* stream = resources_.ic->streams[state_.audioStream];

    if (!stream || !stream->codecpar)
    {

        state_.sampleRate.store(0);
        state_.channels.store(0);
        return;
    }

    state_.sampleRate.store(stream->codecpar->sample_rate);

    state_.channels.store(AudioCodecParameterChannels(stream->codecpar));
}

int DemuxTrackCatalog::GetAudioStreamCount()
{
    lock_guard<mutex> lock(resources_.mux);

    return static_cast<int>(state_.audioStreams.size());
}

AudioStreamInfo DemuxTrackCatalog::GetAudioStreamInfo(int idx)
{

    lock_guard<mutex> lock(resources_.mux);

    if (idx < 0 || idx >= static_cast<int>(state_.audioStreams.size()))
        return {};

    return state_.audioStreams[idx];
}

void DemuxTrackCatalog::SetAudioStream(int idx)
{

    lock_guard<mutex> lock(resources_.mux);

    if (idx < 0 || idx >= static_cast<int>(state_.audioStreams.size()))
        return;

    state_.audioStream = state_.audioStreams[idx].streamIndex;

    UpdateSelectedAudioMetadataLocked();
}

int DemuxTrackCatalog::GetCurrentAudioIndex()
{

    lock_guard<mutex> lock(resources_.mux);

    for (int i = 0; i < static_cast<int>(state_.audioStreams.size()); ++i)
    {

        if (state_.audioStreams[i].streamIndex == state_.audioStream)
            return i;
    }

    return -1;
}

int DemuxTrackCatalog::GetSubtitleStreamCount()
{

    lock_guard<mutex> lock(resources_.mux);

    return static_cast<int>(state_.subtitleStreams.size());
}

SubtitleStreamInfo DemuxTrackCatalog::GetSubtitleStreamInfo(int idx)
{

    lock_guard<mutex> lock(resources_.mux);

    if (idx < 0 || idx >= static_cast<int>(state_.subtitleStreams.size()))
        return {};

    return state_.subtitleStreams[idx];
}

AVCodecParameters* DemuxTrackCatalog::CopyVPara()
{

    lock_guard<mutex> lock(resources_.mux);

    if (!resources_.ic || state_.videoStream < 0)
        return nullptr;

    AVCodecParameters* parameters = avcodec_parameters_alloc();

    if (!parameters)
        return nullptr;

    avcodec_parameters_copy(parameters, resources_.ic->streams[state_.videoStream]->codecpar);

    return parameters;
}

AVCodecParameters* DemuxTrackCatalog::CopyAPara()
{

    lock_guard<mutex> lock(resources_.mux);

    if (!resources_.ic || state_.audioStream < 0)
        return nullptr;

    AVCodecParameters* parameters = avcodec_parameters_alloc();
    if (!parameters)
        return nullptr;

    avcodec_parameters_copy(parameters, resources_.ic->streams[state_.audioStream]->codecpar);

    return parameters;
}

bool DemuxTrackCatalog::CopyRecordingVideoStream(

    AVCodecParameters** outParameters, AVRational* outTimeBase, int* outStreamIndex, std::string* outCodecName)
{

    lock_guard<mutex> lock(resources_.mux);

    if (!resources_.ic || state_.videoStream < 0 || !outParameters || !outTimeBase || !outStreamIndex)
        return false;

    AVStream* stream = resources_.ic->streams[state_.videoStream];
    if (!stream || !stream->codecpar)
        return false;

    AVCodecParameters* parameters = avcodec_parameters_alloc();
    if (!parameters)
        return false;

    avcodec_parameters_copy(parameters, stream->codecpar);

    *outParameters = parameters;

    *outTimeBase = stream->time_base;

    *outStreamIndex = state_.videoStream;

    if (outCodecName)

        *outCodecName = avcodec_get_name(stream->codecpar->codec_id);

    return true;
}

bool DemuxTrackCatalog::CopyRecordingAudioStream(
    AVCodecParameters** outParameters, AVRational* outTimeBase, int* outStreamIndex, std::string* outCodecName)
{

    lock_guard<mutex> lock(resources_.mux);

    if (!resources_.ic || state_.audioStream < 0 || !outParameters || !outTimeBase || !outStreamIndex)
        return false;

    AVStream* stream = resources_.ic->streams[state_.audioStream];

    if (!stream || !stream->codecpar)
        return false;

    AVCodecParameters* parameters = avcodec_parameters_alloc();
    if (!parameters)
        return false;

    avcodec_parameters_copy(parameters, stream->codecpar);

    *outParameters = parameters;
    *outTimeBase = stream->time_base;
    *outStreamIndex = state_.audioStream;
    if (outCodecName)
        *outCodecName = avcodec_get_name(stream->codecpar->codec_id);

    return true;
}

#include "../../common/diagnostics/logger.h"

#include <algorithm>
#include <chrono>
#include <string>

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
}

using namespace std;

namespace {

void SetOpenOptionInt(AVDictionary** opts, const char* key, long long value)
{
    if (!opts || !key || value <= 0)
        return;

    const string text = to_string(value);
    av_dict_set(opts, key, text.c_str(), 0);
}

StreamOpenOptions BuildEffectiveOpenOptions(const char* url, const StreamOpenOptions& requestedOptions)
{
    const string source = url ? url : "";
    const StreamSourceType sourceType = ResolveStreamSourceType(source);
    StreamOpenOptions effectiveOptions = requestedOptions;
    const bool isHls = sourceType == StreamSourceType::Hls;

    if (isHls)
    {

        const StreamOpenOptions defaults = StreamOpenOptions::DefaultNetwork();
        effectiveOptions.enableLowLatency = false;
        effectiveOptions.noBuffer = false;
        effectiveOptions.lowDelayFlag = false;
        effectiveOptions.reorderQueueSize = -1;
        effectiveOptions.liveClockPolicy = LiveClockPolicy::AudioMaster;
        effectiveOptions.videoQueuePackets = max(effectiveOptions.videoQueuePackets, defaults.videoQueuePackets);
        effectiveOptions.audioQueuePackets = max(effectiveOptions.audioQueuePackets, defaults.audioQueuePackets);
        effectiveOptions.audioDeviceBufferMs = effectiveOptions.audioDeviceBufferMs > 0
            ? effectiveOptions.audioDeviceBufferMs
            : defaults.audioDeviceBufferMs;
        effectiveOptions.videoLeadMs = max(effectiveOptions.videoLeadMs, defaults.videoLeadMs);
        effectiveOptions.lateVideoDropMs = 0;
    }

    const StreamSourcePolicy sourcePolicy =
        ResolveStreamSourcePolicy(StreamPlaybackKind::NetworkVod, sourceType, effectiveOptions);
    const bool useBalancedProbeFloor =
        !effectiveOptions.enableLowLatency && sourcePolicy.balancedProbeFloorBytes > 0;
    const bool useLowLatencyProbeFloor =
        effectiveOptions.enableLowLatency && sourcePolicy.lowLatencyProbeFloorBytes > 0;
    const bool isRtspUdpLowLatency =
        sourceType == StreamSourceType::Rtsp
        && !effectiveOptions.forceTcpForRtsp
        && effectiveOptions.enableLowLatency;

    if (isRtspUdpLowLatency)
    {
        effectiveOptions.probeSizeBytes = max(
            effectiveOptions.probeSizeBytes, sourcePolicy.lowLatencyProbeFloorBytes);
        effectiveOptions.analyzeDurationUs = max(
            effectiveOptions.analyzeDurationUs, sourcePolicy.lowLatencyAnalyzeDurationUs);
    }
    else if (useBalancedProbeFloor)
    {
        effectiveOptions.probeSizeBytes = max(
            effectiveOptions.probeSizeBytes, sourcePolicy.balancedProbeFloorBytes);
        effectiveOptions.analyzeDurationUs = max(
            effectiveOptions.analyzeDurationUs, sourcePolicy.balancedAnalyzeDurationUs);
    }
    else if (useLowLatencyProbeFloor)
    {
        effectiveOptions.probeSizeBytes = max(
            effectiveOptions.probeSizeBytes, sourcePolicy.lowLatencyProbeFloorBytes);
        effectiveOptions.analyzeDurationUs = max(
            effectiveOptions.analyzeDurationUs, sourcePolicy.lowLatencyAnalyzeDurationUs);
    }

    if (isRtspUdpLowLatency && effectiveOptions.reorderQueueSize == 0)
        effectiveOptions.reorderQueueSize = 4;

    return effectiveOptions;
}

StreamOpenOptions BuildStableRtspRetryOptions(const StreamOpenOptions& requestedOptions, bool forceTcp)
{
    StreamOpenOptions retryOptions = requestedOptions;
    const StreamOpenOptions stableDefaults = StreamOpenOptions::DefaultNetwork();

    retryOptions.forceTcpForRtsp = forceTcp;
    retryOptions.enableLowLatency = false;
    retryOptions.noBuffer = false;
    retryOptions.lowDelayFlag = false;
    retryOptions.reorderQueueSize = -1;
    retryOptions.liveClockPolicy = LiveClockPolicy::AudioMaster;
    retryOptions.connectTimeoutMs = max(retryOptions.connectTimeoutMs, stableDefaults.connectTimeoutMs);
    retryOptions.videoQueuePackets = max(retryOptions.videoQueuePackets, stableDefaults.videoQueuePackets);
    retryOptions.audioQueuePackets = max(retryOptions.audioQueuePackets, stableDefaults.audioQueuePackets);
    retryOptions.audioDeviceBufferMs = retryOptions.audioDeviceBufferMs > 0
        ? max(retryOptions.audioDeviceBufferMs, stableDefaults.audioDeviceBufferMs)
        : stableDefaults.audioDeviceBufferMs;
    retryOptions.videoLeadMs = max(retryOptions.videoLeadMs, stableDefaults.videoLeadMs);
    retryOptions.lateVideoDropMs = 0;
    retryOptions.probeSizeBytes = max(retryOptions.probeSizeBytes, 512 * 1024);
    retryOptions.analyzeDurationUs = max(retryOptions.analyzeDurationUs, 2000 * 1000);
    return retryOptions;
}

void ApplyOpenOptions(AVDictionary** opts, const char* url, const StreamOpenOptions& options)
{
    const string source = url ? url : "";
    const StreamSourceType sourceType = ResolveStreamSourceType(source);
    const StreamOpenOptions effectiveOptions = BuildEffectiveOpenOptions(url, options);
    const StreamSourcePolicy sourcePolicy =
        ResolveStreamSourcePolicy(StreamPlaybackKind::NetworkVod, sourceType, effectiveOptions);
    const bool isRtsp = sourceType == StreamSourceType::Rtsp;
    const bool isHls = sourceType == StreamSourceType::Hls;
    const bool useBalancedProbeFloor =
        !effectiveOptions.enableLowLatency && sourcePolicy.balancedProbeFloorBytes > 0;
    const bool useLowLatencyProbeFloor =
        effectiveOptions.enableLowLatency && sourcePolicy.lowLatencyProbeFloorBytes > 0;
    const bool isRtspUdpLowLatency =
        isRtsp && !effectiveOptions.forceTcpForRtsp && effectiveOptions.enableLowLatency;
    if (isRtsp)
        av_dict_set(opts, "rtsp_transport", effectiveOptions.forceTcpForRtsp ? "tcp" : "udp", 0);

    SetOpenOptionInt(opts, "max_delay", effectiveOptions.maxDelayUs);
    SetOpenOptionInt(opts, "stimeout", static_cast<long long>(effectiveOptions.connectTimeoutMs) * 1000);
    SetOpenOptionInt(opts, "rw_timeout", static_cast<long long>(effectiveOptions.connectTimeoutMs) * 1000);
    SetOpenOptionInt(opts, "buffer_size", effectiveOptions.bufferSizeBytes);

    const int probeSizeBytes = isRtspUdpLowLatency
        ? max(effectiveOptions.probeSizeBytes, sourcePolicy.lowLatencyProbeFloorBytes)
        : (useBalancedProbeFloor
            ? max(effectiveOptions.probeSizeBytes, sourcePolicy.balancedProbeFloorBytes)
            : (useLowLatencyProbeFloor
                ? max(effectiveOptions.probeSizeBytes, sourcePolicy.lowLatencyProbeFloorBytes)
                : effectiveOptions.probeSizeBytes));
    const int analyzeDurationUs = isRtspUdpLowLatency
        ? max(effectiveOptions.analyzeDurationUs, sourcePolicy.lowLatencyAnalyzeDurationUs)
        : (useBalancedProbeFloor
            ? max(effectiveOptions.analyzeDurationUs, sourcePolicy.balancedAnalyzeDurationUs)
            : (useLowLatencyProbeFloor
                ? max(effectiveOptions.analyzeDurationUs, sourcePolicy.lowLatencyAnalyzeDurationUs)
                : effectiveOptions.analyzeDurationUs));
    const int reorderQueueSize = isRtspUdpLowLatency && effectiveOptions.reorderQueueSize == 0
        ? 4
        : effectiveOptions.reorderQueueSize;

    SetOpenOptionInt(opts, "probesize", probeSizeBytes);
    SetOpenOptionInt(opts, "analyzeduration", analyzeDurationUs);
    if (reorderQueueSize >= 0)
        SetOpenOptionInt(opts, "reorder_queue_size", reorderQueueSize);

    if (isRtsp)
    {
        const char* formatFlags = (effectiveOptions.noBuffer && !isRtspUdpLowLatency)
            ? "nobuffer+discardcorrupt"
            : "discardcorrupt";
        av_dict_set(opts, "fflags", formatFlags, 0);
    }
    else if (isHls)
    {
        av_dict_set(opts, "fflags", "discardcorrupt", 0);
    }
    else if (effectiveOptions.noBuffer && !isRtspUdpLowLatency)
    {
        av_dict_set(opts, "fflags", "nobuffer", 0);
    }

    if (effectiveOptions.lowDelayFlag)
        av_dict_set(opts, "flags", "low_delay", 0);
}

long long SteadyNowMs()
{
    return static_cast<long long>(
        chrono::duration_cast<chrono::milliseconds>(
            chrono::steady_clock::now().time_since_epoch()).count());
}

int DemuxInterruptCallback(void* opaque)
{
    auto* resources = static_cast<DemuxResources*>(opaque);
    if (!resources)
        return 0;

    if (resources->openAbortRequested.load())
        return 1;

    const long long deadlineMs = resources->openDeadlineMs.load();
    if (deadlineMs > 0 && SteadyNowMs() >= deadlineMs)
        return 1;

    return 0;
}

void SetInterruptCallback(AVFormatContext* formatContext, DemuxResources& resources)
{
    if (!formatContext)
        return;

    formatContext->interrupt_callback.callback = &DemuxInterruptCallback;
    formatContext->interrupt_callback.opaque = &resources;
}

string BuildInterruptErrorText(const char* stage, int timeoutMs)
{
    return string(stage) + " timed out after " + to_string(timeoutMs) + " ms";
}

string BuildInputErrorText(int ffmpegError, StreamSourceType sourceType)
{
    if (sourceType == StreamSourceType::Hls && ffmpegError == AVERROR_INVALIDDATA)
        return "Invalid HLS playlist or playlist not ready";

    char buffer[1024] = { 0 };
    av_strerror(ffmpegError, buffer, sizeof(buffer) - 1);
    return buffer;
}

}

DemuxOpenController::DemuxOpenController(
    DemuxState& state,
    DemuxResources& resources,
    DemuxTrackCatalog& trackCatalog)
    : state_(state)
    , resources_(resources)
    , trackCatalog_(trackCatalog)
{
}

bool DemuxOpenController::Open(const char* url)
{
    StreamOpenOptions activeOptions;
    int timeoutMs = 3000;
    {
        lock_guard<mutex> optionLock(resources_.mux);
        activeOptions = state_.openOptions;
        state_.lastError.clear();
        activeOptions = BuildEffectiveOpenOptions(url, activeOptions);
        state_.openOptions = activeOptions;
        timeoutMs = max(1000, activeOptions.connectTimeoutMs);
    }

    Close();

    lock_guard<mutex> lock(resources_.mux);
    const string source = url ? url : "";
    const StreamSourceType sourceType = ResolveStreamSourceType(source);
    const bool isRtsp = sourceType == StreamSourceType::Rtsp;
    const bool isHls = sourceType == StreamSourceType::Hls;
    const int analyzeBudgetMs = max(1000, activeOptions.analyzeDurationUs / 1000);
    const int openBudgetMs = isRtsp
        ? max(timeoutMs + analyzeBudgetMs + 1000, timeoutMs + 4000)
        : timeoutMs + analyzeBudgetMs + 1000;

    auto tryOpenInput = [&](const StreamOpenOptions& attemptOptions, string& errorText) -> bool {
        AVDictionary* opts = nullptr;
        ApplyOpenOptions(&opts, url, attemptOptions);
        resources_.openAbortRequested.store(false);
        resources_.openDeadlineMs.store(SteadyNowMs() + openBudgetMs);
        resources_.ic = avformat_alloc_context();
        SetInterruptCallback(resources_.ic, resources_);
#if LIBAVFORMAT_VERSION_MAJOR >= 59
        const AVInputFormat* forcedInputFormat = isHls ? av_find_input_format("hls") : nullptr;
#else
        AVInputFormat* forcedInputFormat = const_cast<AVInputFormat*>(
            isHls ? av_find_input_format("hls") : nullptr);
#endif
        const int openResult = avformat_open_input(&resources_.ic, url, forcedInputFormat, &opts);
        av_dict_free(&opts);
        if (openResult != 0)
        {
            if (openResult == AVERROR_EXIT)
                errorText = BuildInterruptErrorText("Connection", timeoutMs);
            else
                errorText = BuildInputErrorText(openResult, sourceType);
            if (resources_.ic)
                avformat_close_input(&resources_.ic);
            resources_.openDeadlineMs.store(0);
            return false;
        }

        if (attemptOptions.noBuffer && resources_.ic)
            resources_.ic->flags |= AVFMT_FLAG_NOBUFFER;
        return true;
    };

    string openError;
    if (!tryOpenInput(activeOptions, openError))
    {
        if (!isRtsp)
        {
            state_.lastError = openError;
            Logger::Instance().Log(
                LogLevel::Error,
                "demux",
                "open.fail",
                "Demux open failed",
                {
                    { "url", url ? url : "" },
                    { "error", openError },
                });
            return false;
        }

        const bool triedTcpFirst = activeOptions.forceTcpForRtsp;
        const char* firstTransport = triedTcpFirst ? "TCP" : "UDP";
        const char* retryTransport = triedTcpFirst ? "UDP" : "TCP";
        Logger::Instance().Log(
            LogLevel::Warning,
            "demux",
            "open.rtsp_retry",
            std::string("RTSP/") + firstTransport + " open failed, retrying with stable " + retryTransport,
            {
                { "url", url ? url : "" },
                { "retry_profile", "stable" },
            });
        avformat_close_input(&resources_.ic);

        StreamOpenOptions retryOptions =
            BuildStableRtspRetryOptions(activeOptions, !activeOptions.forceTcpForRtsp);
        string retryError;
        if (!tryOpenInput(retryOptions, retryError))
        {
            state_.lastError = string("RTSP open failed over ") + firstTransport +
                ", and " + retryTransport + " retry also failed: " + retryError;
            Logger::Instance().Log(
                LogLevel::Error,
                "demux",
                "open.rtsp_retry_fail",
                "RTSP open and fallback retry both failed",
                {
                    { "url", url ? url : "" },
                    { "first_transport", firstTransport },
                    { "first_error", openError },
                    { "retry_transport", retryTransport },
                    { "retry_error", retryError },
                });
            return false;
        }

        activeOptions = retryOptions;
        state_.openOptions = retryOptions;
        Logger::Instance().Log(
            LogLevel::Warning,
            "demux",
            "open.rtsp_transport_fallback",
            std::string("RTSP transport fallback: ") + firstTransport + " -> stable " + retryTransport,
            {
                { "url", url ? url : "" },
                { "retry_profile", "stable" },
            });
    }

    state_.sourceUrl = source;

    const int streamInfoResult = avformat_find_stream_info(resources_.ic, nullptr);
    resources_.openDeadlineMs.store(0);
    if (streamInfoResult < 0)
    {
        if (streamInfoResult == AVERROR_EXIT)
            state_.lastError = BuildInterruptErrorText("Stream analysis", openBudgetMs);
        else
            state_.lastError = BuildInputErrorText(streamInfoResult, sourceType);
        Logger::Instance().Log(
            LogLevel::Error,
            "demux",
            "stream_info.fail",
            "Failed to read stream info",
            {
                { "url", url ? url : "" },
                { "error", state_.lastError },
            });
        avformat_close_input(&resources_.ic);
        state_.sourceUrl.clear();
        trackCatalog_.ResetLocked();
        return false;
    }

    state_.totalMs.store(
        resources_.ic->duration > 0
            ? static_cast<int>(resources_.ic->duration / (AV_TIME_BASE / 1000))
            : 0);
    state_.bitrate.store(resources_.ic->bit_rate > 0 ? resources_.ic->bit_rate : 0);

    av_dump_format(resources_.ic, 0, url, 0);

    state_.videoStream = av_find_best_stream(resources_.ic, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (state_.videoStream >= 0)
    {
        AVStream* vs = resources_.ic->streams[state_.videoStream];
        state_.width.store(vs->codecpar->width);
        state_.height.store(vs->codecpar->height);
        if (vs->avg_frame_rate.den > 0)
        {
            state_.videoFpsNum.store(vs->avg_frame_rate.num);
            state_.videoFpsDen.store(vs->avg_frame_rate.den);
        }
        else if (vs->r_frame_rate.den > 0)
        {
            state_.videoFpsNum.store(vs->r_frame_rate.num);
            state_.videoFpsDen.store(vs->r_frame_rate.den);
        }
        else
        {
            state_.videoFpsNum.store(0);
            state_.videoFpsDen.store(1);
        }
    }
    else
    {
        state_.width.store(0);
        state_.height.store(0);
        state_.videoFpsNum.store(0);
        state_.videoFpsDen.store(1);
    }

    state_.audioStream = av_find_best_stream(resources_.ic, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    trackCatalog_.RebuildLocked();
    trackCatalog_.UpdateSelectedAudioMetadataLocked();

    return true;
}

bool DemuxOpenController::Open(const char* url, const StreamOpenOptions& options)
{
    SetOpenOptions(options);
    return Open(url);
}

void DemuxOpenController::Close()
{
    resources_.openAbortRequested.store(true);
    resources_.openDeadlineMs.store(0);
    lock_guard<mutex> lock(resources_.mux);
    if (resources_.ic)
        avformat_close_input(&resources_.ic);

    state_.sourceUrl.clear();
    trackCatalog_.ResetLocked();
    state_.videoStream = -1;
    state_.audioStream = -1;
    state_.totalMs.store(0);
    state_.width.store(0);
    state_.height.store(0);
    state_.sampleRate.store(0);
    state_.channels.store(0);
    state_.videoFpsNum.store(0);
    state_.videoFpsDen.store(1);
    state_.bitrate.store(0);
}

void DemuxOpenController::SetOpenOptions(const StreamOpenOptions& options)
{
    lock_guard<mutex> lock(resources_.mux);
    state_.openOptions = options;
}

StreamOpenOptions DemuxOpenController::GetOpenOptions()
{
    lock_guard<mutex> lock(resources_.mux);
    return state_.openOptions;
}

string DemuxOpenController::GetLastError()
{
    lock_guard<mutex> lock(resources_.mux);
    return state_.lastError;
}
