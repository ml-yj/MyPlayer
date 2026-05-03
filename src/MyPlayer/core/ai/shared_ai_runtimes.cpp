#include "shared_ai_runtimes.h"

#include "../../common/diagnostics/logger.h"

#include <QDir>
#include <QString>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <whisper.h>

namespace
{
constexpr uint32_t kVadOrtApiVersion = 17;

bool EnsureVadOrtApiInitialized()
{
    static bool initialized = false;
    static bool ok = false;
    if (initialized)
        return ok;

    initialized = true;
    const OrtApiBase* base = OrtGetApiBase();
    if (!base)
        return false;

    const OrtApi* api = base->GetApi(kVadOrtApiVersion);
    if (!api)
        return false;

    Ort::InitApi(api);
    ok = true;
    return true;
}

std::string Narrow(const std::wstring& text)
{
    return QString::fromStdWString(text).toStdString();
}

std::string NormalizeVadModelKey(const std::wstring& modelPath)
{
    QString normalized = QDir::cleanPath(QString::fromStdWString(modelPath));
#ifdef _WIN32
    normalized = normalized.toLower();
#endif
    return normalized.toStdString();
}

std::string NormalizeModelKey(const std::string& modelPath)
{
    QString normalized = QDir::cleanPath(QString::fromUtf8(modelPath.c_str()));
#ifdef _WIN32
    normalized = normalized.toLower();
#endif
    return normalized.toStdString();
}

bool IsWhisperLanguageDetectionLog(const char* text)
{
    return text && std::strstr(text, "auto-detected language:") != nullptr;
}

void WhisperLogFilter(enum ggml_log_level, const char* text, void*)
{
    if (!text || IsWhisperLanguageDetectionLog(text))
        return;

    std::fputs(text, stderr);
}

void EnsureWhisperLogConfigured()
{
    static std::once_flag configured;
    std::call_once(configured, []() {
        whisper_log_set(WhisperLogFilter, nullptr);
    });
}

std::shared_ptr<SharedVadModel> CreateVadModelBundle(const std::wstring& modelPath)
{
    if (!EnsureVadOrtApiInitialized())
    {
        Logger::Instance().Log(
            LogLevel::Error,
            "vad",
            "ort.api_missing",
            "Failed to initialize ONNX Runtime API for VAD",
            { { "api_version", std::to_string(kVadOrtApiVersion) } });
        return nullptr;
    }

    auto model = std::make_shared<SharedVadModel>();
    model->ortEnv = std::make_shared<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "SileroVAD");

#ifdef _WIN32
    const auto* ortModelPath = modelPath.c_str();
#else
    const std::string modelPathUtf8 = Narrow(modelPath);
    const auto* ortModelPath = modelPathUtf8.c_str();
#endif

