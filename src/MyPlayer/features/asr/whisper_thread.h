#pragma once

#include <QThread>
#include <QString>
#include <QtGlobal>

#include "../../core/ai/ai_types.h"
#include "../../core/media/demux_types.h"
#include "whisper_pipeline.h"

class WhisperThread : public QThread
{
    Q_OBJECT

public:
    WhisperThread();
    ~WhisperThread();

    bool LoadModel(const std::string& modelPath);
    void SetInferenceScheduler(InferenceScheduler* scheduler);
    void SetSessionContext(const AiSessionContext& context);
    void SetVadConfig(bool enabled, const std::wstring& modelPath);
    void SetMediaEpoch(const StreamEpoch& epoch);
    void PushAudio(const unsigned char* data, int bytes, int sampleRate, long long startMs);
    void Clear();
    void Stop();

    bool IsModelLoaded() const { return state_.modelLoaded.load(); }
    StreamEpoch GetEpoch() const { return StreamEpoch{ state_.generation.load(), state_.serial.load() }; }
    quint64 GetSerial() const { return state_.serial.load(); }
    bool IsUsingGpu() const { return state_.usingGpu.load(); }
    bool IsVadEnabled() const { return state_.vadEnabled.load(); }
    bool IsVadModelLoaded() const { return state_.vadModelLoaded.load(); }
    bool IsVadHeuristicActive() const { return state_.vadHeuristicActive.load(); }
    QString GetBackendSummary() const;
    QString GetVadBackendSummary() const;
    QString GetPipelineSummary() const;

signals:
    void SubtitleReady(
        const QString& text, long long startMs, long long endMs, quint64 generation, quint64 serial);

protected:
    void run() override;

private:
    WhisperThreadState state_;
    WhisperResources resources_;
    WhisperModelSession modelSession_;
    WhisperChunkBuffer chunkBuffer_;
    WhisperAudioIngress audioIngress_;
    WhisperWorkerLoop workerLoop_;
};
