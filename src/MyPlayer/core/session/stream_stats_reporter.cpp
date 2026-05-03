
#include "stream_stats_reporter.h"

#include "../audio/audio_thread.h"
#include "../media/decode.h"
#include "../media/demux.h"
#include "../video/video_thread.h"
#include "../../common/diagnostics/logger.h"
#include "../../features/asr/whisper_thread.h"
#include "../../features/detector/detector_thread.h"

extern "C" {
#include <libavutil/pixdesc.h>
#include <libavutil/samplefmt.h>
}

#include <string>

namespace
{

const char* ColorSpaceName(int cs)
{
    switch (cs) {
    case AVCOL_SPC_BT709:       return "BT.709";
    case AVCOL_SPC_BT2020_NCL:  return "BT.2020 NCL";
    case AVCOL_SPC_BT2020_CL:   return "BT.2020 CL";
    case AVCOL_SPC_SMPTE170M:   return "SMPTE 170M";
    case AVCOL_SPC_SMPTE240M:   return "SMPTE 240M";
    case AVCOL_SPC_BT470BG:     return "BT.470 BG";
    case AVCOL_SPC_UNSPECIFIED: return "Unspecified";
    default:                    return "Unknown";
    }
}

const char* ColorRangeName(int cr)
{
    switch (cr) {
    case AVCOL_RANGE_MPEG:        return "Limited";
    case AVCOL_RANGE_JPEG:        return "Full";
    case AVCOL_RANGE_UNSPECIFIED: return "Unspecified";
    default:                      return "Unknown";
    }
}

const char* ColorTrcName(int trc)
{
    switch (trc) {
    case AVCOL_TRC_BT709:        return "BT.709";
    case AVCOL_TRC_SMPTE2084:    return "PQ (HDR10)";
    case AVCOL_TRC_ARIB_STD_B67: return "HLG";
    case AVCOL_TRC_LINEAR:       return "Linear";
    case AVCOL_TRC_GAMMA22:      return "Gamma 2.2";
    case AVCOL_TRC_GAMMA28:      return "Gamma 2.8";
    case AVCOL_TRC_UNSPECIFIED:  return "Unspecified";
    default:                     return "Unknown";
    }
}

const char* ChannelLayoutName(int ch)
{
    switch (ch) {
    case 1: return "Mono";
    case 2: return "Stereo";
    case 6: return "5.1";
    case 8: return "7.1";
    default: return "";
    }
}
}

StreamStatsReporter::StreamStatsReporter(DemuxThreadState& stateRef, SessionResources& resourcesRef)
    : state(stateRef)
    , resources(resourcesRef)
{
}

void StreamStatsReporter::PublishStatusEventLocked(const std::string& text)
{
    state.statusEventText = text;
    state.statusEventGeneration.fetch_add(1);
    if (!text.empty())
        Logger::Instance().Log(LogLevel::Info, "session", "status_event", text);
}

int StreamStatsReporter::FetchRenderedFrames()
{
    std::lock_guard<std::mutex> lock(resources.mux);
    if (resources.vt)
        return resources.vt->TakeRenderedFrames();
    return 0;
}

