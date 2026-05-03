#pragma once

#include "demux_thread_shared.h"

namespace PacketPumpBuffering
{
struct BufferProfile
{
    int startupVideoPackets = 0;
    int startupAudioPackets = 0;
    int startupAudioBufferedMs = 0;
    int resumeVideoPackets = 0;
    int resumeAudioPackets = 0;
    int lowWaterVideoPackets = 0;
    int lowWaterAudioPackets = 0;
    int resumeAudioBufferedMs = 0;
    int lowWaterAudioBufferedMs = 0;
    bool holdDuringPriming = false;
    bool holdDuringBuffering = false;
};

int ComputeBufferProfileLevel(const DemuxThreadState& state, StreamPlaybackKind playbackKind);
BufferProfile BuildBufferProfile(const DemuxThreadState& state, const SessionResources& resources);
const char* BufferReasonLabel(StreamPlaybackKind playbackKind);
bool IsBufferReady(const BufferProfile& profile, const SessionResources& resources,
    bool recovering, bool allowQueueOnlyAudioReady, bool allowDecodedVideoAsReady);
void LogPrimingWaitState(const DemuxThreadState& state, const SessionResources& resources,
    const BufferProfile& profile, bool recovering);
bool IsBufferLow(const BufferProfile& profile, const SessionResources& resources);
long long SteadyNowMs();
int ComputeReadyStreakTarget(const DemuxThreadState& state, StreamSessionState sessionState);
int ComputeRebufferCooldownMs(const DemuxThreadState& state);
int ComputeMinBufferHoldMs(const DemuxThreadState& state, StreamSessionState sessionState);
void PublishBufferProfile(DemuxThreadState& state, const BufferProfile& profile, int profileLevel,
    int readyStreak, int lowWaterStreak);
bool ShouldStartNetworkVodRebuffer(const DemuxThreadState& state, const SessionResources& resources,
    int lowWaterStreak);
bool ShouldStartLowWaterBuffering(DemuxThreadState& state, const SessionResources& resources,
    int lowWaterStreak, long long nowMs, long long lastPlaybackResumeMs, long long lastRenderedVideoMs);
}
