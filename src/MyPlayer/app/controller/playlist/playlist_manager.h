#pragma once

#include "../../service/config_service.h"

#include <QString>
#include <QStringList>
#include <functional>

class QWidget;
class QListWidget;
class QPushButton;

enum class PlayMode { Sequential, LoopAll, RepeatOne, Shuffle };

class PlaylistManager
{
public:
    PlaylistManager(QWidget* host,
        QListWidget* playlist,
        QWidget* sidebarBackground,
        QPushButton* toggleButton,
        QPushButton* playModeButton,
        QPushButton* addFolderButton,
        QPushButton* clearListButton);

    void SetPlayRequestedHandler(std::function<void(const QString&)> handler);
    void SetLayoutChangedHandler(std::function<void()> handler);

    void AddFiles(const QStringList& files);
    bool PlayNext(QString* path) const;
    void SetCurrentPath(const QString& path);
    void RemovePath(const QString& path);
    void RemoveSelectedItems();

    void ToggleSidebar();
    bool IsSidebarVisible() const;
    int SidebarWidth() const;
    void SetSidebarWidth(int width, int hostWidth = 0);

    PlaylistState SaveState() const;
    void RestoreState(const PlaylistState& state);

private:
    void CyclePlayMode();
    void OpenFolder();
    void ApplySidebarVisibility() const;
    void UpdatePlayModeButton() const;
    void NotifyLayoutChanged() const;
    int ClampSidebarWidth(int width, int hostWidth) const;
    static QStringList CollectVideoFiles(const QString& dirPath);
    static QString ResolveRestoredPath(const QString& savedPath);

    QWidget* host = nullptr;
    QListWidget* playlist = nullptr;
    QWidget* sidebarBackground = nullptr;
    QPushButton* toggleButton = nullptr;
    QPushButton* playModeButton = nullptr;
    QPushButton* addFolderButton = nullptr;
    QPushButton* clearListButton = nullptr;
    bool sidebarVisible = false;
    int sidebarWidth = 250;
    PlayMode playMode = PlayMode::Sequential;
    std::function<void(const QString&)> playRequestedHandler;
    std::function<void()> layoutChangedHandler;
};
