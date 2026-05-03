#pragma once

#include "recording_models.h"
#include "recording_service.h"

#include <memory>
#include <mutex>
#include <optional>

struct AVPacket;
class Demux;
class SegmentWriter;

class SessionRecorder
{
public:
    SessionRecorder();
    ~SessionRecorder();

    bool StartRecording(const RecordingConfiguration& configuration, std::string* errorMessage = nullptr);
    void StopRecording();
    bool IsRecording() const;
    RecordingRuntimeSnapshot GetRuntimeSnapshot() const;

    bool OnSessionOpened(Demux& demux, const std::string& sourceUrl, std::string* errorMessage = nullptr);
    void OnSessionClosed();
    void OnPacket(const AVPacket* packet, bool isAudio);
    bool RecordEvent(ArchiveEventRecord* record, std::string* errorMessage = nullptr);

private:
    bool StartSegmentLocked(Demux& demux, const std::string& sourceUrl, const QDateTime& nowUtc,
        std::string* errorMessage);
    void FinalizeSegmentLocked(const QDateTime& nowUtc);
    void SetRuntimeErrorLocked(const QString& errorMessage);
    void ClearCurrentSegmentLocked();

    mutable std::mutex mux_;
    RecordingConfiguration configuration_;
    RecordingService service_;
    std::optional<RecordingActiveSegment> activeSegment_;
    std::unique_ptr<SegmentWriter> writer_;
    Demux* currentDemux_ = nullptr;
    std::string currentSourceUrl_;
    RecordingRuntimeSnapshot runtimeSnapshot_;
};
