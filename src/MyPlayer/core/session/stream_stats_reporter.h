#pragma once

#include <string>

#include "demux_thread_shared.h"

class StreamStatsReporter
{
public:
    StreamStatsReporter(DemuxThreadState& state, SessionResources& resources);

    void PublishStatusEventLocked(const std::string& text);
    int FetchRenderedFrames();
    std::string GetOsdDetail();
    StreamStatsSnapshot GetStreamStats();
    quint64 GetStatusEventGeneration() const;
    std::string GetStatusEventText();

private:
    DemuxThreadState& state;
    SessionResources& resources;
};
