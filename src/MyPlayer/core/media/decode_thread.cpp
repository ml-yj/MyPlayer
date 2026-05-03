#include "decode_thread.h"
#include "decode.h"

class PacketQueueBuffer
{
public:

    explicit PacketQueueBuffer(DecodeThreadState& state);

    void Push(AVPacket* pkt);

    void Push(AVPacket* pkt, quint64 generation, quint64 serial);

    void PushFlush(quint64 generation, quint64 serial);

    void PushDrain(quint64 generation, quint64 serial);

    PacketEnvelope Pop();

    void ClearQueue();

    int GetQueueSize();

    void SetOverflowPolicy(QueueOverflowPolicy policy);

private:

    DecodeThreadState& state_;
};

DecodeThread::DecodeThread()

    : maxList(state_.maxList)
    , isExit(state_.isExit)
    , decode(state_.decode)
    , cv(state_.cv)
{

    packetQueue_ = std::make_unique<PacketQueueBuffer>(state_);

    if (!state_.decode)
        state_.decode = new Decode();
}

DecodeThread::~DecodeThread()
{

    Close();
}

void DecodeThread::Push(AVPacket* pkt)
{
    if (packetQueue_)
        packetQueue_->Push(pkt);
}

void DecodeThread::PushWithGeneration(AVPacket* pkt, quint64 generation)
{

    PushWithEpoch(pkt, generation, generation);
}

void DecodeThread::PushWithEpoch(AVPacket* pkt, quint64 generation, quint64 serial)
{
    if (packetQueue_)
        packetQueue_->Push(pkt, generation, serial);
}

void DecodeThread::PushDrain(quint64 generation, quint64 serial)
{

    if (packetQueue_)
        packetQueue_->PushDrain(generation, serial);
}

PacketEnvelope DecodeThread::Pop()
{

    return packetQueue_ ? packetQueue_->Pop() : PacketEnvelope{};
}

void DecodeThread::Clear()
{

    {
        std::lock_guard<std::mutex> lock(state_.mux);
        if (state_.decode)
            state_.decode->Clear();
    }

    if (packetQueue_)
        packetQueue_->ClearQueue();
}

void DecodeThread::Close()
{

    isExit.store(true);

    cv.notify_all();

    wait();

    Clear();

    Decode* activeDecode = nullptr;
    {
        std::lock_guard<std::mutex> lock(state_.mux);
        activeDecode = state_.decode;
        state_.decode = nullptr;
    }

    if (activeDecode)
    {
        activeDecode->Close();
        delete activeDecode;
    }
}

int DecodeThread::GetQueueSize()
{
    return packetQueue_ ? packetQueue_->GetQueueSize() : 0;
}

void DecodeThread::SetOverflowPolicy(QueueOverflowPolicy policy)
{
    if (packetQueue_)
        packetQueue_->SetOverflowPolicy(policy);
}

int DecodeThread::GetDroppedPacketCount() const
{
    return state_.droppedPacketCount.load();
}

void DecodeThread::ResetDroppedPacketCount()
{
    state_.droppedPacketCount.store(0);
}

Decode* DecodeThread::GetDecode()
{

    return state_.decode;
}

void DecodeThread::SetQueueGeneration(quint64 generation)
{
    SetQueueEpoch(generation, generation);
}

void DecodeThread::SetQueueEpoch(quint64 generation, quint64 serial)
{

    state_.generation.store(generation);
    state_.serial.store(serial);
}

void DecodeThread::SetQueueEpoch(const StreamEpoch& epoch)
{
    SetQueueEpoch(epoch.generation, epoch.serial);
}

void DecodeThread::ResetQueueEpoch(quint64 generation, quint64 serial, bool enqueueFlush)
{

    SetQueueEpoch(generation, serial);

    if (!packetQueue_)
        return;

    packetQueue_->ClearQueue();

    if (enqueueFlush)
        packetQueue_->PushFlush(generation, serial);
}

void DecodeThread::ResetQueueEpoch(const StreamEpoch& epoch, bool enqueueFlush)
{
    ResetQueueEpoch(epoch.generation, epoch.serial, enqueueFlush);
}

quint64 DecodeThread::GetQueueGeneration() const
{
    return state_.generation.load();
}

quint64 DecodeThread::GetQueueSerial() const
{
    return state_.serial.load();
}

StreamEpoch DecodeThread::GetQueueEpoch() const
{
    return StreamEpoch{ state_.generation.load(), state_.serial.load() };
}

extern "C" {
#include <libavcodec/avcodec.h>
}

#include <algorithm>
#include <chrono>

namespace
{

    void FreeEnvelope(PacketEnvelope& envelope)
    {

        if (envelope.packet)
            av_packet_free(&envelope.packet);
    }

