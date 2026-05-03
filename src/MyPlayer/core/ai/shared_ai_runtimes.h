#pragma once

#include "../../features/detector/detector_common.h"

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#ifndef ORT_API_MANUAL_INIT
#define ORT_API_MANUAL_INIT
#endif
#include <onnxruntime_cxx_api.h>

struct whisper_context;

struct SharedVadModel
{
    ~SharedVadModel();

    std::shared_ptr<Ort::Env> ortEnv;
    std::shared_ptr<Ort::Session> ortSession;
    bool usingGpu = false;
    std::string inputName;
    std::string stateName;
    std::string srName;
    std::string outputName;
    std::string stateOutName;
};

class SharedVadRuntime
{
public:
    static SharedVadRuntime& Instance();

    std::shared_ptr<const SharedVadModel> AcquireModel(const std::wstring& modelPath);

private:
    SharedVadRuntime() = default;

    std::mutex cacheMutex_;
    std::unordered_map<std::string, std::weak_ptr<const SharedVadModel>> modelCache_;
};

struct WhisperSharedModel
{
    ~WhisperSharedModel();

    std::string modelPath;
    whisper_context* context = nullptr;
    bool usingGpu = false;
};

class SharedWhisperRuntime
{
public:
    static SharedWhisperRuntime& Instance();

    std::shared_ptr<const WhisperSharedModel> AcquireModel(const std::string& modelPath);

private:
    SharedWhisperRuntime() = default;

    std::mutex cacheMutex_;
    std::unordered_map<std::string, std::weak_ptr<const WhisperSharedModel>> modelCache_;
};

struct DetectorSharedModel
{
    std::string modelPath;
    std::shared_ptr<Ort::Env> ortEnv;
    std::shared_ptr<Ort::Session> ortSession;
    DetectorModelFamily modelFamily = DetectorModelFamily::Unknown;
    int inputWidth = kDefaultInputSize;
    int inputHeight = kDefaultInputSize;
    bool canUseGpuPreprocess = true;
    bool yoloUsesObjectness = false;
    int decodedClassCount = 0;
    DetectorBackend inferenceBackend = DetectorBackend::Unknown;
    std::string ortInputName;
    std::vector<std::string> ortOutputNames;
    std::vector<const char*> ortOutputNameViews;
};

class SharedDetectorRuntime
{
public:
    static SharedDetectorRuntime& Instance();

    std::shared_ptr<const DetectorSharedModel> AcquireModel(const std::string& modelPath);

private:
    SharedDetectorRuntime() = default;

    std::mutex cacheMutex_;
    std::unordered_map<std::string, std::weak_ptr<const DetectorSharedModel>> modelCache_;
};
