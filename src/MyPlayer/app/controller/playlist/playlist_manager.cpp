

#include "playlist_manager.h"
#include "../../view/qt_ui_theme.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QRandomGenerator>
#include <QSet>
#include <QWidget>
#include <algorithm>
#include <utility>

namespace
{
const char* kPlayModeLabels[] = { "Order", "Loop All", "Repeat 1", "Shuffle" };
const char* kPlayModeTooltips[] = {
    "Play the list once in order.",
    "Loop the whole playlist.",
    "Repeat the current file.",
    "Play a random file next."
};
constexpr int kMinSidebarWidth = 260;
}

PlaylistManager::PlaylistManager(QWidget* hostWidget,
    QListWidget* playlistWidget,
    QWidget* sidebarBgWidget,
    QPushButton* toggleBtn,
    QPushButton* playModeBtn,
    QPushButton* addFolderBtn,
    QPushButton* clearListBtn)
    : host(hostWidget)
    , playlist(playlistWidget)
    , sidebarBackground(sidebarBgWidget)
    , toggleButton(toggleBtn)
    , playModeButton(playModeBtn)
    , addFolderButton(addFolderBtn)
    , clearListButton(clearListBtn)
{
    if (playlist)
    {
        QObject::connect(playlist, &QListWidget::itemDoubleClicked,
            [this](QListWidgetItem* item)
            {
                if (!item || !playRequestedHandler)
                    return;
                playRequestedHandler(item->data(Qt::UserRole).toString());
            });
    }

    if (toggleButton)
    {
        QObject::connect(toggleButton, &QPushButton::clicked,
            [this]()
            {
                ToggleSidebar();
            });
    }

    if (playModeButton)
    {
        QObject::connect(playModeButton, &QPushButton::clicked,
            [this]()
            {
                CyclePlayMode();
            });
        playModeButton->setToolTip(kPlayModeTooltips[static_cast<int>(playMode)]);
    }

    if (addFolderButton)
    {
        QObject::connect(addFolderButton, &QPushButton::clicked,
            [this]()
            {
                OpenFolder();
            });
        addFolderButton->setToolTip("Add all videos from a folder.");
    }

    if (clearListButton)
    {
        QObject::connect(clearListButton, &QPushButton::clicked,
            [this]()
            {
                if (playlist)
                    playlist->clear();
            });
        clearListButton->setToolTip("Clear the current playlist.");
    }

    UpdatePlayModeButton();
    ApplySidebarVisibility();
}

void PlaylistManager::SetPlayRequestedHandler(std::function<void(const QString&)> handler)
{
    playRequestedHandler = std::move(handler);
}

void PlaylistManager::SetLayoutChangedHandler(std::function<void()> handler)
{
    layoutChangedHandler = std::move(handler);
}

void PlaylistManager::AddFiles(const QStringList& files)
{
    if (!playlist)
        return;

    QSet<QString> existing;
    for (int i = 0; i < playlist->count(); ++i)
        existing.insert(playlist->item(i)->data(Qt::UserRole).toString());

    for (const QString& filePath : files)
    {
        if (filePath.isEmpty() || existing.contains(filePath))
            continue;

        QListWidgetItem* item = new QListWidgetItem(QFileInfo(filePath).fileName());
        item->setData(Qt::UserRole, filePath);
        playlist->addItem(item);
        existing.insert(filePath);
    }
}

bool PlaylistManager::PlayNext(QString* path) const
{
    if (!playlist || playlist->count() == 0)
        return false;

    const int count = playlist->count();
    const int curRow = playlist->currentRow();
    int nextRow = -1;

    switch (playMode)
    {
    case PlayMode::RepeatOne:
        nextRow = curRow >= 0 ? curRow : 0;
        break;
    case PlayMode::Sequential:
        nextRow = curRow + 1;
        if (nextRow >= count)
            return false;
        break;
    case PlayMode::LoopAll:
        nextRow = (curRow + 1) % count;
        break;
    case PlayMode::Shuffle:
        if (count == 1)
        {
            nextRow = 0;
            break;
        }
        do
        {
            nextRow = QRandomGenerator::global()->bounded(count);
        } while (nextRow == curRow && count > 1);
        break;
    }

    if (nextRow < 0 || nextRow >= count)
        return false;

    if (path)
        *path = playlist->item(nextRow)->data(Qt::UserRole).toString();
    return true;
}

void PlaylistManager::SetCurrentPath(const QString& path)
{
    if (!playlist)
        return;

    for (int i = 0; i < playlist->count(); ++i)
    {
        if (playlist->item(i)->data(Qt::UserRole).toString() == path)
        {
            playlist->setCurrentRow(i);
            return;
        }
    }
}

void PlaylistManager::RemovePath(const QString& path)
{
    if (!playlist || path.isEmpty())
        return;

    for (int i = playlist->count() - 1; i >= 0; --i)
    {
        QListWidgetItem* item = playlist->item(i);
        if (!item)
            continue;
        if (item->data(Qt::UserRole).toString() != path)
            continue;
        delete playlist->takeItem(i);
    }
}

void PlaylistManager::RemoveSelectedItems()
{
    if (!playlist)
        return;

    const QList<QListWidgetItem*> items = playlist->selectedItems();
    for (QListWidgetItem* item : items)
        delete item;
}

void PlaylistManager::ToggleSidebar()
{
    sidebarVisible = !sidebarVisible;
    ApplySidebarVisibility();
    NotifyLayoutChanged();
}

bool PlaylistManager::IsSidebarVisible() const
{
    return sidebarVisible;
}

int PlaylistManager::SidebarWidth() const
{
    return sidebarWidth;
}

