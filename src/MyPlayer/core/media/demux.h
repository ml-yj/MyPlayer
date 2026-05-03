#pragma once

#include "demux_types.h"
#include "../session/stream_config.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

struct AVCodecParameters;
struct AVPacket;
struct AVRational;
struct AVFormatContext;

struct DemuxResources
{
    mutable std::mutex mux;
    AVFormatContext* ic = nullptr;
    std::atomic<long long> openDeadlineMs{ 0 };
    std::atomic<bool> openAbortRequested{ false };
};

struct DemuxState
{
    std::string sourceUrl;
    std::string lastError;
    int videoStream = -1;
    int audioStream = -1;
    StreamOpenOptions openOptions = StreamOpenOptions::DefaultFile();
    std::vector<AudioStreamInfo> audioStreams;
    std::vector<SubtitleStreamInfo> subtitleStreams;
    std::atomic<int> totalMs{ 0 };
    std::atomic<int> width{ 0 };
    std::atomic<int> height{ 0 };
    std::atomic<int> sampleRate{ 0 };
    std::atomic<int> channels{ 0 };
    std::atomic<int> videoFpsNum{ 0 };
    std::atomic<int> videoFpsDen{ 1 };
    std::atomic<long long> bitrate{ 0 };
};

class DemuxOpenController;
class DemuxPacketReader;
class DemuxTrackCatalog;
class SubtitleTrackLoader;

class Demux
{
public:
    Demux();
    virtual ~Demux();

    virtual bool Open(const char* url);
    virtual bool Open(const char* url, const StreamOpenOptions& options);
    virtual AVPacket* Read();
    virtual AVPacket* ReadVideo();
    virtual bool IsAudio(AVPacket* pkt);

    virtual AVCodecParameters* CopyVPara();
    virtual AVCodecParameters* CopyAPara();
    virtual bool CopyRecordingVideoStream(
        AVCodecParameters** outParameters, AVRational* outTimeBase, int* outStreamIndex,
        std::string* outCodecName = nullptr);
    virtual bool CopyRecordingAudioStream(
        AVCodecParameters** outParameters, AVRational* outTimeBase, int* outStreamIndex,
        std::string* outCodecName = nullptr);

    virtual bool Seek(double pos);
    virtual bool SeekMs(long long ms);
    virtual void Clear();
    virtual void Close();

    int GetAudioStreamCount();
    AudioStreamInfo GetAudioStreamInfo(int idx);
    void SetAudioStream(int idx);
    int GetCurrentAudioIndex();
    int GetSubtitleStreamCount();
    SubtitleStreamInfo GetSubtitleStreamInfo(int idx);
    bool LoadSubtitleTrack(int idx, std::vector<SubtitleCueData>& cues);

    void SetOpenOptions(const StreamOpenOptions& options);
    StreamOpenOptions GetOpenOptions();
    std::string GetLastError();
    bool IsOpen() const;
    bool HasVideoStream() const;
    bool IsSeekable() const;

    std::vector<AudioStreamInfo>& audioStreams;
    std::vector<SubtitleStreamInfo>& subtitleStreams;
    std::atomic<int>& totalMs;
    std::atomic<int>& width;
    std::atomic<int>& height;
    std::atomic<int>& sampleRate;
    std::atomic<int>& channels;
    std::atomic<int>& videoFpsNum;
    std::atomic<int>& videoFpsDen;
    std::atomic<long long>& bitrate;

private:
    DemuxState state_;
    DemuxResources resources_;
    std::unique_ptr<SubtitleTrackLoader> subtitleTrackLoader_;
    std::unique_ptr<DemuxTrackCatalog> trackCatalog_;
    std::unique_ptr<DemuxPacketReader> packetReader_;
    std::unique_ptr<DemuxOpenController> openController_;
};
