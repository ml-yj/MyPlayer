

#include "my_player.h"

#include "../controller/subtitle/subtitle_controller.h"

void MyPlayer::ClearSubtitleState()
{
    if (subtitleController)
        subtitleController->ClearRenderedTrack();
}

void MyPlayer::SyncSubtitleRenderer()
{
    if (subtitleController)
        subtitleController->SyncRenderer();
}

void MyPlayer::UpdateSubtitleDisplay()
{
    if (subtitleController)
        subtitleController->UpdateDisplay();
}

void MyPlayer::UpdateSubtitleStyle()
{
    if (subtitleController)
        subtitleController->UpdateStyle();
}

void MyPlayer::ShowSubtitleOsd(const QString& text)
{
    if (subtitleController)
        subtitleController->ShowOsd(text);
}

void MyPlayer::UpdateSubtitleTrackButton()
{
    if (subtitleController)
        subtitleController->UpdateTrackButton();
}

void MyPlayer::AutoLoadEmbeddedSubtitle(const QString& mediaPath)
{
    if (subtitleController)
        subtitleController->AutoLoadEmbedded(mediaPath);
}

void MyPlayer::AutoLoadExternalSubtitle(const QString& mediaPath)
{
    if (subtitleController)
        subtitleController->AutoLoadExternal(mediaPath);
}

void MyPlayer::OpenSubtitleFile()
{
    if (subtitleController)
        subtitleController->OpenSubtitleFile();
}

void MyPlayer::CycleSubtitleTrack()
{
    if (subtitleController)
        subtitleController->CycleTrack();
}

void MyPlayer::ShowSubtitleMenu(const QPoint& pos)
{
    if (subtitleController)
        subtitleController->ShowMenu(pos);
}

void MyPlayer::AdjustSubtitleOffset(int deltaMs)
{
    if (subtitleController)
        subtitleController->AdjustOffset(deltaMs);
}

void MyPlayer::AdjustSubtitleFontSize(int deltaPt)
{
    if (subtitleController)
        subtitleController->AdjustFontSize(deltaPt);
}

void MyPlayer::AdjustSubtitleBottomMargin(int deltaPx)
{
    if (subtitleController)
        subtitleController->AdjustBottomMargin(deltaPx);
}

void MyPlayer::ExportAsrSubtitle()
{
    if (subtitleController)
        subtitleController->ExportAsrSubtitle();
}

void MyPlayer::OnSubtitleReady(
    const QString& text, long long startMs, long long endMs, quint64 generation, quint64 serial)
{
    if (subtitleController)
        subtitleController->OnSubtitleReady(text, startMs, endMs, generation, serial);
}
