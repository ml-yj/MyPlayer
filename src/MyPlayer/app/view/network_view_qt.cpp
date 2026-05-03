
#include "network_view_qt.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QObject>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWidget>
#include <algorithm>

NetworkViewQt::NetworkViewQt(QWidget* host)
    : host_(host)
{

    toggleButton_ = new QPushButton("NET", host_);
    toggleButton_->setFixedSize(56, 36);
    toggleButton_->setCheckable(true);

    QObject::connect(toggleButton_, &QPushButton::clicked, [this]() {
        if (togglePanelHandler_) togglePanelHandler_();
        });

    statsButton_ = new QPushButton("STAT", host_);
    statsButton_->setFixedSize(72, 36);
    statsButton_->setCheckable(true);
    QObject::connect(statsButton_, &QPushButton::clicked, [this]() {
        if (toggleStatsHandler_) toggleStatsHandler_();
        });

    networkPanel_ = new QWidget(host_);
    networkPanel_->setObjectName("networkPanel");

    networkPanel_->setAttribute(Qt::WA_StyledBackground, true);

    networkPanel_->setStyleSheet(

        "QWidget#networkPanel { background: rgba(18, 22, 28, 228); border: 1px solid rgba(255,255,255,36); border-radius: 10px; }"

        "QWidget#networkPanel QLabel { color: #F5F5F7; }"

        "QWidget#networkPanel QCheckBox { color: #F5F5F7; spacing: 8px; }"
        "QWidget#networkPanel QCheckBox::indicator {"
        " width: 16px; height: 16px; border-radius: 3px;"
        " border: 1px solid rgba(255,255,255,96); background: rgba(255,255,255,24);"
        " }"
        "QWidget#networkPanel QCheckBox::indicator:unchecked:hover { background: rgba(255,255,255,40); }"
        "QWidget#networkPanel QCheckBox::indicator:checked {"
        " border: 1px solid rgba(176,221,255,220); background: rgb(10,132,255);"
        " }"
        "QWidget#networkPanel QCheckBox::indicator:checked:hover { background: rgb(54,160,255); }"

        "QWidget#networkPanel QComboBox, QWidget#networkPanel QSpinBox { background: rgba(255,255,255,22); color: #F5F5F7; border: 1px solid rgba(255,255,255,30); border-radius: 6px; padding: 4px 8px; }");

    networkPanel_->setMinimumWidth(340);
    networkPanel_->setVisible(false);

    auto* networkLayout = new QVBoxLayout(networkPanel_);
    networkLayout->setContentsMargins(12, 12, 12, 12);
    networkLayout->setSpacing(8);

    QLabel* networkTitle = new QLabel("Network Settings", networkPanel_);
    networkTitle->setStyleSheet("font-size: 15px; font-weight: 600; color: #F5F5F7;");
    networkLayout->addWidget(networkTitle);

    auto* networkForm = new QFormLayout();
    networkForm->setContentsMargins(0, 0, 0, 0);
    networkForm->setSpacing(6);
    networkForm->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    networkLayout->addLayout(networkForm);

    rtspTransportCombo_ = new QComboBox(networkPanel_);
    rtspTransportCombo_->addItem("TCP");
    rtspTransportCombo_->addItem("UDP");
    networkForm->addRow("RTSP", rtspTransportCombo_);

    liveClockCombo_ = new QComboBox(networkPanel_);
    liveClockCombo_->addItem("Audio master");
    liveClockCombo_->addItem("Audio + drop late video");
    networkForm->addRow("Clock", liveClockCombo_);

    lowLatencyCheck_ = new QCheckBox("Enable low latency", networkPanel_);
    networkForm->addRow("Mode", lowLatencyCheck_);

    noBufferCheck_ = new QCheckBox("fflags=nobuffer", networkPanel_);
    networkForm->addRow("Buffer", noBufferCheck_);

    lowDelayCheck_ = new QCheckBox("flags=low_delay", networkPanel_);
    networkForm->addRow("Decode", lowDelayCheck_);

    reconnectCheck_ = new QCheckBox("Reconnect on drop", networkPanel_);
    networkForm->addRow("Retry", reconnectCheck_);

    connectTimeoutSpin_ = new QSpinBox(networkPanel_);
    connectTimeoutSpin_->setRange(100, 60000);
    connectTimeoutSpin_->setSuffix(" ms");
    networkForm->addRow("Connect timeout", connectTimeoutSpin_);

    maxDelaySpin_ = new QSpinBox(networkPanel_);
    maxDelaySpin_->setRange(0, 5000000);
    maxDelaySpin_->setSingleStep(50000);
    maxDelaySpin_->setSuffix(" us");
    networkForm->addRow("Max delay", maxDelaySpin_);

    bufferSizeSpin_ = new QSpinBox(networkPanel_);
    bufferSizeSpin_->setRange(0, 4096);
    bufferSizeSpin_->setSuffix(" KB");
    networkForm->addRow("Socket buffer", bufferSizeSpin_);

    probeSizeSpin_ = new QSpinBox(networkPanel_);
    probeSizeSpin_->setRange(0, 4096);
    probeSizeSpin_->setSuffix(" KB");
    networkForm->addRow("Probe size", probeSizeSpin_);

    analyzeDurationSpin_ = new QSpinBox(networkPanel_);
    analyzeDurationSpin_->setRange(0, 10000);
    analyzeDurationSpin_->setSuffix(" ms");
    networkForm->addRow("Analyze", analyzeDurationSpin_);

    videoQueueSpin_ = new QSpinBox(networkPanel_);
    videoQueueSpin_->setRange(1, 120);
    networkForm->addRow("Video queue", videoQueueSpin_);

    audioQueueSpin_ = new QSpinBox(networkPanel_);
    audioQueueSpin_->setRange(1, 120);
    networkForm->addRow("Audio queue", audioQueueSpin_);

    lateDropSpin_ = new QSpinBox(networkPanel_);
    lateDropSpin_->setRange(0, 2000);
    lateDropSpin_->setSuffix(" ms");
    networkForm->addRow("Late drop", lateDropSpin_);

    reconnectAttemptsSpin_ = new QSpinBox(networkPanel_);
    reconnectAttemptsSpin_->setRange(0, 20);
    networkForm->addRow("Reconnect tries", reconnectAttemptsSpin_);

    QLabel* settingsScopeLabel = new QLabel(
        "These controls edit next-open values. Actual playback may still be adjusted "
        "by source policy and live tuning.", networkPanel_);
    settingsScopeLabel->setWordWrap(true);
    settingsScopeLabel->setStyleSheet(
        "font-size: 11px; color: rgba(214,226,240,170); background: transparent;");
    networkLayout->addWidget(settingsScopeLabel);

    QLabel* effectivePolicyTitle = new QLabel("Effective Policy", networkPanel_);
    effectivePolicyTitle->setStyleSheet("font-size: 13px; font-weight: 600; color: #F5F5F7;");
    networkLayout->addWidget(effectivePolicyTitle);

    effectivePolicyLabel_ = new QLabel(
        "Configured next open: balanced | RTSP TCP | audio-master\n"
        "Active: no network stream", networkPanel_);
    effectivePolicyLabel_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    effectivePolicyLabel_->setWordWrap(true);
    effectivePolicyLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    effectivePolicyLabel_->setStyleSheet(
        "font-family: 'Consolas','Courier New',monospace; font-size: 11px; "
        "color: #D6E2F0; background: rgba(255,255,255,10); "
        "border: 1px solid rgba(255,255,255,18); border-radius: 8px; padding: 8px;");
    networkLayout->addWidget(effectivePolicyLabel_);

    auto* networkButtons = new QHBoxLayout();
    networkButtons->setContentsMargins(0, 4, 0, 0);
    networkButtons->setSpacing(8);

    QPushButton* balancedPresetBtn = new QPushButton("Balanced", networkPanel_);
    QPushButton* lowLatencyPresetBtn = new QPushButton("Low Latency", networkPanel_);
    QPushButton* applyNetworkBtn = new QPushButton("Apply", networkPanel_);

    QObject::connect(balancedPresetBtn, &QPushButton::clicked, [this]() {
        WriteData(NetworkViewData{});
        });

    QObject::connect(lowLatencyPresetBtn, &QPushButton::clicked, [this]() {
        NetworkViewData lowLatency;

        lowLatency.enableLowLatency = true;
        lowLatency.noBuffer = true;
        lowLatency.lowDelayFlag = true;
        lowLatency.videoQueuePackets = 8;
        lowLatency.audioQueuePackets = 12;
        lowLatency.lateDropMs = 120;
        WriteData(lowLatency);
        });

    QObject::connect(applyNetworkBtn, &QPushButton::clicked, [this]() {
        if (applyHandler_) applyHandler_();
        });

    networkButtons->addWidget(balancedPresetBtn);
    networkButtons->addWidget(lowLatencyPresetBtn);
    networkButtons->addStretch(1);
    networkButtons->addWidget(applyNetworkBtn);
    networkLayout->addLayout(networkButtons);

    networkStatsPanel_ = new QWidget(host_);
    networkStatsPanel_->setObjectName("networkStatsPanel");
    networkStatsPanel_->setAttribute(Qt::WA_StyledBackground, true);

    networkStatsPanel_->setStyleSheet(
        "QWidget#networkStatsPanel { background: rgba(8, 12, 18, 228); border: 1px solid rgba(255,255,255,34); border-radius: 10px; }"
        "QWidget#networkStatsPanel QLabel { color: #D6E2F0; }");
    networkStatsPanel_->setMinimumWidth(320);
    networkStatsPanel_->setVisible(false);

    auto* networkStatsLayout = new QVBoxLayout(networkStatsPanel_);
    networkStatsLayout->setContentsMargins(12, 12, 12, 12);
    networkStatsLayout->setSpacing(8);

    QLabel* networkStatsTitle = new QLabel("Network Stats", networkStatsPanel_);
    networkStatsTitle->setStyleSheet("font-size: 15px; font-weight: 600; color: #F5F5F7;");
    networkStatsLayout->addWidget(networkStatsTitle);

    networkStatsLabel_ = new QLabel("No network stream", networkStatsPanel_);

    networkStatsLabel_->setStyleSheet(
        "font-family: 'Consolas','Courier New',monospace; font-size: 12px; "
        "color: #D6E2F0; background: transparent; border: none;");
    networkStatsLabel_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    networkStatsLabel_->setWordWrap(true);
    networkStatsLayout->addWidget(networkStatsLabel_);
}

