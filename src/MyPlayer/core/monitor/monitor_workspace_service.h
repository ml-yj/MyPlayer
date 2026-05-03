#pragma once

#include "monitor_types.h"
#include "../archive/archive_models.h"
#include "../recording/recording_models.h"
#include "../../features/detector/detector_types.h"

#include <map>
#include <memory>
#include <mutex>
#include <functional>

class MonitorSession;
class VideoCallback;
class QObject;
class Demux;

class MonitorWorkspaceService
{
public:
    MonitorWorkspaceService();
    ~MonitorWorkspaceService();

    QString UpsertSource(const MonitorSourceDescriptor& source);
    bool RemoveSession(const QString& sessionId);
    void CloseAll();

    bool OpenSession(const QString& sessionId, const std::shared_ptr<VideoCallback>& callback);
    bool OpenAll(const std::shared_ptr<VideoCallback>& callback);
    bool CloseSession(const QString& sessionId);

    bool SelectSession(const QString& sessionId);
    bool SetAudioSession(const QString& sessionId);
    bool SetSessionMuted(const QString& sessionId, bool muted);
    bool SetSessionDetectorEnabled(const QString& sessionId, bool enabled, std::string* error = nullptr);
    bool SetSessionAsrEnabled(const QString& sessionId, bool enabled, std::string* error = nullptr);
    bool SetSessionRecordingEnabled(const QString& sessionId, bool enabled, const RecordingConfiguration& configuration,
        std::string* error = nullptr);
    bool ApplySessionAiPolicy(
        const QString& sessionId,
        const MonitorAiSessionPolicy& policy,
        std::string* error = nullptr);
    bool RecordSessionEvent(const QString& sessionId, ArchiveEventRecord* record, std::string* error = nullptr);
    bool BindSessionDetectorResultHandler(
        const QString& sessionId, QObject* context, std::function<void(DetectionResult)> handler);
    bool BindSessionAsrSubtitleHandler(
        const QString& sessionId,
        QObject* context,
        std::function<void(const QString&, long long, long long, quint64, quint64)> handler);
    bool ReopenSession(const QString& sessionId, const std::shared_ptr<VideoCallback>& callback);
    bool ReopenSessionPrepared(
        const QString& sessionId,
        const QString& expectedSourceUrl,
        Demux* preparedDemux,
        const StreamOpenOptions& options,
        const std::shared_ptr<VideoCallback>& callback,
        int measuredOpenLatencyMs);
    bool GetSessionSource(const QString& sessionId, MonitorSourceDescriptor* source) const;

    MonitorSession* FindSession(const QString& sessionId);
    const MonitorSession* FindSession(const QString& sessionId) const;

    MonitorLayoutSnapshot BuildLayout(MonitorWallLayoutPreset preset) const;
    MonitorWorkspaceSnapshot GetSnapshot(MonitorWallLayoutPreset preset) const;

private:
    using SessionMap = std::map<QString, std::unique_ptr<MonitorSession>>;

    static QString ResolveSessionId(const MonitorSourceDescriptor& source);
    static std::pair<int, int> DimensionsForPreset(MonitorWallLayoutPreset preset, int sessionCount);
    void SyncAudioSelectionLocked();

    mutable std::mutex mux_;
    SessionMap sessions_;
    QString selectedSessionId_;
    QString audioSessionId_;
};
