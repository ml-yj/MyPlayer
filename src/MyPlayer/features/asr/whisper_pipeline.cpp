#include "whisper_pipeline.h"

#include "../../core/ai/inference_scheduler.h"
#include "../../core/ai/shared_ai_runtimes.h"

#include <whisper.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>

namespace
{
int WhisperPriorityScore(const WhisperThreadState& state)
{
    const AiPriorityTier tier = static_cast<AiPriorityTier>(state.priorityTier.load());
    switch (tier)
    {
    case AiPriorityTier::Fullscreen: return 140;
    case AiPriorityTier::Alarm:      return 132;
    case AiPriorityTier::Focused:    return 118;
    case AiPriorityTier::Candidate:  return 70;
    case AiPriorityTier::Background: return 30;
    }
    return 70;
}

InferenceTaskProfile BuildWhisperTaskProfile(const WhisperThreadState& state)
{
    InferenceTaskProfile profile;
    profile.capability = AiCapability::Asr;
    profile.lane = state.usingGpu.load() ? InferenceLane::Gpu : InferenceLane::Cpu;
    profile.priority = WhisperPriorityScore(state);
    profile.dropIfLate = false;
    profile.maxQueueDelayMs = 0;
    profile.waitPollMs = state.lowLatencyMode.load() ? 8 : 12;
    return profile;
}
}

WhisperResources::WhisperResources() = default;

WhisperResources::~WhisperResources()
{
    if (wstate)
    {
        whisper_free_state(wstate);
        wstate = nullptr;
    }
    wctx = nullptr;
    sharedModel.reset();
}

WhisperModelSession::WhisperModelSession(WhisperThreadState& state, WhisperResources& resources)
    : state_(state)
    , resources_(resources)
{
}

bool WhisperModelSession::LoadModel(const std::string& modelPath)
{
    if (state_.modelLoaded.load())
        return true;

    state_.usingGpu.store(false);
    state_.vadHeuristicActive.store(state_.vadEnabled.load());
    state_.vadModelLoaded.store(false);
    state_.vadUsingGpu.store(false);
    resources_.vadSegmenter.SetEnabled(state_.vadEnabled.load());
    resources_.vadSegmenter.Reset();

    if (resources_.wstate)
    {
        whisper_free_state(resources_.wstate);
        resources_.wstate = nullptr;
    }
    resources_.wctx = nullptr;
    resources_.sharedModel.reset();

    resources_.sharedModel = SharedWhisperRuntime::Instance().AcquireModel(modelPath);
    if (!resources_.sharedModel || !resources_.sharedModel->context)
    {
        std::cout << "Failed to load Whisper model: " << modelPath << std::endl;
        return false;
    }
    resources_.wctx = resources_.sharedModel->context;
    resources_.wstate = whisper_init_state(resources_.wctx);
    if (!resources_.wstate)
    {
        resources_.wctx = nullptr;
        resources_.sharedModel.reset();
        std::cout << "Failed to allocate Whisper state: " << modelPath << std::endl;
        return false;
    }

    if (state_.vadEnabled.load())
    {
        try
        {
            if (!resources_.configuredVadModelPath.empty())
            {
                if (resources_.vadSegmenter.LoadModel(resources_.configuredVadModelPath))
                {
                    state_.vadModelLoaded.store(true);
                    state_.vadUsingGpu.store(resources_.vadSegmenter.IsUsingGpu());
                    state_.vadHeuristicActive.store(false);
                    std::wcout << L"Silero VAD enabled: " << resources_.configuredVadModelPath << std::endl;
                }
                else
                {
                    state_.vadHeuristicActive.store(true);
                    std::wcout << L"Silero VAD load failed, fallback to heuristic gate: "
                        << resources_.configuredVadModelPath << std::endl;
                }
            }
            else
            {
                state_.vadHeuristicActive.store(true);
                std::cout << "Silero VAD model not configured, fallback to heuristic gate" << std::endl;
            }
        }
        catch (const std::exception& ex)
        {
            state_.vadHeuristicActive.store(true);
            std::cout << "Silero VAD setup failed, fallback to heuristic gate: " << ex.what() << std::endl;
        }
    }
    else
    {
        state_.vadHeuristicActive.store(false);
        std::cout << "Silero VAD disabled by pipeline manager" << std::endl;
    }

    state_.usingGpu.store(resources_.sharedModel->usingGpu);
    state_.modelLoaded.store(true);
    std::cout << "Whisper model loaded successfully: " << modelPath
        << " (gpu=" << (resources_.sharedModel->usingGpu ? "on" : "off") << ")" << std::endl;
    std::cout << "Whisper pipeline: infer=" << (resources_.sharedModel->usingGpu ? "GPU" : "CPU")
        << " vad=" << (state_.vadEnabled.load()
            ? (state_.vadModelLoaded.load() ? (state_.vadUsingGpu.load() ? "GPU(ONNX)" : "CPU(ONNX)") : "heuristic")
            : "off") << std::endl;
    return true;
}

