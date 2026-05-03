

#include "monitor_workspace_service.h"

#include "../media/demux.h"
#include "monitor_session.h"

#include <QUuid>

namespace
{

QString FallbackDisplayName(const MonitorSourceDescriptor& source)
{
    if (!source.displayName.trimmed().isEmpty())
        return source.displayName;
    return source.cameraId;
}
}

MonitorWorkspaceService::MonitorWorkspaceService() = default;

MonitorWorkspaceService::~MonitorWorkspaceService()
{
    CloseAll();
}

QString MonitorWorkspaceService::UpsertSource(const MonitorSourceDescriptor& source)
{
    if (!source.IsValid())
        return {};

    std::lock_guard<std::mutex> lock(mux_);
    const QString sessionId = ResolveSessionId(source);
    auto it = sessions_.find(sessionId);
    MonitorSourceDescriptor normalized = source;
    normalized.displayName = FallbackDisplayName(normalized);
    if (it == sessions_.end())
    {
        auto session = std::make_unique<MonitorSession>(sessionId, normalized);
        sessions_.emplace(sessionId, std::move(session));
    }
    else
    {
        it->second->UpdateSource(normalized);
    }

    if (selectedSessionId_.isEmpty())
        selectedSessionId_ = sessionId;
    if (audioSessionId_.isEmpty())
        audioSessionId_ = sessionId;
    SyncAudioSelectionLocked();
    return sessionId;
}

bool MonitorWorkspaceService::RemoveSession(const QString& sessionId)
{
    std::lock_guard<std::mutex> lock(mux_);
    const auto it = sessions_.find(sessionId);
    if (it == sessions_.end())
        return false;

    it->second->Close();
    sessions_.erase(it);

    if (selectedSessionId_ == sessionId)
        selectedSessionId_.clear();
    if (audioSessionId_ == sessionId)
        audioSessionId_.clear();

    if (selectedSessionId_.isEmpty() && !sessions_.empty())
        selectedSessionId_ = sessions_.begin()->first;
    if (audioSessionId_.isEmpty() && !sessions_.empty())
        audioSessionId_ = sessions_.begin()->first;
    SyncAudioSelectionLocked();
    return true;
}

void MonitorWorkspaceService::CloseAll()
{
    std::lock_guard<std::mutex> lock(mux_);
    for (auto& [_, session] : sessions_)
        session->Close();
}

bool MonitorWorkspaceService::OpenSession(const QString& sessionId, const std::shared_ptr<VideoCallback>& callback)
{
    std::lock_guard<std::mutex> lock(mux_);
    const auto it = sessions_.find(sessionId);
    return it != sessions_.end() && it->second->Open(callback);
}

bool MonitorWorkspaceService::OpenAll(const std::shared_ptr<VideoCallback>& callback)
{
    std::lock_guard<std::mutex> lock(mux_);
    bool allOk = true;
    for (auto& [_, session] : sessions_)
        allOk = session->Open(callback) && allOk;
    return allOk;
}

bool MonitorWorkspaceService::CloseSession(const QString& sessionId)
{
    std::lock_guard<std::mutex> lock(mux_);
    const auto it = sessions_.find(sessionId);
    if (it == sessions_.end())
        return false;

    it->second->Close();
    return true;
}

bool MonitorWorkspaceService::SelectSession(const QString& sessionId)
{
    std::lock_guard<std::mutex> lock(mux_);
    if (sessions_.find(sessionId) == sessions_.end())
        return false;

    selectedSessionId_ = sessionId;
    if (audioSessionId_.isEmpty())
        audioSessionId_ = sessionId;
    SyncAudioSelectionLocked();
    return true;
}

bool MonitorWorkspaceService::SetAudioSession(const QString& sessionId)
{
    std::lock_guard<std::mutex> lock(mux_);
    if (sessions_.find(sessionId) == sessions_.end())
        return false;

    audioSessionId_ = sessionId;
    SyncAudioSelectionLocked();
    return true;
}