std::string StreamStatsReporter::GetOsdDetail()
{
    std::lock_guard<std::mutex> lock(resources.mux);
    std::string s;

    s += "Stream:     ";
    if (state.isLiveStream.load())
        s += "Live";
    else if (IsNetworkUrl(state.currentUrl))
        s += "Network";
    else
        s += "File";
    s += state.activeOpenOptions.enableLowLatency ? " | low-latency\n" : " | normal\n";

    s += "State:      ";
    s += StreamSessionStateName(state.streamState.load());
    s += "\n";

    s += "Source:     ";
    s += StreamSourceTypeName(state.sourceType.load());
    s += "\n";

    if (IsNetworkUrl(state.currentUrl))
    {
        s += "Open Lat:   " + std::to_string(state.openLatencyMs.load()) + " ms\n";
        s += "Clock:      ";
        s += LiveClockPolicyName(state.activeOpenOptions.liveClockPolicy);
        s += "\n";
        s += "Reconnect:  " + std::to_string(state.reconnectCount) + "/"
            + std::to_string(state.activeOpenOptions.reconnect.maxAttempts)
            + " | ok " + std::to_string(state.reconnectSuccessCount.load())
            + " | buffer " + std::to_string(state.bufferingEventCount.load())
            + " | miss " + std::to_string(state.consecutiveReadFailures.load()) + "\n";
        s += "Policy:     " + AdaptiveRecoveryModeLabel(state.adaptiveRecoveryStage) + "\n";
        if (!state.adaptiveHint.empty())
            s += "Hint:       " + state.adaptiveHint + "\n";
        s += "Tune:       " + (state.liveTuneProfile.empty() ? std::string("off") : state.liveTuneProfile) + "\n";
        s += "Targets:    audio " + std::to_string(state.runtimeAudioTargetMs)
            + " ms | vlead " + std::to_string(state.runtimeVideoLeadMs)
            + " ms | late " + std::to_string(state.runtimeLateDropMs) + " ms\n";
        s += "BufferCtl:  prime " + std::string(state.primingPending.load() ? "pending" : "clear")
            + " | start V" + std::to_string(state.runtimeStartupVideoPackets.load())
            + "/A" + std::to_string(state.runtimeStartupAudioPackets.load())
            + "/" + std::to_string(state.runtimeStartupAudioBufferedMs.load()) + "ms"
            + " | resume V" + std::to_string(state.runtimeResumeVideoPackets.load())
            + "/A" + std::to_string(state.runtimeResumeAudioPackets.load())
            + "/" + std::to_string(state.runtimeResumeAudioBufferedMs.load()) + "ms"
            + " | low V" + std::to_string(state.runtimeLowWaterVideoPackets.load())
            + "/A" + std::to_string(state.runtimeLowWaterAudioPackets.load())
            + "/" + std::to_string(state.runtimeLowWaterAudioBufferedMs.load()) + "ms"
            + " | profile " + std::to_string(state.runtimeBufferProfileLevel.load())
            + " | ready " + std::to_string(state.runtimeReadyStreak.load())
            + "/" + std::to_string(state.runtimeReadyTarget.load())
            + " | hold " + std::to_string(state.runtimeMinBufferHoldMs.load()) + "ms"
            + " | cool " + std::to_string(state.runtimeRebufferCooldownMs.load()) + "ms"
            + " | streak " + std::to_string(state.runtimeLowWaterStreak.load()) + "\n";
        s += "BufferDbg:  resume " + std::to_string(state.runtimePlaybackResumeCount.load())
            + " | suppress " + std::to_string(state.runtimeRebufferSuppressedCount.load()) + "\n";
        if (resources.at)
        {
            s += "Audio Sync:  catch-up " + std::to_string(resources.at->GetLiveCatchUpCount())
                + " | throttle " + std::to_string(resources.at->GetProgressiveThrottleCount()) + "\n";
        }
        if (state.runtimeDetectorBaseSkipFrames > 0)
        {
            s += "Detector:   skip " + std::to_string(state.runtimeDetectorSkipFrames)
                + " (base " + std::to_string(state.runtimeDetectorBaseSkipFrames) + ")\n";
        }
        s += "AI Sched:   GPU q " + std::to_string(state.aiGpuQueueDepth.load())
            + " / a " + std::to_string(state.aiGpuActiveTasks.load())
            + " | CPU q " + std::to_string(state.aiCpuQueueDepth.load())
            + " / a " + std::to_string(state.aiCpuActiveTasks.load())
            + " | done " + std::to_string(state.aiCompletedTasks.load())
            + " | drop " + std::to_string(state.aiDroppedTasks.load())
            + " | cancel " + std::to_string(state.aiCancelledTasks.load())
            + " | wait " + std::to_string(state.aiAverageWaitMs.load())
            + "/" + std::to_string(state.aiLastWaitMs.load()) + " ms\n";
        const DiagnosticsMetricsSnapshot diag = MetricsRegistry::Instance().GetSnapshot();
        s += "Diag:       log " + std::to_string(diag.logLinesTotal)
            + " | warn " + std::to_string(diag.logWarningLines)
            + " | err " + std::to_string(diag.logErrorLines)
            + " | qt " + std::to_string(diag.qtWarnings)
            + "/" + std::to_string(diag.qtCriticals)
            + " | dump " + std::to_string(diag.crashDumpsWritten) + "\n";
        if (!state.liveTuneHint.empty())
            s += "Tune Hint:  " + state.liveTuneHint + "\n";
    }

    Decode* vDec = resources.vt ? resources.vt->GetDecode() : nullptr;
    Decode* aDec = resources.at ? resources.at->GetDecode() : nullptr;
    s += "Decode:     video ";
    s += (vDec && vDec->isHwAccel.load()) ? "GPU(CUDA)" : "CPU";
    s += " | audio ";
    s += aDec ? "CPU" : "off";
    s += "\n";
    if (vDec)
    {
        std::string codec = vDec->codecName.empty() ? "N/A" : vDec->codecName;
        const bool hw = vDec->isHwAccel.load();
        s += "Codec:      " + codec + (hw ? " (CUDA HW)" : " (SW)") + "\n";

        const int pf = vDec->pixFmt.load();
        if (pf >= 0)
        {
            const char* name = av_get_pix_fmt_name(static_cast<AVPixelFormat>(pf));
            s += "Pixel Fmt:  ";
            s += (name ? name : "Unknown");
            s += "\n";
        }

        const int cs = vDec->colorSpace.load();
        const int cr = vDec->colorRange.load();
        if (cs >= 0 || cr >= 0)
        {
            s += "Color:      ";
            s += ColorSpaceName(cs);
            s += " / ";
            s += ColorRangeName(cr);
            s += "\n";
        }

        const int trc = vDec->colorTrc.load();
        if (trc >= 0)
        {
            s += "Transfer:   ";
            s += ColorTrcName(trc);
            if (trc == AVCOL_TRC_SMPTE2084 || trc == AVCOL_TRC_ARIB_STD_B67)
                s += " [HDR]";
            s += "\n";
        }
    }

    if (aDec)

    {
        std::string codec = aDec->codecName.empty() ? "N/A" : aDec->codecName;
        s += "Audio:      " + codec + "\n";

        const int sr = state.audioSampleRate.load();
        const int sf = aDec->sampleFmtOut.load();
        s += "Sample:     " + std::to_string(sr) + " Hz";
        if (sf >= 0)
        {
            const char* sfName = av_get_sample_fmt_name(static_cast<AVSampleFormat>(sf));
            if (sfName)
            {
                s += " / ";
                s += sfName;
            }
        }
        s += "\n";

        const int ch = state.audioChannels.load();
        s += "Channels:   " + std::to_string(ch);
        const char* layout = ChannelLayoutName(ch);
        if (layout[0])
        {
            s += " (";
            s += layout;
            s += ")";
        }
        s += "\n";

        if (state.isLiveStream.load())
            s += "Audio Out:  " + std::to_string(resources.at->GetAudioDeviceBufferMs()) + " ms\n";
    }

    if (resources.demux)
    {
        const int audioCount = resources.demux->GetAudioStreamCount();
        if (audioCount > 0)
        {
            int currentAudio = resources.demux->GetCurrentAudioIndex();
            if (currentAudio < 0)
                currentAudio = 0;

            s += "Audio Trk:  " + std::to_string(currentAudio + 1) + "/" + std::to_string(audioCount);
            const AudioStreamInfo info = resources.demux->GetAudioStreamInfo(currentAudio);
            if (!info.language.empty())
                s += " | " + info.language;
            if (!info.title.empty())
                s += " | " + info.title;
            s += "\n";
        }
        else
        {
            s += "Audio Trk:  None\n";
        }

        const int subtitleCount = resources.demux->GetSubtitleStreamCount();
        int textSubtitleCount = 0;
        for (const SubtitleStreamInfo& info : resources.demux->subtitleStreams)
        {
            if (info.isTextBased)
                ++textSubtitleCount;
        }

        s += "Subtitles:  ";
        if (subtitleCount <= 0)
        {
            s += "No embedded\n";
        }
        else
        {
            s += "Yes, total " + std::to_string(subtitleCount);
            if (textSubtitleCount == subtitleCount)
                s += " (all text)";
            else
                s += " (text " + std::to_string(textSubtitleCount) + ")";
            s += "\n";
        }
    }

    if (resources.det)
    {
        s += "DET/TRK:    " + resources.det->GetPipelineSummary().toStdString() + "\n";
    }
    else
    {
        s += "DET/TRK:    off\n";
    }

    if (resources.wt)
    {
        s += "ASR/VAD:    " + resources.wt->GetPipelineSummary().toStdString() + "\n";

    }
    else
    {
        s += "ASR/VAD:    off\n";
    }

    if (vDec && resources.at)
    {
        const long long audioPts = resources.at->GetPts();
        const long long videoPts = vDec->pts.load();
        const long long diff = videoPts - audioPts;
        s += "A/V Sync:   ";
        if (diff >= 0)
            s += "+";
        s += std::to_string(diff) + " ms\n";
    }

    if (resources.vt)
    {
        s += "Video Buf:  " + std::to_string(resources.vt->GetQueueSize())
            + "/" + std::to_string(resources.vt->maxList);
        if (state.activeOpenOptions.enableLowLatency && IsNetworkUrl(state.currentUrl))
        {
            s += " | drop-q " + std::to_string(resources.vt->GetDroppedPacketCount());
            s += " | drop-late " + std::to_string(resources.vt->GetLateFrameDropCount());
        }
        s += "\n";
    }

    if (resources.at)
    {
        s += "Audio Buf:  " + std::to_string(resources.at->GetQueueSize())
            + "/" + std::to_string(resources.at->maxList);
        if (state.activeOpenOptions.enableLowLatency && IsNetworkUrl(state.currentUrl))
        {
            s += " | drop-q " + std::to_string(resources.at->GetDroppedPacketCount());
            s += " | catch-up " + std::to_string(resources.at->GetLiveCatchUpCount());
        }
        s += "\n";
    }

    return s;
}