void NetworkViewQt::SetTogglePanelHandler(std::function<void()> handler) { togglePanelHandler_ = std::move(handler); }
void NetworkViewQt::SetToggleStatsHandler(std::function<void()> handler) { toggleStatsHandler_ = std::move(handler); }
void NetworkViewQt::SetApplyHandler(std::function<void()> handler) { applyHandler_ = std::move(handler); }

QPushButton* NetworkViewQt::ToggleButton() const { return toggleButton_; }
QPushButton* NetworkViewQt::StatsButton() const { return statsButton_; }
QWidget* NetworkViewQt::Panel() const { return networkPanel_; }
QWidget* NetworkViewQt::StatsPanel() const { return networkStatsPanel_; }
bool NetworkViewQt::IsPanelVisible() const { return networkPanel_ && networkPanel_->isVisible(); }
bool NetworkViewQt::IsStatsPanelVisible() const { return networkStatsPanel_ && networkStatsPanel_->isVisible(); }
void NetworkViewQt::SetPanelVisible(bool visible) { if (networkPanel_) networkPanel_->setVisible(visible); }
void NetworkViewQt::SetStatsPanelVisible(bool visible) { if (networkStatsPanel_) networkStatsPanel_->setVisible(visible); }
void NetworkViewQt::SetToggleButtonChecked(bool checked) { if (toggleButton_) toggleButton_->setChecked(checked); }
void NetworkViewQt::SetStatsButtonChecked(bool checked) { if (statsButton_) statsButton_->setChecked(checked); }
void NetworkViewQt::RaisePanel() { if (networkPanel_) networkPanel_->raise(); }
void NetworkViewQt::RaiseStatsPanel() { if (networkStatsPanel_) networkStatsPanel_->raise(); }

