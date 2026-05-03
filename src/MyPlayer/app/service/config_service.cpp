
#include "config_service.h"

#include "../../core/archive/archive_path_policy.h"

#include <QByteArray>
#include <QSettings>

namespace
{

    QSettings MakeSettings()
    {
        return QSettings("MyPlayer", "MyPlayer");
    }
}

int ConfigService::LoadVolume() const
{
    QSettings settings = MakeSettings();

    return settings.value("volume", 50).toInt();
}

void ConfigService::SaveVolume(int volume) const
{
    QSettings settings = MakeSettings();
    settings.setValue("volume", volume);
}

long long ConfigService::LoadPlaybackPosition(const QString& mediaPath) const
{
    if (mediaPath.isEmpty()) return 0;

    QSettings settings = MakeSettings();

    return settings.value(PlaybackPositionKey(mediaPath), 0).toLongLong();
}

void ConfigService::SavePlaybackPosition(
    const QString& mediaPath, long long positionMs, long long totalMs, bool isLive) const
{
    if (mediaPath.isEmpty()) return;

    QSettings settings = MakeSettings();
    const QString key = PlaybackPositionKey(mediaPath);

    if (isLive)
    {
        settings.remove(key);
        return;
    }

    if (totalMs > 0 && positionMs > 5000 && positionMs < totalMs - 5000)
        settings.setValue(key, positionMs);
    else
        settings.remove(key);
}

void ConfigService::ClearPlaybackPosition(const QString& mediaPath) const
{
    if (mediaPath.isEmpty()) return;
    QSettings settings = MakeSettings();
    settings.remove(PlaybackPositionKey(mediaPath));
}

SubtitlePreferences ConfigService::LoadSubtitlePreferences() const
{
    QSettings settings = MakeSettings();
    SubtitlePreferences preferences;

    preferences.offsetMs = settings.value("subtitle/offset_ms", preferences.offsetMs).toInt();
    preferences.fontPointSize = settings.value("subtitle/font_pt", preferences.fontPointSize).toInt();
    preferences.bottomMarginPx = settings.value("subtitle/bottom_margin_px", preferences.bottomMarginPx).toInt();
    return preferences;
}

void ConfigService::SaveSubtitlePreferences(const SubtitlePreferences& preferences) const
{
    QSettings settings = MakeSettings();
    settings.setValue("subtitle/offset_ms", preferences.offsetMs);
    settings.setValue("subtitle/font_pt", preferences.fontPointSize);
    settings.setValue("subtitle/bottom_margin_px", preferences.bottomMarginPx);
}

StreamOpenOptions ConfigService::LoadNetworkOpenOptions() const
{

    StreamOpenOptions options = StreamOpenOptions::DefaultNetwork();
    QSettings settings = MakeSettings();

    options.enableLowLatency = settings.value("network/low_latency", options.enableLowLatency).toBool();
    options.forceTcpForRtsp = settings.value("network/rtsp_tcp", options.forceTcpForRtsp).toBool();
    options.noBuffer = settings.value("network/no_buffer", options.noBuffer).toBool();
    options.lowDelayFlag = settings.value("network/low_delay_flag", options.lowDelayFlag).toBool();
    options.connectTimeoutMs = settings.value("network/connect_timeout_ms", options.connectTimeoutMs).toInt();
    options.maxDelayUs = settings.value("network/max_delay_us", options.maxDelayUs).toInt();

    options.bufferSizeBytes = settings.value("network/buffer_kb", options.bufferSizeBytes / 1024).toInt() * 1024;
    options.probeSizeBytes = settings.value("network/probe_kb", options.probeSizeBytes / 1024).toInt() * 1024;

    options.analyzeDurationUs = settings.value("network/analyze_ms", options.analyzeDurationUs / 1000).toInt() * 1000;

    options.videoQueuePackets = settings.value("network/video_queue", options.videoQueuePackets).toInt();
    options.audioQueuePackets = settings.value("network/audio_queue", options.audioQueuePackets).toInt();

    options.liveClockPolicy = static_cast<LiveClockPolicy>(
        settings.value("network/live_clock_policy", static_cast<int>(options.liveClockPolicy)).toInt());

    options.lateVideoDropMs = settings.value("network/late_drop_ms", options.lateVideoDropMs).toInt();
    options.reconnect.enabled = settings.value("network/reconnect_enabled", options.reconnect.enabled).toBool();
    options.reconnect.maxAttempts = settings.value("network/reconnect_attempts", options.reconnect.maxAttempts).toInt();
    return options;
}

