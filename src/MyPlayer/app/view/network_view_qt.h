#pragma once

#include "../../../core/session/stream_config.h"

#include <QList>
#include <functional>

class QObject;
class QPushButton;
class QWidget;
class QString;

struct NetworkViewData
{
    bool forceTcpForRtsp = true;
    LiveClockPolicy liveClockPolicy = LiveClockPolicy::AudioMaster;
    bool enableLowLatency = false;
    bool noBuffer = false;
    bool lowDelayFlag = false;
    bool reconnectEnabled = true;

    int connectTimeoutMs = 5000;
    int maxDelayUs = 500000;
    int bufferSizeKb = 0;
    int probeSizeKb = 128;
    int analyzeDurationMs = 1000;
    int videoQueuePackets = 24;
    int audioQueuePackets = 40;
    int lateDropMs = 160;
    int reconnectAttempts = 5;
};

class INetworkView
{
public:
    virtual ~INetworkView() = default;

    virtual void SetTogglePanelHandler(std::function<void()> handler) = 0;
    virtual void SetToggleStatsHandler(std::function<void()> handler) = 0;
    virtual void SetApplyHandler(std::function<void()> handler) = 0;

    virtual QPushButton* ToggleButton() const = 0;
    virtual QPushButton* StatsButton() const = 0;
    virtual QWidget* Panel() const = 0;
    virtual QWidget* StatsPanel() const = 0;

    virtual bool IsPanelVisible() const = 0;
    virtual bool IsStatsPanelVisible() const = 0;
    virtual void SetPanelVisible(bool visible) = 0;
    virtual void SetStatsPanelVisible(bool visible) = 0;
    virtual void SetToggleButtonChecked(bool checked) = 0;
    virtual void SetStatsButtonChecked(bool checked) = 0;
    virtual void RaisePanel() = 0;
    virtual void RaiseStatsPanel() = 0;

    virtual void LayoutPanels(int videoWidth, int hostHeight) = 0;

    virtual QList<QWidget*> AutoHideWidgets() const = 0;
    virtual void InstallEventFilters(QObject* filter) const = 0;

    virtual NetworkViewData ReadData() const = 0;

    virtual void WriteData(const NetworkViewData& data) = 0;

    virtual void SetEffectivePolicyText(const QString& text) = 0;
};

class QLabel;
class QCheckBox;
class QComboBox;
class QSpinBox;
class QString;

class NetworkViewQt final : public INetworkView
{
public:

    explicit NetworkViewQt(QWidget* host);

    void SetTogglePanelHandler(std::function<void()> handler) override;
    void SetToggleStatsHandler(std::function<void()> handler) override;
    void SetApplyHandler(std::function<void()> handler) override;

    QPushButton* ToggleButton() const override;
    QPushButton* StatsButton() const override;
    QWidget* Panel() const override;
    QWidget* StatsPanel() const override;

    bool IsPanelVisible() const override;
    bool IsStatsPanelVisible() const override;
    void SetPanelVisible(bool visible) override;
    void SetStatsPanelVisible(bool visible) override;
    void SetToggleButtonChecked(bool checked) override;
    void SetStatsButtonChecked(bool checked) override;
    void RaisePanel() override;
    void RaiseStatsPanel() override;
    void LayoutPanels(int videoWidth, int hostHeight) override;

    QList<QWidget*> AutoHideWidgets() const override;
    void InstallEventFilters(QObject* filter) const override;

    NetworkViewData ReadData() const override;
    void WriteData(const NetworkViewData& data) override;
    void SetEffectivePolicyText(const QString& text) override;

    QLabel* StatsLabel() const;

private:

    QWidget* host_ = nullptr;
    QPushButton* toggleButton_ = nullptr;
    QPushButton* statsButton_ = nullptr;
    QWidget* networkPanel_ = nullptr;
    QWidget* networkStatsPanel_ = nullptr;
    QLabel* networkStatsLabel_ = nullptr;
    QLabel* effectivePolicyLabel_ = nullptr;

    QCheckBox* lowLatencyCheck_ = nullptr;
    QCheckBox* noBufferCheck_ = nullptr;
    QCheckBox* lowDelayCheck_ = nullptr;
    QCheckBox* reconnectCheck_ = nullptr;

    QComboBox* rtspTransportCombo_ = nullptr;
    QComboBox* liveClockCombo_ = nullptr;

    QSpinBox* connectTimeoutSpin_ = nullptr;
    QSpinBox* maxDelaySpin_ = nullptr;
    QSpinBox* bufferSizeSpin_ = nullptr;
    QSpinBox* probeSizeSpin_ = nullptr;
    QSpinBox* analyzeDurationSpin_ = nullptr;
    QSpinBox* videoQueueSpin_ = nullptr;
    QSpinBox* audioQueueSpin_ = nullptr;
    QSpinBox* lateDropSpin_ = nullptr;
    QSpinBox* reconnectAttemptsSpin_ = nullptr;

    std::function<void()> togglePanelHandler_;
    std::function<void()> toggleStatsHandler_;
    std::function<void()> applyHandler_;
};
