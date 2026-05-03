#pragma once

#include <QPoint>
#include <QString>
#include <QStringList>

namespace Ui { class MyPlayerClass; }
class QPushButton;
class QWidget;
class VideoWidget;

class IPlaybackView
{
public:
    virtual ~IPlaybackView() = default;

    virtual QWidget* HostWidget() const = 0;
    virtual VideoWidget* VideoWidgetHandle() const = 0;
    virtual QString UrlText() const = 0;
    virtual void SetUrlText(const QString& text) = 0;
    virtual int VolumeValue() const = 0;
    virtual int SeekValue() const = 0;
    virtual int SeekMaximum() const = 0;
    virtual void SetSeekValue(int value) = 0;
    virtual void SetPlayButtonText(const QString& text) = 0;
    virtual void SetSpeedText(const QString& text) = 0;
    virtual void SetSpeedValue(int value) = 0;
    virtual void SetSpeedControlsEnabled(bool enabled) = 0;
    virtual void SetWindowTitleText(const QString& text) = 0;
    virtual void SetPreferLiveRendering(bool isLive) = 0;
    virtual void SetSeekVisible(bool visible) = 0;
    virtual void SetLiveVisible(bool visible) = 0;
    virtual void SetTimeText(const QString& text) = 0;
    virtual void SetAudioTrackButtonVisible(bool visible) = 0;
    virtual void SetAudioTrackButtonText(const QString& text) = 0;
    virtual void SetAudioTrackButtonToolTip(const QString& text) = 0;
    virtual QPoint MapAudioTrackMenuToGlobal(const QPoint& pos) const = 0;
    virtual QStringList SelectVideoFiles() const = 0;
    virtual void ShowInfoMessage(const QString& title, const QString& text) const = 0;
    virtual void ShowWarningMessage(const QString& title, const QString& text) const = 0;
};

class PlaybackViewQt final : public IPlaybackView
{
public:
    PlaybackViewQt(QWidget* host, Ui::MyPlayerClass* ui, QPushButton* audioTrackButton);

    QWidget* HostWidget() const override;
    VideoWidget* VideoWidgetHandle() const override;
    QString UrlText() const override;
    void SetUrlText(const QString& text) override;
    int VolumeValue() const override;
    int SeekValue() const override;
    int SeekMaximum() const override;
    void SetSeekValue(int value) override;
    void SetPlayButtonText(const QString& text) override;
    void SetSpeedText(const QString& text) override;
    void SetSpeedValue(int value) override;
    void SetSpeedControlsEnabled(bool enabled) override;
    void SetWindowTitleText(const QString& text) override;
    void SetPreferLiveRendering(bool isLive) override;
    void SetSeekVisible(bool visible) override;
    void SetLiveVisible(bool visible) override;
    void SetTimeText(const QString& text) override;
    void SetAudioTrackButtonVisible(bool visible) override;
    void SetAudioTrackButtonText(const QString& text) override;
    void SetAudioTrackButtonToolTip(const QString& text) override;
    QPoint MapAudioTrackMenuToGlobal(const QPoint& pos) const override;
    QStringList SelectVideoFiles() const override;
    void ShowInfoMessage(const QString& title, const QString& text) const override;
    void ShowWarningMessage(const QString& title, const QString& text) const override;

private:
    QWidget* host_ = nullptr;
    Ui::MyPlayerClass* ui_ = nullptr;
    QPushButton* audioTrackButton_ = nullptr;
};