void ConfigService::SaveNetworkOpenOptions(const StreamOpenOptions& options) const
{
    QSettings settings = MakeSettings();

    settings.setValue("network/low_latency", options.enableLowLatency);
    settings.setValue("network/rtsp_tcp", options.forceTcpForRtsp);
    settings.setValue("network/no_buffer", options.noBuffer);
    settings.setValue("network/low_delay_flag", options.lowDelayFlag);
    settings.setValue("network/connect_timeout_ms", options.connectTimeoutMs);
    settings.setValue("network/max_delay_us", options.maxDelayUs);
    settings.setValue("network/buffer_kb", options.bufferSizeBytes / 1024);
    settings.setValue("network/probe_kb", options.probeSizeBytes / 1024);
    settings.setValue("network/analyze_ms", options.analyzeDurationUs / 1000);
    settings.setValue("network/video_queue", options.videoQueuePackets);
    settings.setValue("network/audio_queue", options.audioQueuePackets);
    settings.setValue("network/live_clock_policy", static_cast<int>(options.liveClockPolicy));
    settings.setValue("network/late_drop_ms", options.lateVideoDropMs);
    settings.setValue("network/reconnect_enabled", options.reconnect.enabled);
    settings.setValue("network/reconnect_attempts", options.reconnect.maxAttempts);
}

DetectorPreferences ConfigService::LoadDetectorPreferences() const
{
    QSettings settings = MakeSettings();
    DetectorPreferences preferences;

    preferences.modelPath = settings.value("detector/model_path").toString().trimmed();
    preferences.labelsPath = settings.value("detector/labels_path").toString().trimmed();
    return preferences;
}

void ConfigService::SaveDetectorPreferences(const DetectorPreferences& preferences) const
{
    QSettings settings = MakeSettings();
    settings.setValue("detector/model_path", preferences.modelPath);
    settings.setValue("detector/labels_path", preferences.labelsPath);
}

PlaylistState ConfigService::LoadPlaylistState() const
{
    QSettings settings = MakeSettings();
    PlaylistState state;

    const int count = settings.value("playlist/count", 0).toInt();
    for (int i = 0; i < count; ++i)
    {

        const QString path = settings.value(QString("playlist/item_%1").arg(i)).toString();
        if (!path.isEmpty())
            state.items.append(path);
    }

    state.currentRow = settings.value("playlist/current", state.currentRow).toInt();
    state.playMode = settings.value("playlist/mode", state.playMode).toInt();
    state.sidebarWidth = settings.value("playlist/width", state.sidebarWidth).toInt();
    return state;
}

void ConfigService::SavePlaylistState(const PlaylistState& state) const
{
    QSettings settings = MakeSettings();
    const int previousCount = settings.value("playlist/count", 0).toInt();
    settings.setValue("playlist/count", state.items.size());

    for (int i = 0; i < state.items.size(); ++i)
        settings.setValue(QString("playlist/item_%1").arg(i), state.items.at(i));

    for (int i = state.items.size(); i < previousCount; ++i)
        settings.remove(QString("playlist/item_%1").arg(i));

    settings.setValue("playlist/current", state.currentRow);
    settings.setValue("playlist/mode", state.playMode);
    settings.setValue("playlist/width", state.sidebarWidth);
}

DiagnosticsPreferences ConfigService::LoadDiagnosticsPreferences() const
{
    QSettings settings = MakeSettings();
    DiagnosticsPreferences preferences;

    preferences.consoleLoggingEnabled = settings.value("diagnostics/log_console", preferences.consoleLoggingEnabled).toBool();
    preferences.fileLoggingEnabled = settings.value("diagnostics/log_file", preferences.fileLoggingEnabled).toBool();
    preferences.crashDumpEnabled = settings.value("diagnostics/crash_dump", preferences.crashDumpEnabled).toBool();
    preferences.minimumLogLevel = settings.value("diagnostics/log_level", preferences.minimumLogLevel).toInt();
    preferences.logDirectory = settings.value("diagnostics/log_directory").toString().trimmed();
    preferences.crashDirectory = settings.value("diagnostics/crash_directory").toString().trimmed();
    return preferences;
}