    std::list<PacketEnvelope>::iterator FindDroppableEnvelope(std::list<PacketEnvelope>& packets)
    {

        return std::find_if(packets.begin(), packets.end(), [](const PacketEnvelope& envelope) {
            return envelope.kind == PacketEnvelopeKind::Data;
            });
    }
}

PacketQueueBuffer::PacketQueueBuffer(DecodeThreadState& state)
    : state_(state)
{
}

void PacketQueueBuffer::Push(AVPacket* pkt)
{

    Push(pkt, state_.generation.load(), state_.serial.load());
}

void PacketQueueBuffer::Push(AVPacket* pkt, quint64 generation, quint64 serial)
{

    if (!pkt)
        return;

    while (!state_.isExit.load())
    {

        std::unique_lock<std::mutex> lock(state_.mux);

        if (static_cast<int>(state_.packets.size()) >= state_.maxList
            && state_.overflowPolicy == QueueOverflowPolicy::DropOldest)
        {

            auto it = FindDroppableEnvelope(state_.packets);

            if (it != state_.packets.end())
            {
                PacketEnvelope old = *it;
                state_.packets.erase(it);
                FreeEnvelope(old);
                state_.droppedPacketCount.fetch_add(1);
            }
        }

        if (static_cast<int>(state_.packets.size()) < state_.maxList)
        {

            state_.packets.push_back(PacketEnvelope{ pkt, generation, serial, PacketEnvelopeKind::Data });

            state_.cv.notify_one();
            return;
        }

        state_.cv.wait_for(lock, std::chrono::milliseconds(1), [this]() {

            return static_cast<int>(state_.packets.size()) < state_.maxList || state_.isExit.load();
            });
    }

    av_packet_free(&pkt);
}

void PacketQueueBuffer::PushFlush(quint64 generation, quint64 serial)
{
    while (!state_.isExit.load())
    {
        std::unique_lock<std::mutex> lock(state_.mux);

        if (static_cast<int>(state_.packets.size()) >= state_.maxList)
        {
            auto it = FindDroppableEnvelope(state_.packets);
            if (it != state_.packets.end())
            {
                PacketEnvelope old = *it;
                state_.packets.erase(it);
                FreeEnvelope(old);
                state_.droppedPacketCount.fetch_add(1);
            }
        }

        if (static_cast<int>(state_.packets.size()) < state_.maxList)
        {

            state_.packets.push_back(PacketEnvelope{ nullptr, generation, serial, PacketEnvelopeKind::Flush });
            state_.cv.notify_one();
            return;
        }

        state_.cv.wait_for(lock, std::chrono::milliseconds(1), [this]() {
            return static_cast<int>(state_.packets.size()) < state_.maxList || state_.isExit.load();
            });
    }
}

void PacketQueueBuffer::PushDrain(quint64 generation, quint64 serial)
{
    while (!state_.isExit.load())
    {
        std::unique_lock<std::mutex> lock(state_.mux);

        if (static_cast<int>(state_.packets.size()) >= state_.maxList)
        {
            auto it = FindDroppableEnvelope(state_.packets);
            if (it != state_.packets.end())
            {
                PacketEnvelope old = *it;
                state_.packets.erase(it);
                FreeEnvelope(old);
                state_.droppedPacketCount.fetch_add(1);
            }
        }

        if (static_cast<int>(state_.packets.size()) < state_.maxList)
        {

            state_.packets.push_back(PacketEnvelope{ nullptr, generation, serial, PacketEnvelopeKind::Drain });
            state_.cv.notify_one();
            return;
        }

        state_.cv.wait_for(lock, std::chrono::milliseconds(1), [this]() {
            return static_cast<int>(state_.packets.size()) < state_.maxList || state_.isExit.load();
            });
    }
}

PacketEnvelope PacketQueueBuffer::Pop()
{

    std::unique_lock<std::mutex> lock(state_.mux);

    state_.cv.wait_for(lock, std::chrono::milliseconds(5), [this]() {

        return !state_.packets.empty() || state_.isExit.load();
        });

    if (state_.packets.empty())
        return {};

    PacketEnvelope pkt = state_.packets.front();

    state_.packets.pop_front();

    state_.cv.notify_one();

    return pkt;
}

void PacketQueueBuffer::ClearQueue()
{

    std::lock_guard<std::mutex> lock(state_.mux);

    while (!state_.packets.empty())
    {
        PacketEnvelope pkt = state_.packets.front();

        FreeEnvelope(pkt);

        state_.packets.pop_front();
    }

    state_.cv.notify_all();
}

int PacketQueueBuffer::GetQueueSize()
{

    std::lock_guard<std::mutex> lock(state_.mux);
    return static_cast<int>(state_.packets.size());
}

void PacketQueueBuffer::SetOverflowPolicy(QueueOverflowPolicy policy)
{
    std::lock_guard<std::mutex> lock(state_.mux);
    state_.overflowPolicy = policy;
}
