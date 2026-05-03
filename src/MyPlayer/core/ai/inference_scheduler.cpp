

#include "inference_scheduler.h"

#include <algorithm>

void InferenceScheduler::RegisterState(DemuxThreadState* state)
{
    if (!state)
        return;

    std::lock_guard<std::mutex> lock(mux_);
    if (std::find(states_.begin(), states_.end(), state) == states_.end())
        states_.push_back(state);
    SyncStatsLocked();
}

void InferenceScheduler::UnregisterState(DemuxThreadState* state)
{
    std::lock_guard<std::mutex> lock(mux_);
    states_.erase(std::remove(states_.begin(), states_.end(), state), states_.end());
    SyncStatsLocked();
}

InferenceTaskTicket InferenceScheduler::Enqueue(const InferenceTaskProfile& profile)
{
    std::lock_guard<std::mutex> lock(mux_);
    InferenceTaskTicket ticket;
    ticket.id = nextId_++;
    ticket.profile = profile;
    ticket.enqueuedAt = std::chrono::steady_clock::now();
    ticket.queued = true;

    Waiter waiter;
    waiter.id = ticket.id;
    waiter.profile = profile;
    waiter.enqueuedAt = ticket.enqueuedAt;
    InsertWaiterLocked(std::move(waiter));
    SyncStatsLocked();
    cv_.notify_all();
    return ticket;
}

InferenceAcquireResult InferenceScheduler::Acquire(
    InferenceTaskTicket& ticket,
    const std::function<bool()>& shouldCancel)
{
    if (!ticket.queued)
        return InferenceAcquireResult::Cancelled;

    std::unique_lock<std::mutex> lock(mux_);
    while (ticket.queued)
    {
        auto it = FindWaiterLocked(ticket.id);
        if (it == waiters_.end())
            return ticket.acquired ? InferenceAcquireResult::Acquired : InferenceAcquireResult::Cancelled;

        if (shouldCancel && shouldCancel())
        {
            RecordCancelledLocked(it->profile.capability);
            waiters_.erase(it);
            ticket.queued = false;
            SyncStatsLocked();
            cv_.notify_all();
            return InferenceAcquireResult::Cancelled;
        }

        const auto now = std::chrono::steady_clock::now();
        const int waitMs = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(now - ticket.enqueuedAt).count());
        if (ticket.profile.dropIfLate && ticket.profile.maxQueueDelayMs > 0
            && waitMs > ticket.profile.maxQueueDelayMs)
        {
            RecordDroppedLocked(it->profile.capability);
            waiters_.erase(it);
            ticket.queued = false;
            SyncStatsLocked();
            cv_.notify_all();
            return InferenceAcquireResult::Dropped;
        }

        if (CanAcquireLocked(it))
        {
            if (it->profile.lane == InferenceLane::Gpu)
                ++activeGpuTasks_;
            else
                ++activeCpuTasks_;

            waiters_.erase(it);
            ticket.queued = false;
            ticket.acquired = true;

            for (DemuxThreadState* state : states_)
            {
                if (!state)
                    continue;
                state->aiLastWaitMs.store(waitMs);
                UpdateAverageWaitLocked(*state, waitMs);
            }

            SyncStatsLocked();
            return InferenceAcquireResult::Acquired;
        }

        cv_.wait_for(lock, std::chrono::milliseconds(std::max(1, ticket.profile.waitPollMs)));
    }

    return InferenceAcquireResult::Cancelled;
}

void InferenceScheduler::Release(InferenceTaskTicket& ticket, bool completed)
{
    if (!ticket.acquired)
        return;

    std::lock_guard<std::mutex> lock(mux_);
    if (ticket.profile.lane == InferenceLane::Gpu)
    {
        if (activeGpuTasks_ > 0)
            --activeGpuTasks_;
    }
    else
    {
        if (activeCpuTasks_ > 0)
            --activeCpuTasks_;
    }

    if (completed)
    {
        for (DemuxThreadState* state : states_)
        {
            if (state)
                state->aiCompletedTasks.fetch_add(1);
        }
    }

    ticket.acquired = false;
    SyncStatsLocked();
    cv_.notify_all();
}