void NetworkViewQt::LayoutPanels(int videoWidth, int hostHeight)
{
    const int panelMargin = 12;
    const int panelTop = 48;
    int stackedTop = panelTop;

    if (networkPanel_)
    {

        const QSize hint = networkPanel_->sizeHint().expandedTo(QSize(networkPanel_->minimumWidth(), 0));
        const int panelW = std::max(220, std::min(videoWidth - panelMargin * 2, hint.width()));
        const int panelH = std::max(hint.height(), 430);
        networkPanel_->resize(panelW, panelH);

        networkPanel_->move(std::max(panelMargin, videoWidth - panelW - panelMargin), stackedTop);

        if (networkPanel_->isVisible())
        {
            networkPanel_->raise();
            stackedTop = networkPanel_->y() + networkPanel_->height() + 10;
        }
    }

    if (networkStatsPanel_)
    {
        const QSize hint = networkStatsPanel_->sizeHint().expandedTo(QSize(networkStatsPanel_->minimumWidth(), 0));
        const int panelW = std::max(220, std::min(videoWidth - panelMargin * 2, hint.width()));
        const int panelH = std::max(hint.height(), 250);

        int statsY = (networkPanel_ && networkPanel_->isVisible()) ? stackedTop : panelTop;

        if (statsY + panelH > hostHeight - panelMargin)
            statsY = std::max(panelTop, hostHeight - panelH - panelMargin);

        networkStatsPanel_->resize(panelW, panelH);
        networkStatsPanel_->move(std::max(panelMargin, videoWidth - panelW - panelMargin), statsY);
        if (networkStatsPanel_->isVisible())
            networkStatsPanel_->raise();
    }
}

