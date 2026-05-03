#pragma once

#include <QPoint>
#include <QString>
#include <QStringList>

class QWidget;
class VideoWidget;

class ISubtitleView
{
public:

    virtual ~ISubtitleView() = default;

    virtual QWidget* HostWidget() const = 0;
    virtual VideoWidget* VideoWidgetHandle() const = 0;
    virtual int VideoAreaWidth() const = 0;
    virtual int HostHeight() const = 0;

    virtual void UpdateTrackButton(const QString& text, bool checked, const QString& tooltip) = 0;

    virtual QPoint MapTrackMenuToGlobal(const QPoint& pos) const = 0;

    virtual void SetOsdText(const QString& text) = 0;
    virtual void ShowOsd() = 0;
    virtual void HideOsd() = 0;
    virtual bool IsOsdVisible() const = 0;
    virtual int OsdWidth() const = 0;
    virtual void MoveOsd(int x, int y) = 0;
    virtual void RaiseOsd() = 0;

    virtual QStringList SelectSubtitleFiles(const QString& startDir) const = 0;

    virtual QString SelectSubtitleSavePath(const QString& startPath) const = 0;

    virtual void ShowInfoMessage(const QString& title, const QString& text) const = 0;

    virtual void ShowWarningMessage(const QString& title, const QString& text) const = 0;
};

#include <functional>

class QLabel;
class QPushButton;
class QWidget;
class VideoWidget;

class SubtitleViewQt final : public ISubtitleView
{
public:

    SubtitleViewQt(QWidget* host, VideoWidget* video, QPushButton* trackButton, QLabel* osdLabel,
        std::function<int()> videoAreaWidthProvider);

    QWidget* HostWidget() const override;

    VideoWidget* VideoWidgetHandle() const override;

    int VideoAreaWidth() const override;

    int HostHeight() const override;

    void UpdateTrackButton(const QString& text, bool checked, const QString& tooltip) override;

    QPoint MapTrackMenuToGlobal(const QPoint& pos) const override;

    void SetOsdText(const QString& text) override;
    void ShowOsd() override;
    void HideOsd() override;
    bool IsOsdVisible() const override;
    int OsdWidth() const override;
    void MoveOsd(int x, int y) override;
    void RaiseOsd() override;

    QStringList SelectSubtitleFiles(const QString& startDir) const override;
    QString SelectSubtitleSavePath(const QString& startPath) const override;
    void ShowInfoMessage(const QString& title, const QString& text) const override;
    void ShowWarningMessage(const QString& title, const QString& text) const override;

private:

    QWidget* host_ = nullptr;
    VideoWidget* video_ = nullptr;
    QPushButton* trackButton_ = nullptr;
    QLabel* osdLabel_ = nullptr;

    std::function<int()> videoAreaWidthProvider_;

    QString osdText_;
    bool osdVisible_ = false;
};