std::list<InferenceScheduler::Waiter>::iterator InferenceScheduler::FindWaiterLocked(std::uint64_t id)
{
    return std::find_if(waiters_.begin(), waiters_.end(),
        [id](const Waiter& waiter) { return waiter.id == id; });
}

bool InferenceScheduler::CanAcquireLocked(std::list<Waiter>::iterator it) const
{
    for (auto cursor = waiters_.begin(); cursor != it; ++cursor)
    {
        if (cursor->profile.lane == it->profile.lane)
            return false;
    }

    const int activeTasks = it->profile.lane == InferenceLane::Gpu
        ? activeGpuTasks_
        : activeCpuTasks_;
    return activeTasks < MaxActiveForLane(it->profile.lane);
}

void InferenceScheduler::InsertWaiterLocked(Waiter waiter)
{
    auto insertPos = waiters_.end();
    for (auto it = waiters_.begin(); it != waiters_.end(); ++it)
    {
        if (it->profile.lane != waiter.profile.lane)
            continue;
        if (waiter.profile.priority > it->profile.priority)
        {
            insertPos = it;
            break;
        }
    }

    if (insertPos == waiters_.end())
        waiters_.push_back(std::move(waiter));
    else
        waiters_.insert(insertPos, std::move(waiter));
}

void InferenceScheduler::RecordDroppedLocked(AiCapability capability)
{
    for (DemuxThreadState* state : states_)
    {
        if (!state)
            continue;
        state->aiDroppedTasks.fetch_add(1);
        if (capability == AiCapability::Detector)
            state->aiDetectorDroppedTasks.fetch_add(1);
    }
}

void InferenceScheduler::RecordCancelledLocked(AiCapability capability)
{
    for (DemuxThreadState* state : states_)
    {
        if (!state)
            continue;
        state->aiCancelledTasks.fetch_add(1);
        if (capability == AiCapability::Detector)
            state->aiDetectorCancelledTasks.fetch_add(1);
    }
}

void InferenceScheduler::SyncStatsLocked() const
{
    int cpuQueueDepth = 0;
    int gpuQueueDepth = 0;
    for (const Waiter& waiter : waiters_)
    {
        if (waiter.profile.lane == InferenceLane::Gpu)
            ++gpuQueueDepth;
        else
            ++cpuQueueDepth;
    }

    for (DemuxThreadState* state : states_)
    {
        if (state)
            ApplyStatsToStateLocked(*state, cpuQueueDepth, gpuQueueDepth);
    }
}

int InferenceScheduler::MaxActiveForLane(InferenceLane lane) const
{
    return lane == InferenceLane::Gpu ? 1 : 2;
}

void InferenceScheduler::UpdateAverageWaitLocked(DemuxThreadState& state, int waitMs) const
{
    state.aiAccumulatedWaitMs.fetch_add(waitMs);
    const int acquireCount = state.aiAcquireCount.fetch_add(1) + 1;
    const long long accumulatedWaitMs = state.aiAccumulatedWaitMs.load();
    state.aiAverageWaitMs.store(
        acquireCount > 0 ? static_cast<int>(accumulatedWaitMs / acquireCount) : 0);
}

void InferenceScheduler::ApplyStatsToStateLocked(
    DemuxThreadState& state, int cpuQueueDepth, int gpuQueueDepth) const
{
    state.aiCpuQueueDepth.store(cpuQueueDepth);
    state.aiGpuQueueDepth.store(gpuQueueDepth);
    state.aiCpuActiveTasks.store(activeCpuTasks_);
    state.aiGpuActiveTasks.store(activeGpuTasks_);
}