    bool cudaOk = false;
    try
    {
        Ort::SessionOptions cudaOptions;
        cudaOptions.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        cudaOptions.SetIntraOpNumThreads(1);

        OrtCUDAProviderOptionsV2* cudaOpts = nullptr;
        Ort::GetApi().CreateCUDAProviderOptions(&cudaOpts);
        std::vector<const char*> keys = { "device_id" };
        std::vector<const char*> values = { "0" };
        Ort::GetApi().UpdateCUDAProviderOptions(cudaOpts, keys.data(), values.data(), keys.size());
        cudaOptions.AppendExecutionProvider_CUDA_V2(*cudaOpts);
        Ort::GetApi().ReleaseCUDAProviderOptions(cudaOpts);

        model->ortSession = std::make_shared<Ort::Session>(*model->ortEnv, ortModelPath, cudaOptions);

        Ort::MemoryInfo memInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        std::vector<float> dummyInput(512, 0.0f);
        std::vector<float> dummyState(2 * 1 * 128, 0.0f);
        int64_t sampleRateValue = 16000;
        const int64_t inputShape[2] = { 1, static_cast<int64_t>(dummyInput.size()) };
        const int64_t stateShape[3] = { 2, 1, 128 };

        Ort::Value inputTensor = Ort::Value::CreateTensor<float>(
            memInfo, dummyInput.data(), dummyInput.size(), inputShape, 2);
        Ort::Value stateTensor = Ort::Value::CreateTensor<float>(
            memInfo, dummyState.data(), dummyState.size(), stateShape, 3);
        Ort::Value srTensor = Ort::Value::CreateTensor<int64_t>(
            memInfo, &sampleRateValue, 1, nullptr, 0);

        Ort::AllocatorWithDefaultOptions allocator;
        auto inputName0 = model->ortSession->GetInputNameAllocated(0, allocator);
        auto inputName1 = model->ortSession->GetInputNameAllocated(1, allocator);
        auto inputName2 = model->ortSession->GetInputNameAllocated(2, allocator);
        auto outputName0 = model->ortSession->GetOutputNameAllocated(0, allocator);
        auto outputName1 = model->ortSession->GetOutputNameAllocated(1, allocator);

        const char* inputNames[] = { inputName0.get(), inputName1.get(), inputName2.get() };
        const char* outputNames[] = { outputName0.get(), outputName1.get() };
        Ort::Value inputValues[] = { std::move(inputTensor), std::move(stateTensor), std::move(srTensor) };
        model->ortSession->Run(Ort::RunOptions{ nullptr }, inputNames, inputValues, 3, outputNames, 2);

        cudaOk = true;
        model->usingGpu = true;
        Logger::Instance().Log(
            LogLevel::Info,
            "vad",
            "model.cuda_warmup_ok",
            "Silero VAD CUDA EP warmup succeeded",
            { { "model_path", Narrow(modelPath) } });
    }
    catch (const Ort::Exception& ex)
    {
        Logger::Instance().Log(
            LogLevel::Warning,
            "vad",
            "model.cuda_fallback",
            "Silero VAD CUDA EP failed, falling back to CPU",
            {
                { "model_path", Narrow(modelPath) },
                { "error", ex.what() },
            });
        model->ortSession.reset();
        model->usingGpu = false;
    }

    if (!cudaOk)
    {
        try
        {
            Ort::SessionOptions cpuOptions;
            cpuOptions.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
            cpuOptions.SetIntraOpNumThreads(1);
            model->ortSession = std::make_shared<Ort::Session>(*model->ortEnv, ortModelPath, cpuOptions);
            Logger::Instance().Log(
                LogLevel::Info,
                "vad",
                "model.cpu_backend",
                "Silero VAD is using CPU inference backend",
                { { "model_path", Narrow(modelPath) } });
        }
        catch (const Ort::Exception& ex)
        {
            Logger::Instance().Log(
                LogLevel::Error,
                "vad",
                "model.create_session_fail",
                "Failed to create Silero VAD session",
                {
                    { "model_path", Narrow(modelPath) },
                    { "error", ex.what() },
                });
            return nullptr;
        }
    }

    Ort::AllocatorWithDefaultOptions allocator;
    auto readName = [&](size_t index, bool isInput, std::string& dst) -> bool {
        try
        {
            Ort::AllocatedStringPtr rawName = isInput
                ? model->ortSession->GetInputNameAllocated(index, allocator)
                : model->ortSession->GetOutputNameAllocated(index, allocator);
            dst = rawName ? rawName.get() : "";
            return true;
        }
        catch (const Ort::Exception& ex)
        {
            Logger::Instance().Log(
                LogLevel::Error,
                "vad",
                "model.name_fail",
                "Failed to resolve Silero VAD tensor name",
                {
                    { "model_path", Narrow(modelPath) },
                    { "index", std::to_string(index) },
                    { "kind", isInput ? "input" : "output" },
                    { "error", ex.what() },
                });
            return false;
        }
    };

    if (!readName(0, true, model->inputName) ||
        !readName(1, true, model->stateName) ||
        !readName(2, true, model->srName) ||
        !readName(0, false, model->outputName) ||
        !readName(1, false, model->stateOutName))
    {
        return nullptr;
    }

    Logger::Instance().Log(
        LogLevel::Info,
        "vad",
        "model.shared_loaded",
        "Silero VAD shared model loaded",
        {
            { "model_path", Narrow(modelPath) },
            { "backend", model->usingGpu ? "GPU" : "CPU" },
        });
    return model;
}