QList<QWidget*> NetworkViewQt::AutoHideWidgets() const
{
    return { toggleButton_, statsButton_ };
}

void NetworkViewQt::InstallEventFilters(QObject* filter) const
{
    if (!filter) return;

    const QList<QWidget*> widgets = { toggleButton_, statsButton_, networkPanel_, networkStatsPanel_ };
    for (QWidget* widget : widgets)
    {
        if (widget) widget->installEventFilter(filter);
    }

    if (networkPanel_)
    {

        const QList<QWidget*> children = networkPanel_->findChildren<QWidget*>();
        for (QWidget* widget : children)
            widget->installEventFilter(filter);
    }

    if (networkStatsPanel_)
    {
        const QList<QWidget*> children = networkStatsPanel_->findChildren<QWidget*>();
        for (QWidget* widget : children)
            widget->installEventFilter(filter);
    }
}

NetworkViewData NetworkViewQt::ReadData() const
{
    NetworkViewData data;

    data.forceTcpForRtsp = !rtspTransportCombo_ || rtspTransportCombo_->currentIndex() == 0;
    data.liveClockPolicy = (liveClockCombo_ && liveClockCombo_->currentIndex() == 1)
        ? LiveClockPolicy::AudioMasterDropLateVideo
        : LiveClockPolicy::AudioMaster;
    data.enableLowLatency = lowLatencyCheck_ && lowLatencyCheck_->isChecked();
    data.noBuffer = noBufferCheck_ && noBufferCheck_->isChecked();
    data.lowDelayFlag = lowDelayCheck_ && lowDelayCheck_->isChecked();
    data.reconnectEnabled = reconnectCheck_ && reconnectCheck_->isChecked();
    data.connectTimeoutMs = connectTimeoutSpin_ ? connectTimeoutSpin_->value() : data.connectTimeoutMs;
    data.maxDelayUs = maxDelaySpin_ ? maxDelaySpin_->value() : data.maxDelayUs;
    data.bufferSizeKb = bufferSizeSpin_ ? bufferSizeSpin_->value() : data.bufferSizeKb;
    data.probeSizeKb = probeSizeSpin_ ? probeSizeSpin_->value() : data.probeSizeKb;
    data.analyzeDurationMs = analyzeDurationSpin_ ? analyzeDurationSpin_->value() : data.analyzeDurationMs;
    data.videoQueuePackets = videoQueueSpin_ ? videoQueueSpin_->value() : data.videoQueuePackets;
    data.audioQueuePackets = audioQueueSpin_ ? audioQueueSpin_->value() : data.audioQueuePackets;
    data.lateDropMs = lateDropSpin_ ? lateDropSpin_->value() : data.lateDropMs;
    data.reconnectAttempts = reconnectAttemptsSpin_ ? reconnectAttemptsSpin_->value() : data.reconnectAttempts;
    return data;
}

