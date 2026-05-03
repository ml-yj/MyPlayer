#pragma once

#include "demux_thread_shared.h"

class LiveStreamController;
class StreamSessionCore;
struct AVPacket;

class PacketPump
{
public:

    PacketPump(DemuxThreadState& state, SessionResources& resources,
        LiveStreamController& liveController, StreamSessionCore& sessionCore);

    void Run();

private:

    void UpdatePlaybackClock() const;

    bool ReadNextPacket(AVPacket*& pkt, Demux*& localDemux,
        quint64& packetGeneration, quint64& packetSerial) const;

    void HandlePacketReadSuccess();

    void SetConsumerPause(bool paused) const;

    void MaybeAdvanceBufferedPlayback(
        int& readyStreak, long long nowMs, long long& lastPlaybackResumeMs,
        long long lastBufferingStartMs, long long lastPrimingStartMs) const;

    void DispatchPacket(AVPacket* pkt, bool isAudioPacket, quint64 packetGeneration, quint64 packetSerial) const;

    void IssueDrainIfNeeded(quint64 packetGeneration, quint64 packetSerial, quint64& drainIssuedSerial) const;
    bool IsOutputDrainComplete() const;
    void CompleteFiniteDrain();

    void HandleFileReadMiss(quint64 packetGeneration, quint64 packetSerial, quint64& drainIssuedSerial, int& readyStreak);
    void HandleNetworkVodReadMiss(
        quint64 packetGeneration, quint64 packetSerial, quint64& drainIssuedSerial,
        int& readyStreak, long long nowMs, long long& lastBufferingStartMs);
    void HandleLiveReadMiss(int& readyStreak, long long nowMs, long long& lastBufferingStartMs);

    DemuxThreadState& state;
    SessionResources& resources;
    LiveStreamController& liveController;
    StreamSessionCore& sessionCore;
};
