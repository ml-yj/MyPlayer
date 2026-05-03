#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <list>
#include <mutex>
#include <vector>

#include "ai_types.h"
#include "../session/demux_thread_shared.h"

enum class InferenceLane
{
    Cpu,
    Gpu,
};

struct InferenceTaskProfile
{
    AiCapability capability = AiCapability::Asr;
    InferenceLane lane = InferenceLane::Cpu;
    int priority = 0;
    bool dropIfLate = false;
    int maxQueueDelayMs = 0;
    int waitPollMs = 5;
};

struct InferenceTaskTicket
{
    std::uint64_t id = 0;
    InferenceTaskProfile profile;
    std::chrono::steady_clock::time_point enqueuedAt{};
    bool queued = false;
    bool acquired = false;
};

enum class InferenceAcquireResult
{
    Acquired,
    Dropped,
    Cancelled,
};

class InferenceScheduler
{
public:
    InferenceScheduler() = default;

    void RegisterState(DemuxThreadState* state);
    void UnregisterState(DemuxThreadState* state);
    InferenceTaskTicket Enqueue(const InferenceTaskProfile& profile);
    InferenceAcquireResult Acquire(
        InferenceTaskTicket& ticket,
        const std::function<bool()>& shouldCancel = {});
    void Release(InferenceTaskTicket& ticket, bool completed = true);

private:

    struct Waiter
    {
        std::uint64_t id = 0;
        InferenceTaskProfile profile;
        std::chrono::steady_clock::time_point enqueuedAt{};
    };

    std::list<Waiter>::iterator FindWaiterLocked(std::uint64_t id);
    bool CanAcquireLocked(std::list<Waiter>::iterator it) const;
    void InsertWaiterLocked(Waiter waiter);
    void RecordDroppedLocked(AiCapability capability);
    void RecordCancelledLocked(AiCapability capability);
    void SyncStatsLocked() const;
    int MaxActiveForLane(InferenceLane lane) const;
    void UpdateAverageWaitLocked(DemuxThreadState& state, int waitMs) const;
    void ApplyStatsToStateLocked(DemuxThreadState& state, int cpuQueueDepth, int gpuQueueDepth) const;

    mutable std::mutex mux_;
    std::condition_variable cv_;
    std::list<Waiter> waiters_;
    std::vector<DemuxThreadState*> states_;
    std::uint64_t nextId_ = 1;
    int activeCpuTasks_ = 0;
    int activeGpuTasks_ = 0;
};