void ConfigService::SaveDiagnosticsPreferences(const DiagnosticsPreferences& preferences) const
{
    QSettings settings = MakeSettings();
    settings.setValue("diagnostics/log_console", preferences.consoleLoggingEnabled);
    settings.setValue("diagnostics/log_file", preferences.fileLoggingEnabled);
    settings.setValue("diagnostics/crash_dump", preferences.crashDumpEnabled);
    settings.setValue("diagnostics/log_level", preferences.minimumLogLevel);
    settings.setValue("diagnostics/log_directory", preferences.logDirectory);
    settings.setValue("diagnostics/crash_directory", preferences.crashDirectory);
}

ArchivePreferences ConfigService::LoadArchivePreferences() const
{
    QSettings settings = MakeSettings();
    ArchivePreferences preferences;
    preferences.archiveRootDir = settings.value("archive/root_dir", preferences.archiveRootDir).toString().trimmed();
    preferences.cameraId = settings.value("archive/camera_id", preferences.cameraId).toString().trimmed();
    preferences.displayName = settings.value("archive/display_name", preferences.displayName).toString().trimmed();

    preferences.container = ArchivePathPolicy::NormalizeRecordingContainer(
        settings.value("archive/container", preferences.container).toString());

    preferences.segmentDurationSeconds = settings.value("archive/segment_duration_seconds", preferences.segmentDurationSeconds).toInt();
    if (preferences.segmentDurationSeconds <= 0)
        preferences.segmentDurationSeconds = 300;
    return preferences;
}

void ConfigService::SaveArchivePreferences(const ArchivePreferences& preferences) const
{
    QSettings settings = MakeSettings();
    settings.setValue("archive/root_dir", preferences.archiveRootDir);
    settings.setValue("archive/camera_id", preferences.cameraId);
    settings.setValue("archive/display_name", preferences.displayName);
    settings.setValue("archive/container", ArchivePathPolicy::NormalizeRecordingContainer(preferences.container));
    settings.setValue("archive/segment_duration_seconds", preferences.segmentDurationSeconds);
}