bool MonitorWorkspaceService::SetSessionMuted(const QString& sessionId, bool muted)
{
    std::lock_guard<std::mutex> lock(mux_);
    const auto it = sessions_.find(sessionId);
    if (it == sessions_.end())
        return false;

    it->second->SetMuted(muted);
    SyncAudioSelectionLocked();
    return true;
}

bool MonitorWorkspaceService::SetSessionDetectorEnabled(const QString& sessionId, bool enabled, std::string* error)
{
    std::lock_guard<std::mutex> lock(mux_);
    const auto it = sessions_.find(sessionId);
    if (it == sessions_.end())
        return false;

    return it->second->SetDetectorEnabled(enabled, error);
}

bool MonitorWorkspaceService::SetSessionAsrEnabled(const QString& sessionId, bool enabled, std::string* error)
{
    std::lock_guard<std::mutex> lock(mux_);
    const auto it = sessions_.find(sessionId);
    if (it == sessions_.end())
        return false;

    return it->second->SetAsrEnabled(enabled, error);
}

bool MonitorWorkspaceService::SetSessionRecordingEnabled(
    const QString& sessionId, bool enabled, const RecordingConfiguration& configuration, std::string* error)
{
    std::lock_guard<std::mutex> lock(mux_);
    const auto it = sessions_.find(sessionId);
    if (it == sessions_.end())
        return false;

    return it->second->SetRecordingEnabled(enabled, configuration, error);
}

bool MonitorWorkspaceService::ApplySessionAiPolicy(
    const QString& sessionId,
    const MonitorAiSessionPolicy& policy,
    std::string* error)
{
    std::lock_guard<std::mutex> lock(mux_);
    const auto it = sessions_.find(sessionId);
    if (it == sessions_.end())
        return false;

    return it->second->ApplyAiSessionPolicy(policy, error);
}

bool MonitorWorkspaceService::RecordSessionEvent(
    const QString& sessionId, ArchiveEventRecord* record, std::string* error)
{
    std::lock_guard<std::mutex> lock(mux_);
    const auto it = sessions_.find(sessionId);
    if (it == sessions_.end())
        return false;

    return it->second->RecordArchiveEvent(record, error);
}

bool MonitorWorkspaceService::BindSessionDetectorResultHandler(
    const QString& sessionId, QObject* context, std::function<void(DetectionResult)> handler)
{
    std::lock_guard<std::mutex> lock(mux_);
    const auto it = sessions_.find(sessionId);
    if (it == sessions_.end())
        return false;

    it->second->BindDetectorResultHandler(context, std::move(handler));
    return true;
}

bool MonitorWorkspaceService::BindSessionAsrSubtitleHandler(
    const QString& sessionId,
    QObject* context,
    std::function<void(const QString&, long long, long long, quint64, quint64)> handler)
{
    std::lock_guard<std::mutex> lock(mux_);
    const auto it = sessions_.find(sessionId);
    if (it == sessions_.end())
        return false;

    it->second->BindAsrSubtitleHandler(context, std::move(handler));
    return true;
}

bool MonitorWorkspaceService::ReopenSession(const QString& sessionId, const std::shared_ptr<VideoCallback>& callback)
{
    std::lock_guard<std::mutex> lock(mux_);
    const auto it = sessions_.find(sessionId);
    if (it == sessions_.end())
        return false;

    it->second->Close();
    return it->second->Open(callback);
}

bool MonitorWorkspaceService::ReopenSessionPrepared(
    const QString& sessionId,
    const QString& expectedSourceUrl,
    Demux* preparedDemux,
    const StreamOpenOptions& options,
    const std::shared_ptr<VideoCallback>& callback,
    int measuredOpenLatencyMs)
{
    std::unique_ptr<Demux> preparedHolder(preparedDemux);
    if (!preparedHolder)
        return false;

    std::lock_guard<std::mutex> lock(mux_);
    const auto it = sessions_.find(sessionId);
    if (it == sessions_.end())
        return false;
    if (!expectedSourceUrl.trimmed().isEmpty()
        && it->second->Source().sourceUrl.trimmed() != expectedSourceUrl.trimmed())
    {
        return false;
    }

    it->second->Close();
    return it->second->OpenPrepared(preparedHolder.release(), options, callback, measuredOpenLatencyMs);
}