QString WhisperModelSession::GetBackendSummary() const
{
    return state_.usingGpu.load() ? "GPU" : "CPU";
}

QString WhisperModelSession::GetPipelineSummary() const
{
    return QString("infer=%1 | vad=%2")
        .arg(GetBackendSummary())
        .arg(!state_.vadEnabled.load()
            ? "off"
            : (state_.vadModelLoaded.load()
                ? (state_.vadUsingGpu.load() ? "GPU" : "CPU")
                : "heuristic"));
}

WhisperChunkBuffer::WhisperChunkBuffer(WhisperThreadState& state, WhisperResources& resources)
    : state_(state)
    , resources_(resources)
{
}

void WhisperChunkBuffer::Clear()
{
    std::lock_guard<std::mutex> lock(resources_.bufMux);
    resources_.pcmF32Mono16k.clear();
    resources_.pcmBufferedOffset = 0;
    state_.currentSampleRate = 0;
    state_.bufferStartMs = 0;
    state_.emittedUntilMs = std::numeric_limits<long long>::min();
    state_.forceFlush = false;
    resources_.vadSegmenter.Reset();
}

void WhisperChunkBuffer::Notify()
{
    resources_.bufCV.notify_one();
}

long long WhisperChunkBuffer::SamplesToMs(size_t samples) const
{
    return static_cast<long long>(samples) * 1000LL / kWhisperTargetSampleRate;
}

void WhisperChunkBuffer::AppendAudioLocked(const std::vector<float>& mono16k, long long startMs)
{
    if (mono16k.empty())
        return;

    if (BufferedSamplesLocked() == 0)
    {
        resources_.pcmF32Mono16k.clear();
        resources_.pcmBufferedOffset = 0;
        state_.bufferStartMs = startMs;
        state_.emittedUntilMs = std::numeric_limits<long long>::min();
    }
    else
    {
        const long long expectedStartMs = state_.bufferStartMs + SamplesToMs(BufferedSamplesLocked());
        if (std::llabs(startMs - expectedStartMs) > kWhisperDiscontinuityMs)
        {
            resources_.pcmF32Mono16k.clear();
            resources_.pcmBufferedOffset = 0;
            state_.bufferStartMs = startMs;
            state_.emittedUntilMs = std::numeric_limits<long long>::min();
        }
    }

    resources_.pcmF32Mono16k.insert(resources_.pcmF32Mono16k.end(), mono16k.begin(), mono16k.end());
    const size_t bufferedSamples = BufferedSamplesLocked();
    if (bufferedSamples > kWhisperMaxBufferedSamples)
    {
        const size_t overflow = bufferedSamples - kWhisperMaxBufferedSamples;
        resources_.pcmBufferedOffset += overflow;
        state_.bufferStartMs += SamplesToMs(overflow);
        state_.emittedUntilMs = std::max(state_.emittedUntilMs, state_.bufferStartMs);
        CompactAudioBufferLocked();
    }
    state_.currentSampleRate = kWhisperTargetSampleRate;
}

bool WhisperChunkBuffer::HasReadyChunkLocked() const
{
    const size_t bufferedSamples = BufferedSamplesLocked();
    return bufferedSamples >= kWhisperChunkSamples ||
        (state_.forceFlush && bufferedSamples >= kWhisperMinFlushSamples);
}

size_t WhisperChunkBuffer::BufferedSamplesLocked() const
{
    return resources_.pcmF32Mono16k.size() > resources_.pcmBufferedOffset
        ? (resources_.pcmF32Mono16k.size() - resources_.pcmBufferedOffset)
        : 0;
}

void WhisperChunkBuffer::CompactAudioBufferLocked()
{
    if (resources_.pcmBufferedOffset == 0)
        return;

    if (resources_.pcmBufferedOffset >= resources_.pcmF32Mono16k.size())
    {
        resources_.pcmF32Mono16k.clear();
        resources_.pcmBufferedOffset = 0;
        return;
    }

    if (resources_.pcmBufferedOffset < kWhisperChunkSamples &&
        resources_.pcmBufferedOffset * 2 < resources_.pcmF32Mono16k.size())
    {
        return;
    }

    resources_.pcmF32Mono16k.erase(
        resources_.pcmF32Mono16k.begin(),
        resources_.pcmF32Mono16k.begin() + static_cast<std::ptrdiff_t>(resources_.pcmBufferedOffset));
    resources_.pcmBufferedOffset = 0;
}