std::shared_ptr<WhisperSharedModel> CreateWhisperModelBundle(const std::string& modelPath)
{
    EnsureWhisperLogConfigured();

    auto model = std::make_shared<WhisperSharedModel>();
    model->modelPath = modelPath;

    whisper_context_params cparams = whisper_context_default_params();
    cparams.use_gpu = true;

    model->context = whisper_init_from_file_with_params_no_state(modelPath.c_str(), cparams);
    if (!model->context)
    {
        Logger::Instance().Log(
            LogLevel::Warning,
            "whisper",
            "model.cuda_fallback",
            "Whisper GPU load failed, falling back to CPU",
            { { "model_path", modelPath } });
        cparams.use_gpu = false;
        model->context = whisper_init_from_file_with_params_no_state(modelPath.c_str(), cparams);
    }

    if (!model->context)
    {
        Logger::Instance().Log(
            LogLevel::Error,
            "whisper",
            "model.load_fail",
            "Failed to load Whisper model",
            { { "model_path", modelPath } });
        return nullptr;
    }

    model->usingGpu = cparams.use_gpu;
    Logger::Instance().Log(
        LogLevel::Info,
        "whisper",
        "model.shared_loaded",
        "Whisper shared model loaded",
        {
            { "model_path", modelPath },
            { "backend", cparams.use_gpu ? "GPU" : "CPU" },
        });
    return model;
}

std::shared_ptr<DetectorSharedModel> CreateDetectorModelBundle(const std::string& modelPath)
{
    if (!EnsureOrtApiInitialized())
    {
        Logger::Instance().Log(
            LogLevel::Error,
            "detector",
            "model.ort_init_fail",
            "Failed to initialize ONNX Runtime API",
            { { "api_version", std::to_string(kOrtApiVersion) } });
        return nullptr;
    }

    auto model = std::make_shared<DetectorSharedModel>();
    model->modelPath = modelPath;
    model->ortEnv = std::make_shared<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "FlexibleDetector");

#ifdef _WIN32
    const std::wstring wpath = Utf8ToWide(modelPath);
#endif

    bool cudaOk = false;
    try
    {
        Ort::SessionOptions cudaOptions;

        cudaOptions.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_BASIC);

        OrtCUDAProviderOptionsV2* cudaOpts = nullptr;
        Ort::GetApi().CreateCUDAProviderOptions(&cudaOpts);
        std::vector<const char*> keys = { "device_id" };
        std::vector<const char*> values = { "0" };
        Ort::GetApi().UpdateCUDAProviderOptions(cudaOpts, keys.data(), values.data(), keys.size());
        cudaOptions.AppendExecutionProvider_CUDA_V2(*cudaOpts);
        Ort::GetApi().ReleaseCUDAProviderOptions(cudaOpts);

#ifdef _WIN32
        model->ortSession = std::make_shared<Ort::Session>(*model->ortEnv, wpath.c_str(), cudaOptions);
#else
        model->ortSession = std::make_shared<Ort::Session>(*model->ortEnv, modelPath.c_str(), cudaOptions);
