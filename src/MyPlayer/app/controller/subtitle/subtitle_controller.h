#pragma once

#include <QPoint>
#include <QString>
#include <QVector>
#include <functional>
#include <memory>

#include "../../../core/media/demux_types.h"
#include "../../../features/subtitle/subtitle_manager.h"

class QMenu;
class ISubtitleView;
class IPlaybackSessionService;
class IPlaybackTrackService;
class ConfigService;

class SubtitleController
{
public:
    SubtitleController(std::unique_ptr<ISubtitleView> view,
        IPlaybackSessionService* sessionService,
        IPlaybackTrackService* trackService,
        ConfigService* configService,
        std::function<bool()> asrEnabledProvider,
        std::function<void()> asrToggleHandler,
        std::function<QString()> currentMediaPathProvider);

    void ClearRenderedTrack();
    void ResetForMedia();
    void SyncRenderer();
    void TickUi();
    void UpdateDisplay();
    void UpdateStyle();
    void Relayout();
    void ShowOsd(const QString& text);
    void HideOsd();

    void SaveSettings();
    void LoadSettings();

    void SetEpoch(const StreamEpoch& epoch);
    StreamEpoch Epoch() const;
    void ResetAsrCues(const StreamEpoch& epoch);
    void ActivateAsrTrack(const StreamEpoch& epoch);

    QVector<int> AvailableTrackIds() const;
    void UpdateTrackButton();
    void AutoLoadEmbedded(const QString& mediaPath);
    void AutoLoadExternal(const QString& mediaPath);
    void OpenSubtitleFile();
    void CycleTrack();
    void ShowMenu(const QPoint& pos);
    void AdjustOffset(int deltaMs);
    void AdjustFontSize(int deltaPt);
    void AdjustBottomMargin(int deltaPx);
    void ExportAsrSubtitle();
    void OnSubtitleReady(
        const QString& text, long long startMs, long long endMs, quint64 generation, quint64 serial);

private:
    QString SourceTag(SubtitleSourceType source) const;
    void ApplyMenuStyle(QMenu& menu) const;

    std::unique_ptr<ISubtitleView> view;
    IPlaybackSessionService* session = nullptr;
    IPlaybackTrackService* tracks = nullptr;
    ConfigService* config = nullptr;
    std::function<bool()> asrEnabledProvider;
    std::function<void()> asrToggleHandler;
    std::function<QString()> currentMediaPathProvider;

    SubtitleManager subtitleManager;
    int activeSubtitleTrackId = 0;
    int subtitleOffsetMs = 0;
    int subtitleBottomMarginPx = 120;
    int subtitleFontPointSize = 24;
    StreamEpoch subtitleEpoch;
    int subtitleOsdToken = 0;
};
