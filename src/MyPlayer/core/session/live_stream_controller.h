#pragma once

#include <functional>
#include <string>

#include "demux_thread_shared.h"

class StreamSessionCore;

class LiveStreamController
{
public:
    LiveStreamController(DemuxThreadState& state, SessionResources& resources,
        StreamSessionCore& sessionCore, std::function<void(const std::string&)> publishStatusEvent);

    void ResetRuntimeTuningLocked();
    void UpdateRuntimeTuningLocked(bool force = false);
    bool MaybeAutoRecoverLocked();

private:
    DemuxThreadState& state;
    SessionResources& resources;
    StreamSessionCore& sessionCore;
    std::function<void(const std::string&)> publishStatusEvent;
};
