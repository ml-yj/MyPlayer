#pragma once

#include <QThread>
#include "demux_types.h"

#include <atomic>
#include <condition_variable>
#include <list>
#include <memory>
#include <mutex>

struct AVPacket;
class Decode;
class PacketQueueBuffer;

enum class PacketEnvelopeKind
{
    Empty,
    Data,
    Flush,
    Drain,
};

struct PacketEnvelope
{
    AVPacket* packet = nullptr;
    quint64 generation = 0;
    quint64 serial = 0;
    PacketEnvelopeKind kind = PacketEnvelopeKind::Empty;
};

enum class QueueOverflowPolicy
{
    BlockProducer,
    DropOldest,
};

struct DecodeThreadState
{
    Decode* decode = nullptr;
    std::list<PacketEnvelope> packets;
    std::mutex mux;
    std::condition_variable cv;
    std::atomic<int> droppedPacketCount{ 0 };
    QueueOverflowPolicy overflowPolicy = QueueOverflowPolicy::BlockProducer;
    int maxList = 30;
    std::atomic<bool> isExit{ false };
    std::atomic<quint64> generation{ 1 };
    std::atomic<quint64> serial{ 1 };
};
class DecodeThread : public QThread
{
public:

    DecodeThread();
    ~DecodeThread() override;

    virtual void Push(AVPacket* pkt);

    virtual void PushWithGeneration(AVPacket* pkt, quint64 generation);
    virtual void PushWithEpoch(AVPacket* pkt, quint64 generation, quint64 serial);

    virtual void PushDrain(quint64 generation, quint64 serial);

    virtual PacketEnvelope Pop();

    virtual void Clear();
    virtual void Close();

    int GetQueueSize();
    void SetOverflowPolicy(QueueOverflowPolicy policy);
    int GetDroppedPacketCount() const;
    void ResetDroppedPacketCount();

    Decode* GetDecode();

    void SetQueueGeneration(quint64 generation);
    void SetQueueEpoch(quint64 generation, quint64 serial);
    void SetQueueEpoch(const StreamEpoch& epoch);
    void ResetQueueEpoch(quint64 generation, quint64 serial, bool enqueueFlush = true);
    void ResetQueueEpoch(const StreamEpoch& epoch, bool enqueueFlush = true);
    quint64 GetQueueGeneration() const;
    quint64 GetQueueSerial() const;
    StreamEpoch GetQueueEpoch() const;

    int& maxList;
    std::atomic<bool>& isExit;

protected:

    Decode*& decode;
    std::condition_variable& cv;

private:

    DecodeThreadState state_;

    std::unique_ptr<PacketQueueBuffer> packetQueue_;

};
