#include "stream_config.h"

#include <algorithm>

namespace
{
bool StartsWithNoCase(const std::string& value, const char* prefix)
{
    if (!prefix)
        return false;

    const std::size_t prefixLen = std::char_traits<char>::length(prefix);
    if (value.size() < prefixLen)
        return false;

    for (std::size_t i = 0; i < prefixLen; ++i)
    {
        char a = value[i];
        char b = prefix[i];
        if (a >= 'A' && a <= 'Z')
            a = static_cast<char>(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z')
            b = static_cast<char>(b - 'A' + 'a');
        if (a != b)
            return false;
    }

    return true;
}

bool ContainsNoCase(const std::string& value, const char* needle)
{
    if (!needle)
        return false;

    std::string loweredValue = value;
    std::string loweredNeedle = needle;
    for (char& ch : loweredValue)
    {
        if (ch >= 'A' && ch <= 'Z')
            ch = static_cast<char>(ch - 'A' + 'a');
    }
    for (char& ch : loweredNeedle)
    {
        if (ch >= 'A' && ch <= 'Z')
            ch = static_cast<char>(ch - 'A' + 'a');
    }
    return loweredValue.find(loweredNeedle) != std::string::npos;
}

void ApplyContinuousLiveBufferStrategy(
    StreamSourcePolicy& policy,
    StreamPlaybackKind playbackKind,
    const StreamOpenOptions& options)
{
    policy.forceBaseTuneInBalancedAudioMaster =
        playbackKind == StreamPlaybackKind::Live
        && !options.enableLowLatency
        && options.liveClockPolicy == LiveClockPolicy::AudioMaster;
    policy.balancedProbeFloorBytes = 256 * 1024;
    policy.balancedAnalyzeDurationUs = 1000 * 1000;
    policy.lowLatencyProbeFloorBytes = 64 * 1024;
    policy.lowLatencyAnalyzeDurationUs = 400 * 1000;
    policy.firstVideoGateTimeoutMs = options.enableLowLatency ? 5000 : 7000;
    policy.liveVideoProgressGraceMs =
        (options.enableLowLatency
            || options.liveClockPolicy == LiveClockPolicy::AudioMasterDropLateVideo)
        ? 1200
        : 2200;
    policy.liveLowWaterStreakThreshold = options.enableLowLatency ? 4 : 5;
    policy.liveRebufferCooldownFloorMs = options.enableLowLatency ? 250 : 450;
}
}

StreamSourceType ResolveStreamSourceType(const std::string& url)
{
    if (url.empty())
        return StreamSourceType::Unknown;

    if (StartsWithNoCase(url, "rtsp://"))
        return StreamSourceType::Rtsp;
    if (StartsWithNoCase(url, "rtmp://"))
        return StreamSourceType::Rtmp;
    if (StartsWithNoCase(url, "webrtc://")
        || StartsWithNoCase(url, "whip://")
        || StartsWithNoCase(url, "whep://"))
    {
        return StreamSourceType::WebRtc;
    }

    if (StartsWithNoCase(url, "http://") || StartsWithNoCase(url, "https://"))
    {
        if (ContainsNoCase(url, ".m3u8"))
            return StreamSourceType::Hls;
        if (ContainsNoCase(url, ".flv"))
            return StreamSourceType::HttpFlv;
        return StreamSourceType::HttpProgressive;
    }

    if (StartsWithNoCase(url, "udp://")
        || StartsWithNoCase(url, "tcp://")
        || StartsWithNoCase(url, "srt://"))
    {
        return StreamSourceType::Unknown;
    }

    return StreamSourceType::LocalFile;
}

bool UsesContinuousLiveBufferStrategy(StreamSourceType sourceType)
{
    return sourceType == StreamSourceType::Rtsp
        || sourceType == StreamSourceType::Rtmp
        || sourceType == StreamSourceType::HttpFlv;
}

StreamSourcePolicy ResolveStreamSourcePolicy(
    StreamPlaybackKind playbackKind,
    StreamSourceType sourceType,
    const StreamOpenOptions& options)
{
    StreamSourcePolicy policy;

    policy.holdAudioUntilFirstVideo = playbackKind == StreamPlaybackKind::Live;
    policy.firstVideoGateTimeoutMs = playbackKind == StreamPlaybackKind::Live
        ? (options.enableLowLatency ? 8000 : 12000)
        : 0;
    policy.liveVideoProgressGraceMs = playbackKind == StreamPlaybackKind::Live
        ? ((options.enableLowLatency
            || options.liveClockPolicy == LiveClockPolicy::AudioMasterDropLateVideo) ? 1500 : 2500)
        : 0;
    policy.liveLowWaterStreakThreshold = playbackKind == StreamPlaybackKind::Live ? 5 : 0;
    policy.liveRebufferCooldownFloorMs = playbackKind == StreamPlaybackKind::Live ? 500 : 0;

    if (UsesContinuousLiveBufferStrategy(sourceType))
        ApplyContinuousLiveBufferStrategy(policy, playbackKind, options);

    switch (sourceType)
    {
    case StreamSourceType::Rtsp:
        policy.allowTransportFallback = true;
        policy.allowAdaptiveAutoRecover = playbackKind == StreamPlaybackKind::Live;
        policy.disableFragileLiveHardwareDecode = false;
        policy.balancedProbeFloorBytes = 512 * 1024;
        policy.balancedAnalyzeDurationUs = 1500 * 1000;
        policy.lowLatencyProbeFloorBytes = 128 * 1024;
        policy.lowLatencyAnalyzeDurationUs = 1000 * 1000;
        policy.firstVideoGateTimeoutMs = options.enableLowLatency ? 8000 : 12000;
        policy.liveVideoProgressGraceMs =
            (options.enableLowLatency
                || options.liveClockPolicy == LiveClockPolicy::AudioMasterDropLateVideo)
            ? 1800
            : 3000;
        policy.liveLowWaterStreakThreshold = options.enableLowLatency ? 4 : 6;
        policy.liveRebufferCooldownFloorMs = options.enableLowLatency ? 300 : 650;
        break;
    case StreamSourceType::Rtmp:
    case StreamSourceType::HttpFlv:
        break;
    case StreamSourceType::Hls:
        policy.forceBaseTuneInBalancedAudioMaster =
            playbackKind == StreamPlaybackKind::Live
            && !options.enableLowLatency
            && options.liveClockPolicy == LiveClockPolicy::AudioMaster;
        policy.balancedProbeFloorBytes = 1024 * 1024;
        policy.balancedAnalyzeDurationUs = 2500 * 1000;
        policy.lowLatencyProbeFloorBytes = 256 * 1024;
        policy.lowLatencyAnalyzeDurationUs = 1200 * 1000;
        policy.firstVideoGateTimeoutMs = options.enableLowLatency ? 9000 : 12000;
        policy.liveVideoProgressGraceMs =
            (options.enableLowLatency
                || options.liveClockPolicy == LiveClockPolicy::AudioMasterDropLateVideo)
            ? 2500
            : 4500;
        policy.liveLowWaterStreakThreshold = options.enableLowLatency ? 6 : 8;
        policy.liveRebufferCooldownFloorMs = options.enableLowLatency ? 600 : 900;
        break;
    default:
        break;
    }

    return policy;
}

bool IsNetworkUrl(const std::string& url)
{
    return StartsWithNoCase(url, "rtsp://")
        || StartsWithNoCase(url, "rtmp://")
        || StartsWithNoCase(url, "http://")
        || StartsWithNoCase(url, "https://")
        || StartsWithNoCase(url, "udp://")
        || StartsWithNoCase(url, "tcp://")
        || StartsWithNoCase(url, "srt://");
}

bool IsNetworkUrl(const char* url)
{
    return url ? IsNetworkUrl(std::string(url)) : false;
}