StreamStatsSnapshot StreamStatsReporter::GetStreamStats()
{
    std::lock_guard<std::mutex> lock(resources.mux);
    StreamStatsSnapshot stats;
    stats.state = state.streamState.load();
    stats.playbackKind = state.playbackKind.load();
    stats.sourceType = state.sourceType.load();
    stats.isNetwork = IsNetworkUrl(state.currentUrl);
    stats.isLive = state.isLiveStream.load();
    stats.lowLatencyEnabled = state.activeOpenOptions.enableLowLatency;
    stats.openLatencyMs = state.openLatencyMs.load();
    stats.reconnectAttempts = state.reconnectCount;
    stats.reconnectSuccesses = state.reconnectSuccessCount.load();
    stats.bufferingEvents = state.bufferingEventCount.load();
    stats.consecutiveReadFailures = state.consecutiveReadFailures.load();
    stats.videoQueuePackets = resources.vt ? resources.vt->GetQueueSize() : 0;
    stats.videoQueueLimit = resources.vt ? resources.vt->maxList : 0;
    stats.audioQueuePackets = resources.at ? resources.at->GetQueueSize() : 0;
    stats.audioQueueLimit = resources.at ? resources.at->maxList : 0;
    stats.audioDeviceBufferedMs = resources.at ? static_cast<int>(resources.at->GetAudioDeviceBufferMs()) : 0;
    stats.droppedVideoPackets = resources.vt ? resources.vt->GetDroppedPacketCount() : 0;
    stats.droppedAudioPackets = resources.at ? resources.at->GetDroppedPacketCount() : 0;
    stats.droppedLateVideoFrames = resources.vt ? resources.vt->GetLateFrameDropCount() : 0;
    stats.audioCatchUpEvents = resources.at ? resources.at->GetLiveCatchUpCount() : 0;
    stats.audioThrottleEvents = resources.at ? resources.at->GetProgressiveThrottleCount() : 0;
    stats.adaptiveMode = AdaptiveRecoveryModeLabel(state.adaptiveRecoveryStage);
    stats.adaptiveHint = state.adaptiveHint;
    stats.liveTuneProfile = state.liveTuneProfile;
    stats.liveTuneHint = state.liveTuneHint;
    stats.runtimeAudioTargetMs = state.runtimeAudioTargetMs;
    stats.runtimeVideoLeadMs = state.runtimeVideoLeadMs;
    stats.runtimeLateDropMs = state.runtimeLateDropMs;
    stats.primingPending = state.primingPending.load();
    stats.runtimeStartupVideoPackets = state.runtimeStartupVideoPackets.load();
    stats.runtimeStartupAudioPackets = state.runtimeStartupAudioPackets.load();
    stats.runtimeResumeVideoPackets = state.runtimeResumeVideoPackets.load();
    stats.runtimeResumeAudioPackets = state.runtimeResumeAudioPackets.load();
    stats.runtimeLowWaterVideoPackets = state.runtimeLowWaterVideoPackets.load();
    stats.runtimeLowWaterAudioPackets = state.runtimeLowWaterAudioPackets.load();
    stats.runtimeStartupAudioBufferedMs = state.runtimeStartupAudioBufferedMs.load();
    stats.runtimeResumeAudioBufferedMs = state.runtimeResumeAudioBufferedMs.load();
    stats.runtimeLowWaterAudioBufferedMs = state.runtimeLowWaterAudioBufferedMs.load();
    stats.runtimeBufferProfileLevel = state.runtimeBufferProfileLevel.load();
    stats.runtimeReadyStreak = state.runtimeReadyStreak.load();
    stats.runtimeReadyTarget = state.runtimeReadyTarget.load();
    stats.runtimeRebufferCooldownMs = state.runtimeRebufferCooldownMs.load();
    stats.runtimeMinBufferHoldMs = state.runtimeMinBufferHoldMs.load();
    stats.runtimeLowWaterStreak = state.runtimeLowWaterStreak.load();
    stats.runtimePlaybackResumeCount = state.runtimePlaybackResumeCount.load();
    stats.runtimeRebufferSuppressedCount = state.runtimeRebufferSuppressedCount.load();
    stats.aiGpuQueueDepth = state.aiGpuQueueDepth.load();
    stats.aiGpuActiveTasks = state.aiGpuActiveTasks.load();
    stats.aiCpuQueueDepth = state.aiCpuQueueDepth.load();
    stats.aiCpuActiveTasks = state.aiCpuActiveTasks.load();
    stats.aiCompletedTasks = state.aiCompletedTasks.load();
    stats.aiDroppedTasks = state.aiDroppedTasks.load();
    stats.aiCancelledTasks = state.aiCancelledTasks.load();
    stats.aiDetectorDroppedTasks = state.aiDetectorDroppedTasks.load();
    stats.aiDetectorCancelledTasks = state.aiDetectorCancelledTasks.load();
    stats.aiLastWaitMs = state.aiLastWaitMs.load();
    stats.aiAverageWaitMs = state.aiAverageWaitMs.load();
    stats.runtimeDetectorSkipFrames = state.runtimeDetectorSkipFrames;
    stats.runtimeDetectorBaseSkipFrames = state.runtimeDetectorBaseSkipFrames;
    const DiagnosticsMetricsSnapshot diagnostics = MetricsRegistry::Instance().GetSnapshot();
    stats.diagnosticsUptimeMs = diagnostics.uptimeMs;
    stats.diagnosticsLogLines = diagnostics.logLinesTotal;
    stats.diagnosticsWarningLines = diagnostics.logWarningLines;
    stats.diagnosticsErrorLines = diagnostics.logErrorLines + diagnostics.logFatalLines;
    stats.diagnosticsQtWarnings = diagnostics.qtWarnings;
    stats.diagnosticsQtCriticals = diagnostics.qtCriticals + diagnostics.qtFatals;
    stats.diagnosticsFileWriteFailures = diagnostics.fileWriteFailures;
    stats.diagnosticsCrashDumpsWritten = diagnostics.crashDumpsWritten;
    stats.diagnosticsCrashHandlerInstalled = diagnostics.crashHandlerInstalled;
    return stats;
}

quint64 StreamStatsReporter::GetStatusEventGeneration() const
{
    return state.statusEventGeneration.load();
}

std::string StreamStatsReporter::GetStatusEventText()
{
    std::lock_guard<std::mutex> lock(resources.mux);
    return state.statusEventText;
}