MonitorWorkspacePreferences ConfigService::LoadMonitorWorkspacePreferences() const
{
    QSettings settings = MakeSettings();
    MonitorWorkspacePreferences preferences;

    preferences.layoutPreset = settings.value("monitor/layout_preset", preferences.layoutPreset).toInt();
    preferences.selectedSessionId = settings.value("monitor/selected_session").toString().trimmed();

    preferences.audioSessionId = settings.value("monitor/audio_session").toString().trimmed();
    preferences.archiveRootDir = settings.value("monitor/archive_root").toString().trimmed();
    preferences.maximizedSessionId = settings.value("monitor/maximized_session").toString().trimmed();
    preferences.windowVisible = settings.value("monitor/window_visible", preferences.windowVisible).toBool();
    preferences.fullscreen = settings.value("monitor/fullscreen", preferences.fullscreen).toBool();
    preferences.activeWorkspaceId = settings.value("monitor/active_workspace").toString().trimmed();

    const int sourceCount = settings.beginReadArray("monitor/sources");
    for (int index = 0; index < sourceCount; ++index)
    {
        settings.setArrayIndex(index);
        MonitorSourceState state;

        state.cameraId = settings.value("camera_id").toString().trimmed();
        state.displayName = settings.value("display_name").toString().trimmed();
        state.groupName = settings.value("group_name").toString().trimmed();
        state.sourceUrl = settings.value("source_url").toString().trimmed();
        state.preferLowLatency = settings.value("prefer_low_latency", state.preferLowLatency).toBool();
        state.enableDetector = settings.value("enable_detector", state.enableDetector).toBool();
        state.enableAsr = settings.value("enable_asr", state.enableAsr).toBool();
        state.enableRecording = settings.value("enable_recording", state.enableRecording).toBool();
        state.muted = settings.value("muted", state.muted).toBool();

        if (!state.cameraId.isEmpty() && !state.sourceUrl.isEmpty())
            preferences.sources.append(state);
    }
    settings.endArray();

    const int workspaceCount = settings.beginReadArray("monitor/workspaces");
    for (int workspaceIndex = 0; workspaceIndex < workspaceCount; ++workspaceIndex)
    {
        settings.setArrayIndex(workspaceIndex);
        MonitorWorkspacePreferences::WorkspaceState workspace;
        workspace.workspaceId = settings.value("workspace_id").toString().trimmed();

        workspace.workspaceName = settings.value("workspace_name").toString().trimmed();
        workspace.layoutPreset = settings.value("layout_preset", workspace.layoutPreset).toInt();
        workspace.selectedSessionId = settings.value("selected_session").toString().trimmed();
        workspace.audioSessionId = settings.value("audio_session").toString().trimmed();
        workspace.archiveRootDir = settings.value("archive_root").toString().trimmed();
        workspace.maximizedSessionId = settings.value("maximized_session").toString().trimmed();
        workspace.assignedScreen = settings.value("assigned_screen").toString().trimmed();
        workspace.windowVisible = settings.value("window_visible", workspace.windowVisible).toBool();
        workspace.fullscreen = settings.value("fullscreen", workspace.fullscreen).toBool();
        workspace.sessionOrder = settings.value("session_order").toStringList();
        workspace.groupFilter = settings.value("group_filter").toString().trimmed();
        workspace.activeFavoriteLayoutId = settings.value("active_favorite_layout").toString().trimmed();

        const int nestedSourceCount = settings.beginReadArray("sources");
        for (int sourceIndex = 0; sourceIndex < nestedSourceCount; ++sourceIndex)
        {
            settings.setArrayIndex(sourceIndex);
            MonitorSourceState state;
            state.cameraId = settings.value("camera_id").toString().trimmed();

            state.displayName = settings.value("display_name").toString().trimmed();
            state.groupName = settings.value("group_name").toString().trimmed();
            state.sourceUrl = settings.value("source_url").toString().trimmed();
            state.preferLowLatency = settings.value("prefer_low_latency", state.preferLowLatency).toBool();
            state.enableDetector = settings.value("enable_detector", state.enableDetector).toBool();
            state.enableAsr = settings.value("enable_asr", state.enableAsr).toBool();
            state.enableRecording = settings.value("enable_recording", state.enableRecording).toBool();
            state.muted = settings.value("muted", state.muted).toBool();
            if (!state.cameraId.isEmpty() && !state.sourceUrl.isEmpty())
                workspace.sources.append(state);
        }
        settings.endArray();

        const int favoriteLayoutCount = settings.beginReadArray("favorite_layouts");
        for (int layoutIndex = 0; layoutIndex < favoriteLayoutCount; ++layoutIndex)
        {
            settings.setArrayIndex(layoutIndex);
            MonitorWorkspacePreferences::WorkspaceState::FavoriteLayoutState layout;
            layout.layoutId = settings.value("layout_id").toString().trimmed();
            layout.layoutName = settings.value("layout_name").toString().trimmed();
            layout.layoutPreset = settings.value("layout_preset", layout.layoutPreset).toInt();
            layout.selectedSessionId = settings.value("selected_session").toString().trimmed();
            layout.maximizedSessionId = settings.value("maximized_session").toString().trimmed();
            layout.sessionOrder = settings.value("session_order").toStringList();
            layout.groupFilter = settings.value("group_filter").toString().trimmed();
            if (!layout.layoutId.isEmpty())
                workspace.favoriteLayouts.append(layout);
        }
        settings.endArray();

        if (!workspace.workspaceId.isEmpty())
            preferences.workspaces.append(workspace);
    }
    settings.endArray();

    if (preferences.workspaces.isEmpty() && !preferences.sources.isEmpty())
    {
        MonitorWorkspacePreferences::WorkspaceState workspace;
        workspace.workspaceId = "workspace-1";
        workspace.workspaceName = "Workspace 1";
        workspace.sources = preferences.sources;
        workspace.layoutPreset = preferences.layoutPreset;

        workspace.selectedSessionId = preferences.selectedSessionId;
        workspace.audioSessionId = preferences.audioSessionId;
        workspace.archiveRootDir = preferences.archiveRootDir;
        workspace.maximizedSessionId = preferences.maximizedSessionId;
        workspace.windowVisible = preferences.windowVisible;
        workspace.fullscreen = preferences.fullscreen;
        for (const MonitorSourceState& state : workspace.sources)
            workspace.sessionOrder.append(state.cameraId);
        preferences.workspaces.append(workspace);
    }

    else if (preferences.workspaces.isEmpty())
    {
        MonitorWorkspacePreferences::WorkspaceState workspace;
        workspace.workspaceId = "workspace-1";
        workspace.workspaceName = "Workspace 1";
        workspace.layoutPreset = preferences.layoutPreset;
        workspace.archiveRootDir = preferences.archiveRootDir;
        workspace.windowVisible = preferences.windowVisible;
        workspace.fullscreen = preferences.fullscreen;
        preferences.workspaces.append(workspace);
    }

    if (preferences.activeWorkspaceId.isEmpty() && !preferences.workspaces.isEmpty())
        preferences.activeWorkspaceId = preferences.workspaces.first().workspaceId;

    return preferences;
}