void PlaylistManager::SetSidebarWidth(int width, int hostWidth)
{
    const int clamped = ClampSidebarWidth(width, hostWidth);
    if (clamped == sidebarWidth)
        return;

    sidebarWidth = clamped;
    NotifyLayoutChanged();
}

PlaylistState PlaylistManager::SaveState() const
{
    PlaylistState state;
    state.playMode = static_cast<int>(playMode);
    state.sidebarWidth = sidebarWidth;

    if (!playlist)
        return state;

    for (int i = 0; i < playlist->count(); ++i)
        state.items.append(playlist->item(i)->data(Qt::UserRole).toString());
    state.currentRow = playlist->currentRow();
    return state;
}

void PlaylistManager::RestoreState(const PlaylistState& state)
{
    if (!playlist)
        return;

    playlist->clear();
    QSet<QString> restoredPaths;
    int restoredCurrentRow = -1;

    for (int i = 0; i < state.items.size(); ++i)
    {
        const QString path = state.items.at(i);
        if (path.isEmpty())
            continue;
        const QString resolvedPath = ResolveRestoredPath(path);
        if (resolvedPath.isEmpty() || restoredPaths.contains(resolvedPath))
            continue;

        QListWidgetItem* item = new QListWidgetItem(QFileInfo(resolvedPath).fileName());
        item->setData(Qt::UserRole, resolvedPath);
        playlist->addItem(item);
        restoredPaths.insert(resolvedPath);
        if (i == state.currentRow)
            restoredCurrentRow = playlist->count() - 1;
    }

    const int curRow = restoredCurrentRow;
    if (curRow >= 0 && curRow < playlist->count())
        playlist->setCurrentRow(curRow);

    playMode = static_cast<PlayMode>(state.playMode);
    sidebarWidth = ClampSidebarWidth(state.sidebarWidth, host ? host->width() : 0);
    UpdatePlayModeButton();
    ApplySidebarVisibility();
}

void PlaylistManager::CyclePlayMode()
{
    playMode = static_cast<PlayMode>((static_cast<int>(playMode) + 1) % 4);
    UpdatePlayModeButton();
}

void PlaylistManager::OpenFolder()
{
    if (!host)
        return;

    const QString dir = QtUiTheme::GetExistingDirectory(host, "Select Folder", QString());
    if (dir.isEmpty())
        return;

    AddFiles(CollectVideoFiles(dir));
}

void PlaylistManager::ApplySidebarVisibility() const
{
    if (playlist)
        playlist->setVisible(sidebarVisible);
    if (sidebarBackground)
        sidebarBackground->setVisible(sidebarVisible);
    if (playModeButton)
        playModeButton->setVisible(sidebarVisible);
    if (addFolderButton)
        addFolderButton->setVisible(sidebarVisible);
    if (clearListButton)
        clearListButton->setVisible(sidebarVisible);
}

void PlaylistManager::UpdatePlayModeButton() const
{
    if (playModeButton)
    {
        playModeButton->setText(kPlayModeLabels[static_cast<int>(playMode)]);
        playModeButton->setToolTip(kPlayModeTooltips[static_cast<int>(playMode)]);
    }
}

void PlaylistManager::NotifyLayoutChanged() const
{
    if (layoutChangedHandler)
        layoutChangedHandler();
}

int PlaylistManager::ClampSidebarWidth(int width, int hostWidth) const
{
    int clamped = std::max(width, kMinSidebarWidth);
    if (hostWidth > 0)
        clamped = std::min(clamped, (hostWidth * 2) / 3);
    return clamped;
}

QStringList PlaylistManager::CollectVideoFiles(const QString& dirPath)
{
    const QStringList filters = {
        "*.mp4", "*.mkv", "*.avi", "*.mov", "*.flv",
        "*.wmv", "*.ts", "*.webm", "*.m4v", "*.mpg", "*.mpeg"
    };

    QDir dir(dirPath);
    QStringList files;
    const QFileInfoList entries = dir.entryInfoList(filters, QDir::Files, QDir::Name);
    for (const QFileInfo& fileInfo : entries)
        files.append(fileInfo.absoluteFilePath());
    return files;
}

QString PlaylistManager::ResolveRestoredPath(const QString& savedPath)
{
    const QString cleaned = QDir::cleanPath(savedPath.trimmed());
    if (cleaned.isEmpty())
        return {};

    const QFileInfo directInfo(cleaned);
    if (directInfo.exists() && directInfo.isFile())
        return directInfo.absoluteFilePath();

    const QString fileName = directInfo.fileName();
    if (fileName.isEmpty())
        return {};

    const QString appDir = QCoreApplication::applicationDirPath();
    const QString currentDir = QDir::currentPath();
    const QStringList candidateRoots = {
        appDir,
        currentDir,
        QDir(appDir).filePath(".."),
        QDir(currentDir).filePath(".."),
        QDir(appDir).filePath("../tool/test_video"),
        QDir(currentDir).filePath("../tool/test_video"),
        QDir(appDir).filePath("../../tool/test_video"),
        QDir(currentDir).filePath("../../tool/test_video"),
        QDir(appDir).filePath("../../../tool/test_video"),
        QDir(currentDir).filePath("../../../tool/test_video"),
        QDir(appDir).filePath("../../../../tool/test_video")
    };

    for (const QString& root : candidateRoots)
    {
        const QFileInfo candidateInfo(QDir(QDir::cleanPath(root)).filePath(fileName));
        if (candidateInfo.exists() && candidateInfo.isFile())
            return candidateInfo.absoluteFilePath();
    }

    return {};
}
