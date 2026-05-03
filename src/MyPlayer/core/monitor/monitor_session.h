#pragma once

#include "monitor_types.h"
#include "../archive/archive_models.h"
#include "../recording/recording_models.h"

#include "../../features/detector/detector_types.h"

#include <memory>
#include <functional>

class PlayerCore;
class VideoCallback;
class QObject;
class Demux;

class MonitorSession
{
public:
    MonitorSession(const QString& sessionId, const MonitorSourceDescriptor& source);
    ~MonitorSession();

    QString SessionId() const;
    QString CameraId() const;
    const MonitorSourceDescriptor& Source() const;

    void UpdateSource(const MonitorSourceDescriptor& source);
    bool Open(const std::shared_ptr<VideoCallback>& callback);
    bool OpenPrepared(
        Demux* preparedDemux,
        const StreamOpenOptions& options,
        const std::shared_ptr<VideoCallback>& callback,
        int measuredOpenLatencyMs);
    void Close();

    void SetSelected(bool selected);
    void SetAudioOwner(bool audioOwner);
    void SetMuted(bool muted);
    void SetVolume(double volume);
    bool SetDetectorEnabled(bool enabled, std::string* error = nullptr);
    bool SetAsrEnabled(bool enabled, std::string* error = nullptr);
    bool SetRecordingEnabled(bool enabled, const RecordingConfiguration& configuration, std::string* error = nullptr);
    bool ApplyAiSessionPolicy(const MonitorAiSessionPolicy& policy, std::string* error = nullptr);
    void BindDetectorResultHandler(QObject* context, std::function<void(DetectionResult)> handler);
    void BindAsrSubtitleHandler(
        QObject* context,
        std::function<void(const QString&, long long, long long, quint64, quint64)> handler);
    bool RecordArchiveEvent(ArchiveEventRecord* record, std::string* error = nullptr);

    bool IsSelected() const;
    bool IsAudioOwner() const;
    bool IsMuted() const;
    double Volume() const;
    bool IsDetectorEnabled() const;
    bool IsAsrEnabled() const;
    bool IsRecordingEnabled() const;

    MonitorSessionSnapshot GetSnapshot() const;

private:
    void ApplyAudioState();
    bool ApplyDetectorState(bool enabled, std::string* error = nullptr);
    bool ApplyAsrState(std::string* error = nullptr);
    bool ApplyRecordingState(bool enabled, const RecordingConfiguration& configuration, std::string* error = nullptr);
    void ApplyAiRuntimeHints();
    static QString ResolveDetectorModelPath();
    static QString ResolveDetectorLabelsPath(const QString& modelPath);
    static QString ResolveWhisperModelPath();

    QString sessionId_;
    MonitorSourceDescriptor source_;
    std::unique_ptr<PlayerCore> core_;
    bool started_ = false;
    bool selected_ = false;
    bool audioOwner_ = false;
    bool muted_ = false;
    double volume_ = 1.0;
    bool detectorEnabled_ = false;
    bool asrRequested_ = false;
    bool asrActive_ = false;
    bool recordingEnabled_ = false;
    MonitorAiSessionPolicy aiPolicy_;
    RecordingConfiguration recordingConfiguration_;
};