bool WhisperChunkBuffer::WaitForChunk(WhisperChunkWork& work)
{
    std::unique_lock<std::mutex> lock(resources_.bufMux);
    resources_.bufCV.wait(lock, [this] {
        return state_.isExit.load() || HasReadyChunkLocked();
    });

    if (state_.isExit.load())
        return false;

    const size_t bufferedSamples = BufferedSamplesLocked();
    if (bufferedSamples >= kWhisperChunkSamples)
    {
        work.generation = state_.generation.load();
        work.serial = state_.serial.load();
        work.chunkStartMs = state_.bufferStartMs;
        work.emitAfterMs = state_.emittedUntilMs;
        work.flushRun = false;
        const auto begin = resources_.pcmF32Mono16k.begin() + static_cast<std::ptrdiff_t>(resources_.pcmBufferedOffset);
        work.chunk.assign(begin, begin + kWhisperChunkSamples);
        return true;
    }

    if (state_.forceFlush && bufferedSamples >= kWhisperMinFlushSamples)
    {
        work.generation = state_.generation.load();
        work.serial = state_.serial.load();
        work.chunkStartMs = state_.bufferStartMs;
        work.emitAfterMs = state_.emittedUntilMs;
        work.flushRun = true;
        const auto begin = resources_.pcmF32Mono16k.begin() + static_cast<std::ptrdiff_t>(resources_.pcmBufferedOffset);
        work.chunk.assign(begin, resources_.pcmF32Mono16k.end());
        return true;
    }

    return false;
}

void WhisperChunkBuffer::CommitChunk(const WhisperChunkWork& work)
{
    std::lock_guard<std::mutex> lock(resources_.bufMux);
    if (state_.generation.load() != work.generation || state_.serial.load() != work.serial)
        return;

    state_.emittedUntilMs = std::max(state_.emittedUntilMs, work.chunkStartMs + SamplesToMs(work.chunk.size()));

    if (work.flushRun)
    {
        resources_.pcmF32Mono16k.clear();
        resources_.pcmBufferedOffset = 0;
        state_.forceFlush = false;
    }
    else if (BufferedSamplesLocked() >= kWhisperStepSamples)
    {
        resources_.pcmBufferedOffset += kWhisperStepSamples;
        state_.bufferStartMs += kWhisperStepMs;
        CompactAudioBufferLocked();
    }
    else
    {
        resources_.pcmF32Mono16k.clear();
        resources_.pcmBufferedOffset = 0;
    }
}

WhisperAudioIngress::WhisperAudioIngress(
    WhisperThreadState& state,
    WhisperResources& resources,
    WhisperChunkBuffer& chunkBuffer)
    : state_(state)
    , resources_(resources)
    , chunkBuffer_(chunkBuffer)
{
}

void WhisperAudioIngress::PushAudio(const unsigned char* data, int bytes, int sampleRate, long long startMs)
{
    if (!state_.modelLoaded.load() || !data || bytes <= 0 || sampleRate <= 0)
        return;
    if (startMs < 0)
        startMs = 0;
    ConvertAndResample(data, bytes, sampleRate, startMs);
}

void WhisperAudioIngress::ConvertAndResample(const unsigned char* data, int bytes, int srcSampleRate, long long startMs)
{
    const int numSamples = bytes / 4;
    if (numSamples <= 0)
        return;

    const int16_t* s16data = reinterpret_cast<const int16_t*>(data);
    if (srcSampleRate == kWhisperTargetSampleRate)
    {
        resources_.mono16kScratch.resize(numSamples);
        for (int i = 0; i < numSamples; ++i)
        {
            const float left = s16data[i * 2] / 32768.0f;
            const float right = s16data[i * 2 + 1] / 32768.0f;
            resources_.mono16kScratch[i] = (left + right) * 0.5f;
        }
    }
    else
    {
        resources_.monoSrcScratch.resize(numSamples);
        for (int i = 0; i < numSamples; ++i)
        {
            const float left = s16data[i * 2] / 32768.0f;
            const float right = s16data[i * 2 + 1] / 32768.0f;
            resources_.monoSrcScratch[i] = (left + right) * 0.5f;
        }
        LinearResample(resources_.monoSrcScratch, srcSampleRate, kWhisperTargetSampleRate, resources_.mono16kScratch);
    }

    if (resources_.mono16kScratch.empty())
        return;

    std::lock_guard<std::mutex> lock(resources_.bufMux);
    resources_.speechChunksScratch.clear();
    resources_.vadSegmenter.Process(resources_.mono16kScratch, startMs, resources_.speechChunksScratch);
    bool appended = false;
    for (const VadChunk& chunk : resources_.speechChunksScratch)
    {
        chunkBuffer_.AppendAudioLocked(chunk.samples, chunk.startMs);
        appended = true;
        if (chunk.flushAfter)
            state_.forceFlush = true;
    }
    if (appended || state_.forceFlush)
        resources_.bufCV.notify_one();
}