void NetworkViewQt::WriteData(const NetworkViewData& data)
{

    const QSignalBlocker blockTransport(rtspTransportCombo_);
    const QSignalBlocker blockClock(liveClockCombo_);
    const QSignalBlocker blockLowLatency(lowLatencyCheck_);
    const QSignalBlocker blockNoBuffer(noBufferCheck_);
    const QSignalBlocker blockLowDelay(lowDelayCheck_);
    const QSignalBlocker blockReconnect(reconnectCheck_);
    const QSignalBlocker blockConnectTimeout(connectTimeoutSpin_);
    const QSignalBlocker blockMaxDelay(maxDelaySpin_);
    const QSignalBlocker blockBuffer(bufferSizeSpin_);
    const QSignalBlocker blockProbe(probeSizeSpin_);
    const QSignalBlocker blockAnalyze(analyzeDurationSpin_);
    const QSignalBlocker blockVideoQueue(videoQueueSpin_);
    const QSignalBlocker blockAudioQueue(audioQueueSpin_);
    const QSignalBlocker blockLateDrop(lateDropSpin_);
    const QSignalBlocker blockReconnectAttempts(reconnectAttemptsSpin_);

    if (rtspTransportCombo_) rtspTransportCombo_->setCurrentIndex(data.forceTcpForRtsp ? 0 : 1);
    if (liveClockCombo_) liveClockCombo_->setCurrentIndex(data.liveClockPolicy == LiveClockPolicy::AudioMasterDropLateVideo ? 1 : 0);
    if (lowLatencyCheck_) lowLatencyCheck_->setChecked(data.enableLowLatency);
    if (noBufferCheck_) noBufferCheck_->setChecked(data.noBuffer);
    if (lowDelayCheck_) lowDelayCheck_->setChecked(data.lowDelayFlag);
    if (reconnectCheck_) reconnectCheck_->setChecked(data.reconnectEnabled);
    if (connectTimeoutSpin_) connectTimeoutSpin_->setValue(data.connectTimeoutMs);
    if (maxDelaySpin_) maxDelaySpin_->setValue(data.maxDelayUs);
    if (bufferSizeSpin_) bufferSizeSpin_->setValue(data.bufferSizeKb);
    if (probeSizeSpin_) probeSizeSpin_->setValue(data.probeSizeKb);
    if (analyzeDurationSpin_) analyzeDurationSpin_->setValue(data.analyzeDurationMs);
    if (videoQueueSpin_) videoQueueSpin_->setValue(data.videoQueuePackets);
    if (audioQueueSpin_) audioQueueSpin_->setValue(data.audioQueuePackets);
    if (lateDropSpin_) lateDropSpin_->setValue(data.lateDropMs);
    if (reconnectAttemptsSpin_) reconnectAttemptsSpin_->setValue(data.reconnectAttempts);
}

void NetworkViewQt::SetEffectivePolicyText(const QString& text)
{
    if (effectivePolicyLabel_)
        effectivePolicyLabel_->setText(text);
}

QLabel* NetworkViewQt::StatsLabel() const
{
    return networkStatsLabel_;
}
