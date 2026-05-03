#pragma once

#include <QString>
#include <QStringList>

#include "../../core/session/stream_config.h"

struct SubtitlePreferences
{
    int offsetMs = 0;
    int fontPointSize = 24;
    int bottomMarginPx = 120;
};

struct DetectorPreferences
{
    QString modelPath;
    QString labelsPath;
};

struct PlaylistState
{
    QStringList items;
    int currentRow = -1;
    int playMode = 0;
    int sidebarWidth = 250;
};

struct DiagnosticsPreferences
{
    bool consoleLoggingEnabled = true;
    bool fileLoggingEnabled = true;
    bool crashDumpEnabled = true;
    int minimumLogLevel = 1;
    QString logDirectory;
    QString crashDirectory;
};

struct ArchivePreferences
{
    QString archiveRootDir;
    QString cameraId = "camera-1";
    QString displayName;
    QString container = "mkv";
    int segmentDurationSeconds = 300;
};

struct MonitorSourceState
{
    QString cameraId;
    QString displayName;
    QString groupName;
    QString sourceUrl;
    bool preferLowLatency = true;
    bool enableDetector = false;
    bool enableAsr = false;
    bool enableRecording = false;
    bool muted = false;
};

struct MonitorWorkspacePreferences
{

    struct WorkspaceState
    {

        struct FavoriteLayoutState
        {
            QString layoutId;
            QString layoutName;
            int layoutPreset = 1;
            QString selectedSessionId;
            QString maximizedSessionId;
            QStringList sessionOrder;
            QString groupFilter;
        };

        QString workspaceId;
        QString workspaceName;
        QList<MonitorSourceState> sources;
        int layoutPreset = 1;
        QString selectedSessionId;
        QString audioSessionId;
        QString archiveRootDir;
        QString maximizedSessionId;
        QString assignedScreen;
        QStringList sessionOrder;
        QString groupFilter;
        QString activeFavoriteLayoutId;
        QList<FavoriteLayoutState> favoriteLayouts;
        bool windowVisible = false;
        bool fullscreen = false;
    };

    QList<WorkspaceState> workspaces;
    QString activeWorkspaceId;

    QList<MonitorSourceState> sources;
    int layoutPreset = 1;
    QString selectedSessionId;
    QString audioSessionId;
    QString archiveRootDir;
    QString maximizedSessionId;
    bool windowVisible = false;
    bool fullscreen = false;
};

class ConfigService
{
public:

    int LoadVolume() const;
    void SaveVolume(int volume) const;

    long long LoadPlaybackPosition(const QString& mediaPath) const;
    void SavePlaybackPosition(const QString& mediaPath, long long positionMs, long long totalMs, bool isLive) const;
    void ClearPlaybackPosition(const QString& mediaPath) const;

    SubtitlePreferences LoadSubtitlePreferences() const;
    void SaveSubtitlePreferences(const SubtitlePreferences& preferences) const;

    StreamOpenOptions LoadNetworkOpenOptions() const;
    void SaveNetworkOpenOptions(const StreamOpenOptions& options) const;

    DetectorPreferences LoadDetectorPreferences() const;
    void SaveDetectorPreferences(const DetectorPreferences& preferences) const;

    PlaylistState LoadPlaylistState() const;
    void SavePlaylistState(const PlaylistState& state) const;

    DiagnosticsPreferences LoadDiagnosticsPreferences() const;
    void SaveDiagnosticsPreferences(const DiagnosticsPreferences& preferences) const;

    ArchivePreferences LoadArchivePreferences() const;
    void SaveArchivePreferences(const ArchivePreferences& preferences) const;

    MonitorWorkspacePreferences LoadMonitorWorkspacePreferences() const;
    void SaveMonitorWorkspacePreferences(const MonitorWorkspacePreferences& preferences) const;

private:

    static QString PlaybackPositionKey(const QString& mediaPath);
};
