#pragma once

#include "detector_types.h"

#include <QString>
#include <string>
#include <vector>

constexpr int kDefaultInputSize = 640;
constexpr int kGpuFixedInputSize = 640;
constexpr float kLetterboxFillValue = 114.0f;
constexpr float kYoloNmsThreshold = 0.45f;
constexpr size_t kMaxYoloCandidatesBeforeNms = 256;
constexpr size_t kMaxBoxesBeforeTracking = 128;
constexpr uint32_t kOrtApiVersion = 17;

bool EnsureOrtApiInitialized();
std::vector<QString> DefaultCocoNames();
std::string ToLowerCopy(std::string text);
std::wstring Utf8ToWide(const std::string& text);
DetectorModelFamily GuessFamilyFromPath(const std::string& modelPath);
float Clamp01(float value);
void UndoLetterbox(DetectionBox& box, int inputWidth, int inputHeight, int origWidth, int origHeight);
float IntersectionOverUnion(const DetectionBox& a, const DetectionBox& b);
const char* BackendToCString(DetectorBackend backend);
QString BackendToQString(DetectorBackend backend);