void ConfigService::SaveMonitorWorkspacePreferences(const MonitorWorkspacePreferences& preferences) const
{
    QSettings settings = MakeSettings();

    settings.setValue("monitor/layout_preset", preferences.layoutPreset);
    settings.setValue("monitor/selected_session", preferences.selectedSessionId);
    settings.setValue("monitor/audio_session", preferences.audioSessionId);
    settings.setValue("monitor/archive_root", preferences.archiveRootDir);
    settings.setValue("monitor/maximized_session", preferences.maximizedSessionId);
    settings.setValue("monitor/window_visible", preferences.windowVisible);
    settings.setValue("monitor/fullscreen", preferences.fullscreen);
    settings.setValue("monitor/active_workspace", preferences.activeWorkspaceId);

    settings.beginWriteArray("monitor/sources");
    for (int index = 0; index < preferences.sources.size(); ++index)
    {
        settings.setArrayIndex(index);
        const MonitorSourceState& state = preferences.sources.at(index);
        settings.setValue("camera_id", state.cameraId);
        settings.setValue("display_name", state.displayName);
        settings.setValue("group_name", state.groupName);
        settings.setValue("source_url", state.sourceUrl);
        settings.setValue("prefer_low_latency", state.preferLowLatency);
        settings.setValue("enable_detector", state.enableDetector);
        settings.setValue("enable_asr", state.enableAsr);
        settings.setValue("enable_recording", state.enableRecording);
        settings.setValue("muted", state.muted);
    }
    settings.endArray();

    settings.beginWriteArray("monitor/workspaces");
    for (int workspaceIndex = 0; workspaceIndex < preferences.workspaces.size(); ++workspaceIndex)
    {
        settings.setArrayIndex(workspaceIndex);
        const auto& workspace = preferences.workspaces.at(workspaceIndex);
        settings.setValue("workspace_id", workspace.workspaceId);
        settings.setValue("workspace_name", workspace.workspaceName);
        settings.setValue("layout_preset", workspace.layoutPreset);
        settings.setValue("selected_session", workspace.selectedSessionId);
        settings.setValue("audio_session", workspace.audioSessionId);
        settings.setValue("archive_root", workspace.archiveRootDir);
        settings.setValue("maximized_session", workspace.maximizedSessionId);
        settings.setValue("assigned_screen", workspace.assignedScreen);
        settings.setValue("session_order", workspace.sessionOrder);
        settings.setValue("group_filter", workspace.groupFilter);
        settings.setValue("active_favorite_layout", workspace.activeFavoriteLayoutId);
        settings.setValue("window_visible", workspace.windowVisible);
        settings.setValue("fullscreen", workspace.fullscreen);

        settings.beginWriteArray("sources");
        for (int sourceIndex = 0; sourceIndex < workspace.sources.size(); ++sourceIndex)
        {
            settings.setArrayIndex(sourceIndex);
            const MonitorSourceState& state = workspace.sources.at(sourceIndex);
            settings.setValue("camera_id", state.cameraId);
            settings.setValue("display_name", state.displayName);
            settings.setValue("group_name", state.groupName);
            settings.setValue("source_url", state.sourceUrl);
            settings.setValue("prefer_low_latency", state.preferLowLatency);
            settings.setValue("enable_detector", state.enableDetector);
            settings.setValue("enable_asr", state.enableAsr);
            settings.setValue("enable_recording", state.enableRecording);
            settings.setValue("muted", state.muted);
        }
        settings.endArray();

        settings.beginWriteArray("favorite_layouts");
        for (int layoutIndex = 0; layoutIndex < workspace.favoriteLayouts.size(); ++layoutIndex)
        {
            settings.setArrayIndex(layoutIndex);
            const auto& layout = workspace.favoriteLayouts.at(layoutIndex);
            settings.setValue("layout_id", layout.layoutId);
            settings.setValue("layout_name", layout.layoutName);
            settings.setValue("layout_preset", layout.layoutPreset);
            settings.setValue("selected_session", layout.selectedSessionId);
            settings.setValue("maximized_session", layout.maximizedSessionId);
            settings.setValue("session_order", layout.sessionOrder);
            settings.setValue("group_filter", layout.groupFilter);
        }
        settings.endArray();
    }
    settings.endArray();
}

QString ConfigService::PlaybackPositionKey(const QString& mediaPath)
{

    return "pos/" + QString(mediaPath.toUtf8().toBase64(QByteArray::Base64UrlEncoding));
}
