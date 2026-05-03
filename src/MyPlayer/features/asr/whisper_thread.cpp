

#include "whisper_thread.h"

WhisperThread::WhisperThread()
    : modelSession_(state_, resources_)
    , chunkBuffer_(state_, resources_)
    , audioIngress_(state_, resources_, chunkBuffer_)
    , workerLoop_(state_, resources_, modelSession_, chunkBuffer_)
{
}

WhisperThread::~WhisperThread()
{
    Stop();
}

bool WhisperThread::LoadModel(const std::string& modelPath)
{
    return modelSession_.LoadModel(modelPath);
}

void WhisperThread::SetInferenceScheduler(InferenceScheduler* scheduler)
{
    state_.scheduler = scheduler;
}

void WhisperThread::SetSessionContext(const AiSessionContext& context)
{
    state_.playbackKind.store(context.playbackKind);
    state_.liveMode.store(context.live);
    state_.lowLatencyMode.store(context.lowLatency);
    state_.priorityTier.store(static_cast<int>(context.priorityTier));
    state_.focusRoute.store(context.focusRoute);
    state_.alarmRoute.store(context.alarmRoute);
    state_.fullscreenRoute.store(context.fullscreenRoute);
}

void WhisperThread::SetVadConfig(bool enabled, const std::wstring& modelPath)
{
    state_.vadEnabled.store(enabled);
    state_.vadModelLoaded.store(false);
    state_.vadUsingGpu.store(false);
    state_.vadHeuristicActive.store(enabled);
    resources_.configuredVadModelPath = modelPath;
    resources_.vadSegmenter.SetEnabled(enabled);
}

void WhisperThread::SetMediaEpoch(const StreamEpoch& epoch)
{
    state_.generation.store(epoch.generation);
    state_.serial.store(epoch.serial);
    chunkBuffer_.Clear();
    chunkBuffer_.Notify();
}

void WhisperThread::PushAudio(const unsigned char* data, int bytes, int sampleRate, long long startMs)
{
    audioIngress_.PushAudio(data, bytes, sampleRate, startMs);
}

void WhisperThread::Clear()
{
    chunkBuffer_.Clear();
    chunkBuffer_.Notify();
}

void WhisperThread::Stop()
{
    state_.isExit.store(true);
    state_.generation.fetch_add(1);
    state_.serial.fetch_add(1);
    chunkBuffer_.Notify();
    wait();
}

QString WhisperThread::GetBackendSummary() const
{
    return modelSession_.GetBackendSummary();
}

QString WhisperThread::GetVadBackendSummary() const
{
    if (!state_.vadEnabled.load())
        return "off";
    return state_.vadModelLoaded.load()
        ? (state_.vadUsingGpu.load() ? "GPU(ONNX)" : "CPU(ONNX)")
        : "heuristic";
}

QString WhisperThread::GetPipelineSummary() const
{
    return modelSession_.GetPipelineSummary();
}

void WhisperThread::run()
{
    workerLoop_.Run(
        [this](const QString& text, long long startMs, long long endMs, quint64 generation, quint64 serial) {
            emit SubtitleReady(text, startMs, endMs, generation, serial);
        });
}