bool MonitorWorkspaceService::GetSessionSource(const QString& sessionId, MonitorSourceDescriptor* source) const
{
    if (!source)
        return false;

    std::lock_guard<std::mutex> lock(mux_);
    const auto it = sessions_.find(sessionId);
    if (it == sessions_.end())
        return false;

    *source = it->second->Source();
    return true;
}

MonitorSession* MonitorWorkspaceService::FindSession(const QString& sessionId)
{
    std::lock_guard<std::mutex> lock(mux_);
    const auto it = sessions_.find(sessionId);
    return it != sessions_.end() ? it->second.get() : nullptr;
}

const MonitorSession* MonitorWorkspaceService::FindSession(const QString& sessionId) const
{
    std::lock_guard<std::mutex> lock(mux_);
    const auto it = sessions_.find(sessionId);
    return it != sessions_.end() ? it->second.get() : nullptr;
}

MonitorLayoutSnapshot MonitorWorkspaceService::BuildLayout(MonitorWallLayoutPreset preset) const
{
    return GetSnapshot(preset).layout;
}

MonitorWorkspaceSnapshot MonitorWorkspaceService::GetSnapshot(MonitorWallLayoutPreset preset) const
{
    std::lock_guard<std::mutex> lock(mux_);
    MonitorWorkspaceSnapshot snapshot;
    snapshot.selectedSessionId = selectedSessionId_;
    snapshot.audioSessionId = audioSessionId_;
    snapshot.layout.preset = preset;
    const auto [rows, columns] = DimensionsForPreset(preset, static_cast<int>(sessions_.size()));
    snapshot.layout.rows = rows;
    snapshot.layout.columns = columns;

    int index = 0;
    for (const auto& [sessionId, session] : sessions_)
    {
        auto sessionSnapshot = session->GetSnapshot();
        sessionSnapshot.selected = sessionId == selectedSessionId_;
        sessionSnapshot.audioOwner = sessionId == audioSessionId_;
        snapshot.sessions.push_back(sessionSnapshot);

        MonitorTilePlacement placement;
        placement.sessionId = sessionId;
        placement.row = rows > 0 ? index / columns : 0;
        placement.column = columns > 0 ? index % columns : 0;
        placement.selected = sessionSnapshot.selected;
        placement.audioOwner = sessionSnapshot.audioOwner;
        snapshot.layout.placements.push_back(placement);
        ++index;
    }
    return snapshot;
}

QString MonitorWorkspaceService::ResolveSessionId(const MonitorSourceDescriptor& source)
{
    if (!source.cameraId.trimmed().isEmpty())
        return source.cameraId.trimmed();
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

std::pair<int, int> MonitorWorkspaceService::DimensionsForPreset(
    MonitorWallLayoutPreset preset, int sessionCount)
{
    switch (preset)
    {
    case MonitorWallLayoutPreset::Single: return { 1, 1 };
    case MonitorWallLayoutPreset::Grid2x2: return { 2, 2 };
    case MonitorWallLayoutPreset::Grid3x3: return { 3, 3 };
    case MonitorWallLayoutPreset::Grid4x4: return { 4, 4 };
    case MonitorWallLayoutPreset::Custom:
        if (sessionCount <= 1)
            return { 1, 1 };
        if (sessionCount <= 4)
            return { 2, 2 };
        if (sessionCount <= 9)
            return { 3, 3 };
        return { 4, 4 };
    }
    return { 1, 1 };
}

void MonitorWorkspaceService::SyncAudioSelectionLocked()
{
    for (auto& [sessionId, session] : sessions_)
    {
        session->SetSelected(sessionId == selectedSessionId_);
        session->SetAudioOwner(sessionId == audioSessionId_);
    }
}