#endif

        const auto inputShape = model->ortSession->GetInputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
        if (inputShape.size() != 4 || inputShape[1] != 3)
            throw std::runtime_error("Only NCHW detection models with 3-channel input are supported");
        if (inputShape[2] > 0)
            model->inputHeight = static_cast<int>(inputShape[2]);
        if (inputShape[3] > 0)
            model->inputWidth = static_cast<int>(inputShape[3]);
        model->canUseGpuPreprocess =
            model->inputWidth == kGpuFixedInputSize && model->inputHeight == kGpuFixedInputSize;

        std::vector<float> dummyInput(3ull * model->inputWidth * model->inputHeight, 0.0f);
        Ort::MemoryInfo memInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        std::vector<int64_t> warmupShape = { 1, 3, model->inputHeight, model->inputWidth };
        Ort::Value inputTensor = Ort::Value::CreateTensor<float>(
            memInfo, dummyInput.data(), dummyInput.size(), warmupShape.data(), warmupShape.size());

        Ort::AllocatorWithDefaultOptions allocator;
        auto inputName = model->ortSession->GetInputNameAllocated(0, allocator);
        const size_t numOutputs = model->ortSession->GetOutputCount();
        std::vector<Ort::AllocatedStringPtr> outNamePtrs;
        std::vector<const char*> outNames;
        for (size_t i = 0; i < numOutputs; ++i)
        {
            outNamePtrs.push_back(model->ortSession->GetOutputNameAllocated(i, allocator));
            outNames.push_back(outNamePtrs.back().get());
        }
        const char* inNames[] = { inputName.get() };
        model->ortSession->Run(Ort::RunOptions{ nullptr }, inNames, &inputTensor, 1, outNames.data(), numOutputs);

        cudaOk = true;
        model->inferenceBackend = DetectorBackend::Gpu;
        Logger::Instance().Log(
            LogLevel::Info,
            "detector",
            "model.cuda_warmup_ok",
            "Detector CUDA EP warmup succeeded",
            { { "model_path", modelPath } });
    }
    catch (const Ort::Exception& ex)
    {
        Logger::Instance().Log(
            LogLevel::Warning,
            "detector",
            "model.cuda_fallback",
            "Detector CUDA EP failed, falling back to CPU",
            {
                { "model_path", modelPath },
                { "error", ex.what() },
            });
        model->ortSession.reset();
    }

    if (!cudaOk)
    {
        Ort::SessionOptions cpuOptions;
        cpuOptions.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        cpuOptions.SetIntraOpNumThreads(4);

#ifdef _WIN32
        model->ortSession = std::make_shared<Ort::Session>(*model->ortEnv, wpath.c_str(), cpuOptions);
#else
        model->ortSession = std::make_shared<Ort::Session>(*model->ortEnv, modelPath.c_str(), cpuOptions);
#endif

        const auto inputShape = model->ortSession->GetInputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
        if (inputShape.size() != 4 || inputShape[1] != 3)
            throw std::runtime_error("Only NCHW detection models with 3-channel input are supported");
        if (inputShape[2] > 0)
            model->inputHeight = static_cast<int>(inputShape[2]);
        if (inputShape[3] > 0)
            model->inputWidth = static_cast<int>(inputShape[3]);
        model->canUseGpuPreprocess =
            model->inputWidth == kGpuFixedInputSize && model->inputHeight == kGpuFixedInputSize;
        model->inferenceBackend = DetectorBackend::Cpu;
        Logger::Instance().Log(
            LogLevel::Info,
            "detector",
            "model.cpu_backend",
            "Detector is using CPU inference backend",
            { { "model_path", modelPath } });
    }

    const size_t numOutputs = model->ortSession->GetOutputCount();
    {
        Ort::AllocatorWithDefaultOptions allocator;
        auto inputName = model->ortSession->GetInputNameAllocated(0, allocator);
        model->ortInputName = inputName ? inputName.get() : "";
        model->ortOutputNames.reserve(numOutputs);
        for (size_t i = 0; i < numOutputs; ++i)
        {
            auto outputName = model->ortSession->GetOutputNameAllocated(i, allocator);
            model->ortOutputNames.emplace_back(outputName ? outputName.get() : "");
        }
        model->ortOutputNameViews.reserve(numOutputs);
        for (const std::string& name : model->ortOutputNames)
            model->ortOutputNameViews.push_back(name.c_str());
    }

    model->modelFamily = GuessFamilyFromPath(modelPath);
    if (model->modelFamily == DetectorModelFamily::Unknown)
    {
        if (numOutputs == 1)
        {
            const auto shape = model->ortSession->GetOutputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
            if (shape.size() == 3)
            {
                const int64_t dim1 = std::max<int64_t>(1, shape[1]);
                const int64_t dim2 = std::max<int64_t>(1, shape[2]);
                model->modelFamily = (std::max(dim1, dim2) > 1000)
                    ? DetectorModelFamily::Yolo
                    : DetectorModelFamily::RtDetr;
            }
        }
        else if (numOutputs >= 2)
        {
            model->modelFamily = DetectorModelFamily::RtDetr;
        }
    }

    if (model->modelFamily == DetectorModelFamily::Unknown)
        model->modelFamily = DetectorModelFamily::RtDetr;

    if (numOutputs == 1)
    {
        const auto shape = model->ortSession->GetOutputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
        if (shape.size() == 3)
        {
            int attrs = static_cast<int>(std::min(std::max<int64_t>(1, shape[1]), std::max<int64_t>(1, shape[2])));
            if (model->modelFamily == DetectorModelFamily::RtDetr)
            {
                model->decodedClassCount = std::max(1, attrs - 4);
            }
            else
            {
                const int64_t dim1 = shape[1];
                const int64_t dim2 = shape[2];
                if (dim1 > 4 && dim1 <= 512 && (dim2 <= 0 || dim2 > dim1))
                    attrs = static_cast<int>(dim1);
                else if (dim2 > 4 && dim2 <= 512 && (dim1 <= 0 || dim1 > dim2))
                    attrs = static_cast<int>(dim2);

                const std::string loweredModelPath = ToLowerCopy(modelPath);
                model->yoloUsesObjectness =
                    loweredModelPath.find("yolov5") != std::string::npos ||
                    loweredModelPath.find("yolov6") != std::string::npos ||
                    loweredModelPath.find("yolov7") != std::string::npos ||
                    loweredModelPath.find("yolox") != std::string::npos ||
                    attrs == 85;
                model->decodedClassCount = std::max(1, attrs - (model->yoloUsesObjectness ? 5 : 4));
            }
        }
    }

    Logger::Instance().Log(
        LogLevel::Info,
        "detector",
        "model.shared_loaded",
        "Detector shared model loaded",
        {
            { "model_path", modelPath },
            { "family", model->modelFamily == DetectorModelFamily::Yolo ? "yolo" : "rtdetr" },
            { "input", std::to_string(model->inputWidth) + "x" + std::to_string(model->inputHeight) },
            { "gpu_preprocess_capable", model->canUseGpuPreprocess ? "true" : "false" },
            { "backend", BackendToCString(model->inferenceBackend) },
        });
    return model;
}
}