void WhisperAudioIngress::LinearResample(
    const std::vector<float>& src, int srcRate, int dstRate, std::vector<float>& dst) const
{
    if (src.empty() || srcRate <= 0 || dstRate <= 0)
    {
        dst.clear();
        return;
    }

    const double ratio = static_cast<double>(srcRate) / static_cast<double>(dstRate);
    const int dstLen = static_cast<int>(src.size() / ratio);
    dst.resize(dstLen);

    for (int i = 0; i < dstLen; ++i)
    {
        const double srcPos = i * ratio;
        const int idx0 = static_cast<int>(srcPos);
        const int idx1 = idx0 + 1;

        if (idx1 >= static_cast<int>(src.size()))
        {
            dst[i] = src[idx0];
        }
        else
        {
            const float frac = static_cast<float>(srcPos - idx0);
            dst[i] = src[idx0] * (1.0f - frac) + src[idx1] * frac;
        }
    }
}

WhisperWorkerLoop::WhisperWorkerLoop(
    WhisperThreadState& state,
    WhisperResources& resources,
    WhisperModelSession& modelSession,
    WhisperChunkBuffer& chunkBuffer)
    : state_(state)
    , resources_(resources)
    , modelSession_(modelSession)
    , chunkBuffer_(chunkBuffer)
{
}

void WhisperWorkerLoop::Run(
    const std::function<void(const QString&, long long, long long, quint64, quint64)>& subtitleReadyCallback)
{
    while (!state_.isExit.load())
    {
        WhisperChunkWork work;
        if (!chunkBuffer_.WaitForChunk(work))
            break;

        InferenceTaskTicket schedulerTicket;
        bool schedulerAcquired = false;
        if (state_.scheduler)
        {
            schedulerTicket = state_.scheduler->Enqueue(BuildWhisperTaskProfile(state_));
            const InferenceAcquireResult acquireResult = state_.scheduler->Acquire(
                schedulerTicket,
                [this, work]() {
                    return state_.isExit.load()
                        || state_.generation.load() != work.generation
                        || state_.serial.load() != work.serial;
                });
            if (acquireResult != InferenceAcquireResult::Acquired)
                continue;
            schedulerAcquired = true;
        }

        whisper_full_params wparams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
        wparams.print_progress = false;
        wparams.print_special = false;
        wparams.print_realtime = false;
        wparams.print_timestamps = false;
        wparams.translate = false;
        wparams.language = "auto";
        wparams.n_threads = 4;
        wparams.offset_ms = 0;
        wparams.duration_ms = static_cast<int>(chunkBuffer_.SamplesToMs(work.chunk.size()));

        if (!resources_.wctx || !resources_.wstate)
        {
            chunkBuffer_.CommitChunk(work);
            if (schedulerAcquired)
                state_.scheduler->Release(schedulerTicket, false);
            continue;
        }

        const int ret = whisper_full_with_state(
            resources_.wctx, resources_.wstate, wparams, work.chunk.data(), static_cast<int>(work.chunk.size()));
        if (ret != 0)
        {
            std::cout << "Whisper recognition failed" << std::endl;
        }
        else if (state_.generation.load() == work.generation && state_.serial.load() == work.serial)
        {
            const int nSegments = whisper_full_n_segments_from_state(resources_.wstate);
            for (int i = 0; i < nSegments; ++i)
            {
                const char* text = whisper_full_get_segment_text_from_state(resources_.wstate, i);
                if (!text || text[0] == '\0')
                    continue;

                long long absStartMs =
                    work.chunkStartMs + whisper_full_get_segment_t0_from_state(resources_.wstate, i) * 10LL;
                long long absEndMs =
                    work.chunkStartMs + whisper_full_get_segment_t1_from_state(resources_.wstate, i) * 10LL;
                if (absEndMs <= work.emitAfterMs)
                    continue;
                if (absStartMs < work.emitAfterMs)
                    absStartMs = work.emitAfterMs;
                if (absEndMs <= absStartMs)
                    continue;

                subtitleReadyCallback(
                    QString::fromUtf8(text), absStartMs, absEndMs, work.generation, work.serial);
            }
        }

        chunkBuffer_.CommitChunk(work);
        if (schedulerAcquired)
            state_.scheduler->Release(schedulerTicket, ret == 0);
    }
}
