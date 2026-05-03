#pragma once

#include "../vad/vad_segmenter.h"
#include "../../core/ai/ai_types.h"
#include "../../core/media/demux_types.h"
#include "../../core/session/stream_config.h"

#include <QString>
#include <QtGlobal>

#include <atomic>
#include <condition_variable>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

struct whisper_context;
struct whisper_state;
struct WhisperSharedModel;

inline constexpr int kWhisperTargetSampleRate = 16000;
inline constexpr int kWhisperChunkSamples = kWhisperTargetSampleRate * 2;
inline constexpr int kWhisperStepSamples = kWhisperTargetSampleRate / 2;
inline constexpr int kWhisperMinFlushSamples = kWhisperTargetSampleRate / 3;
inline constexpr int kWhisperMaxBufferedSamples = kWhisperTargetSampleRate * 12;
inline constexpr long long kWhisperChunkMs = 2000;
inline constexpr long long kWhisperStepMs = 500;
inline constexpr long long kWhisperDiscontinuityMs = 250;

class InferenceScheduler;

struct WhisperThreadState
{
    std::atomic<bool> isExit{ false };
    std::atomic<bool> modelLoaded{ false };
    std::atomic<quint64> generation{ 1 };
    std::atomic<quint64> serial{ 1 };
    std::atomic<bool> usingGpu{ false };
    std::atomic<bool> vadEnabled{ true };
    std::atomic<bool> vadModelLoaded{ false };
    std::atomic<bool> vadUsingGpu{ false };
    std::atomic<bool> vadHeuristicActive{ true };
    std::atomic<StreamPlaybackKind> playbackKind{ StreamPlaybackKind::File };
    std::atomic<bool> liveMode{ false };
    std::atomic<bool> lowLatencyMode{ false };
    std::atomic<int> priorityTier{ static_cast<int>(AiPriorityTier::Candidate) };
    std::atomic<bool> focusRoute{ false };
    std::atomic<bool> alarmRoute{ false };
    std::atomic<bool> fullscreenRoute{ false };
    InferenceScheduler* scheduler = nullptr;

    int currentSampleRate = 0;
    long long bufferStartMs = 0;
    long long emittedUntilMs = std::numeric_limits<long long>::min();
    bool forceFlush = false;
};

struct WhisperResources
{
    WhisperResources();
    ~WhisperResources();

    std::shared_ptr<const WhisperSharedModel> sharedModel;
    whisper_context* wctx = nullptr;
    whisper_state* wstate = nullptr;
    std::mutex bufMux;
    std::condition_variable bufCV;
    std::vector<float> pcmF32Mono16k;
    size_t pcmBufferedOffset = 0;
    VadSegmenter vadSegmenter;
    std::vector<float> monoSrcScratch;
    std::vector<float> mono16kScratch;
    std::vector<VadChunk> speechChunksScratch;
    std::wstring configuredVadModelPath;
};

class WhisperModelSession
{
public:
    WhisperModelSession(WhisperThreadState& state, WhisperResources& resources);

    bool LoadModel(const std::string& modelPath);
    QString GetBackendSummary() const;
    QString GetPipelineSummary() const;

private:
    WhisperThreadState& state_;
    WhisperResources& resources_;
};

struct WhisperChunkWork
{
    quint64 generation = 0;
    quint64 serial = 0;
    long long chunkStartMs = 0;
    long long emitAfterMs = std::numeric_limits<long long>::min();
    std::vector<float> chunk;
    bool flushRun = false;
};

class WhisperChunkBuffer
{
public:
    WhisperChunkBuffer(WhisperThreadState& state, WhisperResources& resources);

    void Clear();
    void Notify();
    long long SamplesToMs(size_t samples) const;
    void AppendAudioLocked(const std::vector<float>& mono16k, long long startMs);
    bool HasReadyChunkLocked() const;
    size_t BufferedSamplesLocked() const;
    void CompactAudioBufferLocked();
    bool WaitForChunk(WhisperChunkWork& work);
    void CommitChunk(const WhisperChunkWork& work);

private:
    WhisperThreadState& state_;
    WhisperResources& resources_;
};

class WhisperAudioIngress
{
public:
    WhisperAudioIngress(WhisperThreadState& state, WhisperResources& resources, WhisperChunkBuffer& chunkBuffer);

    void PushAudio(const unsigned char* data, int bytes, int sampleRate, long long startMs);

private:
    void ConvertAndResample(const unsigned char* data, int bytes, int srcSampleRate, long long startMs);
    void LinearResample(const std::vector<float>& src, int srcRate, int dstRate, std::vector<float>& dst) const;

    WhisperThreadState& state_;
    WhisperResources& resources_;
    WhisperChunkBuffer& chunkBuffer_;
};

class WhisperWorkerLoop
{
public:
    WhisperWorkerLoop(
        WhisperThreadState& state,
        WhisperResources& resources,
        WhisperModelSession& modelSession,
        WhisperChunkBuffer& chunkBuffer);

    void Run(const std::function<void(const QString&, long long, long long, quint64, quint64)>& subtitleReadyCallback);

private:
    WhisperThreadState& state_;
    WhisperResources& resources_;
    WhisperModelSession& modelSession_;
    WhisperChunkBuffer& chunkBuffer_;
};