SharedVadModel::~SharedVadModel() = default;

SharedVadRuntime& SharedVadRuntime::Instance()
{
    static SharedVadRuntime runtime;
    return runtime;
}

std::shared_ptr<const SharedVadModel> SharedVadRuntime::AcquireModel(const std::wstring& modelPath)
{
    const std::string cacheKey = NormalizeVadModelKey(modelPath);
    std::lock_guard<std::mutex> lock(cacheMutex_);

    const auto it = modelCache_.find(cacheKey);
    if (it != modelCache_.end())
    {
        if (std::shared_ptr<const SharedVadModel> cached = it->second.lock())
        {
            Logger::Instance().Log(
                LogLevel::Info,
                "vad",
                "model.shared_cache_hit",
                "Silero VAD shared model cache hit",
                { { "model_path", Narrow(modelPath) } });
            return cached;
        }
    }

    std::shared_ptr<const SharedVadModel> model = CreateVadModelBundle(modelPath);
    if (model)
        modelCache_[cacheKey] = model;
    return model;
}

WhisperSharedModel::~WhisperSharedModel()
{
    if (context)
    {
        whisper_free(context);
        context = nullptr;
    }
}

SharedWhisperRuntime& SharedWhisperRuntime::Instance()
{
    static SharedWhisperRuntime runtime;
    return runtime;
}

std::shared_ptr<const WhisperSharedModel> SharedWhisperRuntime::AcquireModel(const std::string& modelPath)
{
    const std::string cacheKey = NormalizeModelKey(modelPath);
    std::lock_guard<std::mutex> lock(cacheMutex_);

    const auto it = modelCache_.find(cacheKey);
    if (it != modelCache_.end())
    {
        if (std::shared_ptr<const WhisperSharedModel> cached = it->second.lock())
        {
            Logger::Instance().Log(
                LogLevel::Info,
                "whisper",
                "model.shared_cache_hit",
                "Whisper shared model cache hit",
                { { "model_path", modelPath } });
            return cached;
        }
    }

    std::shared_ptr<const WhisperSharedModel> model = CreateWhisperModelBundle(modelPath);
    if (model)
        modelCache_[cacheKey] = model;
    return model;
}

SharedDetectorRuntime& SharedDetectorRuntime::Instance()
{
    static SharedDetectorRuntime runtime;
    return runtime;
}

std::shared_ptr<const DetectorSharedModel> SharedDetectorRuntime::AcquireModel(const std::string& modelPath)
{
    const std::string cacheKey = NormalizeModelKey(modelPath);
    std::lock_guard<std::mutex> lock(cacheMutex_);

    const auto it = modelCache_.find(cacheKey);
    if (it != modelCache_.end())
    {
        if (std::shared_ptr<const DetectorSharedModel> cached = it->second.lock())
        {
            Logger::Instance().Log(
                LogLevel::Info,
                "detector",
                "model.shared_cache_hit",
                "Detector shared model cache hit",
                { { "model_path", modelPath } });
            return cached;
        }
    }

    std::shared_ptr<const DetectorSharedModel> model = CreateDetectorModelBundle(modelPath);
    if (model)
        modelCache_[cacheKey] = model;
    return model;
}
