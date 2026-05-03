#include "detector_pipeline.h"

#include "cuda_preprocess.h"
#include "detector_common.h"
#include "../../common/diagnostics/logger.h"
#include "../../core/ai/inference_scheduler.h"
#include "../../core/ai/shared_ai_runtimes.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QTextStream>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <thread>

#include <cuda_runtime.h>
#include <nppi_color_conversion.h>
#include <nppi_geometry_transforms.h>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_cuda.h>
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>
}

DetectorResources::DetectorResources() = default;

DetectorResources::~DetectorResources()
{
    if (preprocessStream)
        cudaStreamSynchronize(preprocessStream);

    if (d_rgbFull)    { cudaFree(d_rgbFull);    d_rgbFull = nullptr; }
    if (d_rgbResized) { cudaFree(d_rgbResized); d_rgbResized = nullptr; }
    if (d_blob[0])    { cudaFree(d_blob[0]);    d_blob[0] = nullptr; }
    if (d_blob[1])    { cudaFree(d_blob[1]);    d_blob[1] = nullptr; }
    if (d_nv12Temp)   { cudaFree(d_nv12Temp);   d_nv12Temp = nullptr; }
    if (preprocessEvent)  { cudaEventDestroy(preprocessEvent);  preprocessEvent = nullptr; }
    if (preprocessStream) { cudaStreamDestroy(preprocessStream); preprocessStream = nullptr; }
    if (cachedSwsCtx)     { sws_freeContext(cachedSwsCtx); cachedSwsCtx = nullptr; }
}

namespace
{
int DetectorPriorityScore(const DetectorThreadState& state)
{
    const AiPriorityTier tier = static_cast<AiPriorityTier>(state.priorityTier.load());
    switch (tier)
    {
    case AiPriorityTier::Fullscreen: return 160;
    case AiPriorityTier::Alarm:      return 145;
    case AiPriorityTier::Focused:    return 125;
    case AiPriorityTier::Candidate:  return 90;
    case AiPriorityTier::Background: return 45;
    }
    return 90;
}

InferenceTaskProfile BuildDetectorTaskProfile(const DetectorThreadState& state, const DetectorModelSession& modelSession)
{
    InferenceTaskProfile profile;
    profile.capability = AiCapability::Detector;
    profile.lane = modelSession.IsUsingGpuInference() ? InferenceLane::Gpu : InferenceLane::Cpu;
    profile.priority = DetectorPriorityScore(state) + (state.liveMode.load() ? 10 : 0);
    const AiPriorityTier tier = static_cast<AiPriorityTier>(state.priorityTier.load());
    const bool backgroundLane = tier == AiPriorityTier::Background || tier == AiPriorityTier::Candidate;
    profile.dropIfLate = state.liveMode.load() || backgroundLane;
    if (tier == AiPriorityTier::Fullscreen)
        profile.maxQueueDelayMs = state.lowLatencyMode.load() ? 30 : 60;
    else if (tier == AiPriorityTier::Alarm)
        profile.maxQueueDelayMs = state.lowLatencyMode.load() ? 40 : 90;
    else if (tier == AiPriorityTier::Focused)
        profile.maxQueueDelayMs = state.lowLatencyMode.load() ? 55 : 120;
    else
        profile.maxQueueDelayMs = state.lowLatencyMode.load() ? 35 : 80;
    profile.waitPollMs = state.lowLatencyMode.load() ? 4 : (backgroundLane ? 10 : 6);
    return profile;
}
}

DetectorModelSession::DetectorModelSession(DetectorThreadState& state, DetectorResources& resources)
    : state_(state)
    , resources_(resources)
{
}

bool DetectorModelSession::LoadModel(const std::string& modelPath, const std::string& labelsPath)
{
    try
    {
        state_.modelLoaded.store(false);
        resources_.sharedModel.reset();
        resources_.classNames.clear();
        state_.modelFamily = DetectorModelFamily::Unknown;
        state_.inputWidth = kDefaultInputSize;
        state_.inputHeight = kDefaultInputSize;
        state_.canUseGpuPreprocess.store(true);
        state_.yoloUsesObjectness = false;
        state_.decodedClassCount = 0;
        state_.inferenceBackend.store(DetectorBackend::Unknown);
        state_.preprocessBackend.store(DetectorBackend::Unknown);
        state_.postprocessBackend.store(DetectorBackend::Cpu);
        state_.trackerBackend.store(DetectorBackend::Cpu);
        state_.loggedModelInfo = false;
        state_.loggedFirstResult = false;
        state_.loggedRawDet = false;
        state_.loggedGpuPreprocessPath = false;
        state_.loggedCpuPreprocessPath = false;

        resources_.sharedModel = SharedDetectorRuntime::Instance().AcquireModel(modelPath);
        if (!resources_.sharedModel || !resources_.sharedModel->ortSession)
            return false;

        state_.modelFamily = resources_.sharedModel->modelFamily;
        state_.inputWidth = resources_.sharedModel->inputWidth;
        state_.inputHeight = resources_.sharedModel->inputHeight;
        state_.canUseGpuPreprocess.store(resources_.sharedModel->canUseGpuPreprocess);
        state_.yoloUsesObjectness = resources_.sharedModel->yoloUsesObjectness;
        state_.decodedClassCount = resources_.sharedModel->decodedClassCount;
        state_.inferenceBackend.store(resources_.sharedModel->inferenceBackend);

        LoadClassNames(modelPath, labelsPath);

        state_.modelLoaded.store(true);
        const int recommendedSkip = state_.inferenceBackend.load() == DetectorBackend::Gpu ? 3 : 4;
        state_.baseSkipFrames.store(recommendedSkip);
        state_.minimumSkipFrames.store(std::max(state_.minimumSkipFrames.load(), recommendedSkip));
        Logger::Instance().Log(
            LogLevel::Info,
            "detector",
            "model.loaded",
            "Detector model loaded",
            {
                { "model_path", modelPath },
                { "family", state_.modelFamily == DetectorModelFamily::Yolo ? "yolo" : "rtdetr" },
                { "input", std::to_string(state_.inputWidth) + "x" + std::to_string(state_.inputHeight) },
                { "labels", std::to_string(resources_.classNames.size()) },
                { "gpu_preprocess_capable", state_.canUseGpuPreprocess.load() ? "true" : "false" },
                { "skip_frames", std::to_string(GetActiveSkipFrames()) },
                { "shared_model", "true" },
                { "pipeline", GetPipelineSummary().toStdString() },
            });
        return true;
    }
    catch (const std::exception& ex)
    {
        Logger::Instance().Log(
            LogLevel::Error,
            "detector",
            "model.load_exception",
            "Detector model load failed",
            { { "error", ex.what() } });
        return false;
    }
}

void DetectorModelSession::SetModelPath(const std::string& path)
{
    SetModelConfig(path, "");
}

void DetectorModelSession::SetModelConfig(const std::string& modelPath, const std::string& labelsPath)
{
    state_.pendingModelPath = modelPath;
    state_.pendingLabelsPath = labelsPath;
}

bool DetectorModelSession::IsModelLoaded() const
{
    return state_.modelLoaded.load();
}

bool DetectorModelSession::IsUsingGpuInference() const
{
    return state_.inferenceBackend.load() == DetectorBackend::Gpu;
}

bool DetectorModelSession::HasGpuPreprocessCapability() const
{
    return state_.canUseGpuPreprocess.load();
}

QString DetectorModelSession::GetBackendSummary() const
{
    return BackendToQString(state_.inferenceBackend.load());
}

QString DetectorModelSession::GetPipelineSummary() const
{
    const DetectorBackend prepBackend = state_.preprocessBackend.load();
    const QString prepText = prepBackend != DetectorBackend::Unknown
        ? BackendToQString(prepBackend)
        : (state_.canUseGpuPreprocess.load() ? "GPU-capable" : "CPU");
    return QString("infer=%1 | prep=%2 | post=%3 | track=%4 | skip=%5")
        .arg(BackendToQString(state_.inferenceBackend.load()))
        .arg(prepText)
        .arg(BackendToQString(state_.postprocessBackend.load()))
        .arg(BackendToQString(state_.trackerBackend.load()))
        .arg(GetActiveSkipFrames());
}

void DetectorModelSession::SetMinimumSkipFrames(int skip)
{
    state_.minimumSkipFrames.store(std::max(0, skip));
}

int DetectorModelSession::GetActiveSkipFrames() const
{
    return std::max(state_.baseSkipFrames.load(), state_.minimumSkipFrames.load());
}

int DetectorModelSession::GetBaseSkipFrames() const
{
    return state_.baseSkipFrames.load();
}

bool DetectorModelSession::SupportsGpuPreprocess() const
{
    return state_.canUseGpuPreprocess.load() &&
        state_.inputWidth == kGpuFixedInputSize &&
        state_.inputHeight == kGpuFixedInputSize;
}

void DetectorModelSession::LoadClassNames(const std::string& modelPath, const std::string& labelsPath)
{
    resources_.classNames.clear();

    QStringList candidates;
    if (!labelsPath.empty())
        candidates << QDir::cleanPath(QString::fromUtf8(labelsPath.c_str()));

    const QFileInfo modelInfo(QString::fromUtf8(modelPath.c_str()));
    if (modelInfo.exists())
    {
        const QString baseName = modelInfo.completeBaseName();
        const QDir dir = modelInfo.dir();
        candidates
            << dir.filePath(baseName + ".labels.txt")
            << dir.filePath(baseName + ".txt")
            << dir.filePath("labels.txt")
            << dir.filePath("classes.txt");

        const QString appDir = QCoreApplication::applicationDirPath();
        const QStringList labelRoots = {
            QDir(appDir).filePath("labels"),
            QDir(appDir).filePath("../common/labels"),
            QDir(QDir::currentPath()).filePath("labels"),
            QDir(QDir::currentPath()).filePath("../common/labels"),
            QDir(appDir).filePath("../../../../bin/labels"),
            QDir(appDir).filePath("../../../../bin/common/labels"),
            QDir(appDir).filePath("../../../bin/labels"),
            QDir(appDir).filePath("../../../bin/common/labels"),
            QDir(appDir).filePath("../../bin/labels"),
            QDir(appDir).filePath("../../bin/common/labels"),
            QDir(appDir).filePath("../bin/labels")
        };
        for (const QString& root : labelRoots)
        {
            const QDir labelDir(QDir::cleanPath(root));
            candidates
                << labelDir.filePath(baseName + ".labels.txt")
                << labelDir.filePath(baseName + ".txt")
                << labelDir.filePath("coco80.txt")
                << labelDir.filePath("labels.txt")
                << labelDir.filePath("classes.txt");
        }
    }

    candidates.removeDuplicates();

    for (const QString& candidate : candidates)
    {
        QFile file(candidate);
        if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;

        QTextStream in(&file);
        while (!in.atEnd())
        {
            const QString line = in.readLine().trimmed();
            if (line.isEmpty() || line.startsWith('#'))
                continue;
            resources_.classNames.push_back(line);
        }

        if (!resources_.classNames.empty())
        {
            Logger::Instance().Log(
                LogLevel::Info,
                "detector",
                "labels.loaded",
                "Detector labels loaded",
                {
                    { "path", candidate.toStdString() },
                    { "count", std::to_string(resources_.classNames.size()) },
                });
            break;
        }
    }

    if (resources_.classNames.empty() && state_.decodedClassCount == 80)
        resources_.classNames = DefaultCocoNames();

    if (!resources_.classNames.empty() && state_.decodedClassCount > 0 &&
        static_cast<int>(resources_.classNames.size()) != state_.decodedClassCount)
    {
        Logger::Instance().Log(
            LogLevel::Warning,
            "detector",
            "labels.count_mismatch",
            "Detector label count does not match decoded class count",
            {
                { "labels", std::to_string(resources_.classNames.size()) },
                { "decoded", std::to_string(state_.decodedClassCount) },
            });
    }
}

DetectorPreprocessPipeline::DetectorPreprocessPipeline(
    DetectorThreadState& state,
    DetectorResources& resources,
    const DetectorModelSession& modelSession)
    : state_(state)
    , resources_(resources)
    , modelSession_(modelSession)
{
}

DetectorPreprocessPipeline::~DetectorPreprocessPipeline()
{
    FreeGpuBuffers();
}

void DetectorPreprocessPipeline::PushFrame(AVFrame* frame)
{
    if (!frame || !state_.modelLoaded.load())
        return;

    const int frameIndex = state_.frameCounter.fetch_add(1) + 1;
    const int skipFrames = modelSession_.GetActiveSkipFrames();
    if (frameIndex % (skipFrames + 1) != 0)
        return;

    if (frame->format == AV_PIX_FMT_CUDA && modelSession_.SupportsGpuPreprocess())
    {
        state_.preprocessBackend.store(DetectorBackend::Gpu);
        if (!state_.loggedGpuPreprocessPath)
        {
            std::cout << "[Detector] Preprocess path: GPU (CUDA/NPP)" << std::endl;
            state_.loggedGpuPreprocessPath = true;
        }

        if (!frame->hw_frames_ctx)
            return;
        AVHWFramesContext* framesCtx = reinterpret_cast<AVHWFramesContext*>(frame->hw_frames_ctx->data);

        const int w = frame->width;
        const int h = frame->height;
        if (w <= 0 || h <= 0)
            return;

        if (!resources_.cudaContext)
        {
            AVCUDADeviceContext* cudaDevCtx =
                reinterpret_cast<AVCUDADeviceContext*>(framesCtx->device_ctx->hwctx);
            resources_.cudaContext = cudaDevCtx->cuda_ctx;
            std::cout << "[Detector] GPU preprocess: CUcontext acquired" << std::endl;
        }

        const float scale = std::min(
            static_cast<float>(state_.inputWidth) / w,
            static_cast<float>(state_.inputHeight) / h);
        const int newW = static_cast<int>(w * scale);
        const int newH = static_cast<int>(h * scale);
        const int padX = (state_.inputWidth - newW) / 2;
        const int padY = (state_.inputHeight - newH) / 2;

        AllocateGpuBuffers(w, h, newW, newH);

        static cudaDeviceProp cachedProp;
        static bool propCached = false;
        if (!propCached)
        {
            cudaGetDeviceProperties(&cachedProp, 0);
            propCached = true;
        }
        NppStreamContext nppCtx = {};
        nppCtx.hStream = resources_.preprocessStream;
        nppCtx.nCudaDeviceId = 0;
        nppCtx.nMultiProcessorCount = cachedProp.multiProcessorCount;
        nppCtx.nMaxThreadsPerMultiProcessor = cachedProp.maxThreadsPerMultiProcessor;
        nppCtx.nMaxThreadsPerBlock = cachedProp.maxThreadsPerBlock;
        nppCtx.nSharedMemPerBlock = cachedProp.sharedMemPerBlock;
        nppCtx.nCudaDevAttrComputeCapabilityMajor = cachedProp.major;
        nppCtx.nCudaDevAttrComputeCapabilityMinor = cachedProp.minor;
        cudaStreamGetFlags(resources_.preprocessStream, &nppCtx.nStreamFlags);

        const bool isP010 = (framesCtx->sw_format == AV_PIX_FMT_P010LE
            || framesCtx->sw_format == AV_PIX_FMT_P010BE);

        const Npp8u* nv12Y;
        const Npp8u* nv12UV;
        int nv12Step;

        if (isP010)
        {
            uint8_t* tmpY = resources_.d_nv12Temp;
            uint8_t* tmpUV = resources_.d_nv12Temp + resources_.d_nv12TempPitch * h;

            launchP010ToNV12(
                reinterpret_cast<const uint16_t*>(frame->data[0]), frame->linesize[0],
                reinterpret_cast<const uint16_t*>(frame->data[1]), frame->linesize[1],
                tmpY, resources_.d_nv12TempPitch,
                tmpUV, resources_.d_nv12TempPitch,
                w, h, resources_.preprocessStream);

            nv12Y = tmpY;
            nv12UV = tmpUV;
            nv12Step = resources_.d_nv12TempPitch;
        }
        else
        {
            nv12Y = reinterpret_cast<const Npp8u*>(frame->data[0]);
            nv12UV = reinterpret_cast<const Npp8u*>(frame->data[1]);
            nv12Step = frame->linesize[0];
        }

        const Npp8u* pSrc[2] = { nv12Y, nv12UV };
        const NppiSize srcSize = { w, h };
        NppStatus nppSt = nppiNV12ToRGB_8u_P2C3R_Ctx(
            pSrc, nv12Step, resources_.d_rgbFull, resources_.d_rgbFullPitch, srcSize, nppCtx);
        if (nppSt != NPP_SUCCESS)
        {
            std::cerr << "[Detector] nppiNV12ToRGB failed: " << nppSt << std::endl;
            return;
        }

        const NppiSize fullSize = { w, h };
        const NppiRect fullROI = { 0, 0, w, h };
        const NppiSize resizedSize = { newW, newH };
        const NppiRect resizedROI = { 0, 0, newW, newH };
        nppSt = nppiResize_8u_C3R_Ctx(
            resources_.d_rgbFull, resources_.d_rgbFullPitch, fullSize, fullROI,
            resources_.d_rgbResized, resources_.d_rgbResizedPitch, resizedSize, resizedROI,
            NPPI_INTER_LINEAR, nppCtx);
        if (nppSt != NPP_SUCCESS)
        {
            std::cerr << "[Detector] nppiResize failed: " << nppSt << std::endl;
            return;
        }

        const int writeIdx = resources_.blobWriteIdx;
        launchLetterboxNormalizeCHW(
            resources_.d_rgbResized, resources_.d_rgbResizedPitch,
            newW, newH,
            resources_.d_blob[writeIdx], 3 * state_.inputWidth * state_.inputHeight,
            padX, padY, scale, resources_.preprocessStream);

        cudaEventRecord(resources_.preprocessEvent, resources_.preprocessStream);
        cudaEventSynchronize(resources_.preprocessEvent);

        {
            std::lock_guard<std::mutex> lock(resources_.frameMux);
            const quint64 frameToken = state_.pendingFrameToken.fetch_add(1) + 1;
            resources_.blobReadIdx = writeIdx;
            resources_.blobWriteIdx = 1 - writeIdx;
            state_.gpuPathActive = true;
            state_.origWidth = w;
            state_.origHeight = h;
            state_.pendingFrameGeneration = state_.mediaGeneration.load();
            state_.pendingFrameSerial = state_.mediaSerial.load();
            state_.pendingFrameToken.store(frameToken);
            state_.hasNewFrame = true;
        }
        resources_.frameCV.notify_one();
        return;
    }

    state_.preprocessBackend.store(DetectorBackend::Cpu);
    if (!state_.loggedCpuPreprocessPath)
    {
        std::cout << "[Detector] Preprocess path: CPU (sws_scale + normalize)" << std::endl;
        state_.loggedCpuPreprocessPath = true;
    }

    AVFrame* cpuFrame = frame;
    AVFrame* tmpFrame = nullptr;

    if (frame->format == AV_PIX_FMT_CUDA)
    {
        tmpFrame = av_frame_alloc();
        if (av_hwframe_transfer_data(tmpFrame, frame, 0) < 0)
        {
            av_frame_free(&tmpFrame);
            return;
        }
        cpuFrame = tmpFrame;
    }

    const int w = cpuFrame->width;
    const int h = cpuFrame->height;
    const int fmt = cpuFrame->format;
    if (w <= 0 || h <= 0)
    {
        if (tmpFrame)
            av_frame_free(&tmpFrame);
        return;
    }

    const float scale = std::min(
        static_cast<float>(state_.inputWidth) / w,
        static_cast<float>(state_.inputHeight) / h);
    const int newW = static_cast<int>(w * scale);
    const int newH = static_cast<int>(h * scale);
    const int padX = (state_.inputWidth - newW) / 2;
    const int padY = (state_.inputHeight - newH) / 2;

    if (!resources_.cachedSwsCtx || w != resources_.cachedSrcW || h != resources_.cachedSrcH
        || fmt != resources_.cachedSrcFmt || newW != resources_.cachedDstW || newH != resources_.cachedDstH)
    {
        if (resources_.cachedSwsCtx)
            sws_freeContext(resources_.cachedSwsCtx);
        resources_.cachedSwsCtx = sws_getContext(
            w, h, static_cast<AVPixelFormat>(fmt),
            newW, newH, AV_PIX_FMT_RGB24,
            SWS_BILINEAR, nullptr, nullptr, nullptr);
        resources_.cachedSrcW = w;
        resources_.cachedSrcH = h;
        resources_.cachedSrcFmt = fmt;
        resources_.cachedDstW = newW;
        resources_.cachedDstH = newH;

        if (!resources_.cachedSwsCtx)
        {
            if (tmpFrame)
                av_frame_free(&tmpFrame);
            return;
        }
    }

    const size_t resizedBytes = static_cast<size_t>(newW) * static_cast<size_t>(newH) * 3u;
    if (resources_.resizedCpuBuffer.size() != resizedBytes)
        resources_.resizedCpuBuffer.resize(resizedBytes);
    uint8_t* dstData[1] = { resources_.resizedCpuBuffer.data() };
    int dstLinesize[1] = { newW * 3 };
    sws_scale(resources_.cachedSwsCtx, cpuFrame->data, cpuFrame->linesize, 0, h, dstData, dstLinesize);

    const int fullSize = state_.inputWidth * state_.inputHeight * 3;
    if (static_cast<int>(resources_.stagingCpuRgb.size()) != fullSize)
        resources_.stagingCpuRgb.resize(fullSize);
    memset(resources_.stagingCpuRgb.data(), static_cast<int>(kLetterboxFillValue), fullSize);

    for (int y = 0; y < newH; ++y)
    {
        memcpy(resources_.stagingCpuRgb.data() + ((padY + y) * state_.inputWidth + padX) * 3,
            resources_.resizedCpuBuffer.data() + y * newW * 3,
            newW * 3);
    }

    {
        std::lock_guard<std::mutex> lock(resources_.frameMux);
        const quint64 frameToken = state_.pendingFrameToken.fetch_add(1) + 1;
        resources_.pendingCpuRgb.swap(resources_.stagingCpuRgb);
        state_.origWidth = w;
        state_.origHeight = h;
        state_.gpuPathActive = false;
        state_.pendingFrameGeneration = state_.mediaGeneration.load();
        state_.pendingFrameSerial = state_.mediaSerial.load();
        state_.pendingFrameToken.store(frameToken);
        state_.hasNewFrame = true;
    }
    resources_.frameCV.notify_one();

    if (tmpFrame)
        av_frame_free(&tmpFrame);
}

void DetectorPreprocessPipeline::Clear()
{
    std::lock_guard<std::mutex> lock(resources_.frameMux);
    state_.hasNewFrame = false;
    state_.frameCounter.store(0);
    state_.pendingFrameToken.fetch_add(1);
    state_.pendingFrameGeneration = state_.mediaGeneration.load();
    state_.pendingFrameSerial = state_.mediaSerial.load();
    resources_.pendingCpuRgb.clear();
    resources_.stagingCpuRgb.clear();
}

void DetectorPreprocessPipeline::NormalizeToBlob(const uint8_t* rgbData, std::vector<float>& blob) const
{
    const int totalPixels = state_.inputWidth * state_.inputHeight;
    blob.resize(3 * totalPixels);

    float* chR = blob.data();
    float* chG = chR + totalPixels;
    float* chB = chG + totalPixels;

    for (int i = 0; i < totalPixels; ++i)
    {
        chR[i] = rgbData[i * 3 + 0] * (1.0f / 255.0f);
        chG[i] = rgbData[i * 3 + 1] * (1.0f / 255.0f);
        chB[i] = rgbData[i * 3 + 2] * (1.0f / 255.0f);
    }
}

void DetectorPreprocessPipeline::FreeGpuBuffers()
{
    if (resources_.preprocessStream)
        cudaStreamSynchronize(resources_.preprocessStream);

    if (resources_.d_rgbFull)    { cudaFree(resources_.d_rgbFull);    resources_.d_rgbFull = nullptr; }
    if (resources_.d_rgbResized) { cudaFree(resources_.d_rgbResized); resources_.d_rgbResized = nullptr; }
    if (resources_.d_blob[0])    { cudaFree(resources_.d_blob[0]);    resources_.d_blob[0] = nullptr; }
    if (resources_.d_blob[1])    { cudaFree(resources_.d_blob[1]);    resources_.d_blob[1] = nullptr; }
    if (resources_.d_nv12Temp)   { cudaFree(resources_.d_nv12Temp);   resources_.d_nv12Temp = nullptr; }
    if (resources_.preprocessEvent)  { cudaEventDestroy(resources_.preprocessEvent);  resources_.preprocessEvent = nullptr; }
    if (resources_.preprocessStream) { cudaStreamDestroy(resources_.preprocessStream); resources_.preprocessStream = nullptr; }
    resources_.d_rgbFullPitch = 0;
    resources_.d_rgbResizedPitch = 0;
    resources_.d_nv12TempPitch = 0;
    resources_.gpuSrcW = 0;
    resources_.gpuSrcH = 0;
    resources_.gpuNewW = 0;
    resources_.gpuNewH = 0;
    resources_.cudaContext = nullptr;
}

void DetectorPreprocessPipeline::AllocateGpuBuffers(int srcW, int srcH, int newW, int newH)
{
    if (srcW == resources_.gpuSrcW && srcH == resources_.gpuSrcH &&
        newW == resources_.gpuNewW && newH == resources_.gpuNewH)
    {
        return;
    }

    if (resources_.preprocessStream)
        cudaStreamSynchronize(resources_.preprocessStream);

    if (resources_.d_rgbFull)    { cudaFree(resources_.d_rgbFull);    resources_.d_rgbFull = nullptr; }
    if (resources_.d_rgbResized) { cudaFree(resources_.d_rgbResized); resources_.d_rgbResized = nullptr; }
    if (resources_.d_nv12Temp)   { cudaFree(resources_.d_nv12Temp);   resources_.d_nv12Temp = nullptr; }

    size_t pitch = 0;

    cudaMallocPitch(&resources_.d_rgbFull, &pitch, static_cast<size_t>(srcW) * 3, srcH);
    resources_.d_rgbFullPitch = static_cast<int>(pitch);

    cudaMallocPitch(&resources_.d_rgbResized, &pitch, static_cast<size_t>(newW) * 3, newH);
    resources_.d_rgbResizedPitch = static_cast<int>(pitch);

    cudaMallocPitch(&resources_.d_nv12Temp, &pitch, static_cast<size_t>(srcW), srcH * 3 / 2);
    resources_.d_nv12TempPitch = static_cast<int>(pitch);

    if (!resources_.d_blob[0])
    {
        const size_t blobBytes = 3ull * kGpuFixedInputSize * kGpuFixedInputSize * sizeof(float);
        cudaMalloc(&resources_.d_blob[0], blobBytes);
        cudaMalloc(&resources_.d_blob[1], blobBytes);
    }

    if (!resources_.preprocessStream)
        cudaStreamCreateWithFlags(&resources_.preprocessStream, cudaStreamNonBlocking);
    if (!resources_.preprocessEvent)
        cudaEventCreateWithFlags(&resources_.preprocessEvent,
            cudaEventBlockingSync | cudaEventDisableTiming);

    resources_.gpuSrcW = srcW;
    resources_.gpuSrcH = srcH;
    resources_.gpuNewW = newW;
    resources_.gpuNewH = newH;

    std::cout << "[Detector] GPU buffers allocated: src=" << srcW << "x" << srcH
        << " resized=" << newW << "x" << newH << std::endl;
}

DetectorOutputDecoder::DetectorOutputDecoder(
    DetectorThreadState& state,
    DetectorResources& resources,
    const float& confidenceThreshold)
    : state_(state)
    , resources_(resources)
    , confidenceThreshold_(confidenceThreshold)
{
}

DetectionResult DetectorOutputDecoder::DecodeOutputs(const std::vector<Ort::Value>& outputs, int w, int h)
{
    if (outputs.empty())
    {
        DetectionResult empty;
        empty.frameWidth = w;
        empty.frameHeight = h;
        return empty;
    }

    DetectionResult result;
    if (state_.modelFamily == DetectorModelFamily::Yolo)
        result = DecodeYoloOutput(outputs.front(), w, h);
    else if (outputs.size() == 1)
        result = DecodeRtDetrOutput(outputs.front(), w, h);
    else
        result = DecodeRtDetrOutputs(outputs, w, h);

    FillClassNames(result);
    return result;
}

QString DetectorOutputDecoder::ResolveClassName(int classId) const
{
    if (classId >= 0 && classId < static_cast<int>(resources_.classNames.size()))
        return resources_.classNames[classId];
    return QString("#%1").arg(classId);
}

void DetectorOutputDecoder::FillClassNames(DetectionResult& result) const
{
    for (DetectionBox& box : result.boxes)
        box.className = ResolveClassName(box.classId);
}

std::vector<DetectionBox> DetectorOutputDecoder::ApplyNms(const std::vector<DetectionBox>& boxes, float iouThreshold) const
{
    std::vector<DetectionBox> sorted = boxes;
    std::sort(sorted.begin(), sorted.end(),
        [](const DetectionBox& a, const DetectionBox& b) {
            return a.confidence > b.confidence;
        });

    std::vector<DetectionBox> kept;
    std::vector<bool> suppressed(sorted.size(), false);
    for (size_t i = 0; i < sorted.size(); ++i)
    {
        if (suppressed[i])
            continue;
        kept.push_back(sorted[i]);
        for (size_t j = i + 1; j < sorted.size(); ++j)
        {
            if (suppressed[j] || sorted[i].classId != sorted[j].classId)
                continue;
            if (IntersectionOverUnion(sorted[i], sorted[j]) > iouThreshold)
                suppressed[j] = true;
        }
    }
    return kept;
}

void DetectorOutputDecoder::LimitBoxesByConfidence(std::vector<DetectionBox>& boxes, size_t maxBoxes) const
{
    if (boxes.size() <= maxBoxes)
        return;

    auto nth = boxes.begin() + static_cast<std::ptrdiff_t>(maxBoxes);
    std::nth_element(boxes.begin(), nth, boxes.end(),
        [](const DetectionBox& a, const DetectionBox& b) {
            return a.confidence > b.confidence;
        });
    boxes.erase(nth, boxes.end());
}

DetectionResult DetectorOutputDecoder::DecodeRtDetrOutput(const Ort::Value& output, int w, int h)
{
    DetectionResult result;
    result.frameWidth = w;
    result.frameHeight = h;

    const auto shape = output.GetTensorTypeAndShapeInfo().GetShape();
    if (shape.size() != 3)
        return result;

    const int dim1 = static_cast<int>(std::max<int64_t>(1, shape[1]));
    const int dim2 = static_cast<int>(std::max<int64_t>(1, shape[2]));
    const bool attrsFirst = dim1 < dim2;
    const int attrCount = attrsFirst ? dim1 : dim2;
    const int numDetections = attrsFirst ? dim2 : dim1;
    if (attrCount <= 4)
        return result;

    state_.decodedClassCount = std::max(state_.decodedClassCount, attrCount - 4);
    const float* data = output.GetTensorData<float>();

    for (int i = 0; i < numDetections; ++i)
    {
        const float* row = attrsFirst ? nullptr : (data + static_cast<size_t>(i) * attrCount);
        const float cx = attrsFirst ? data[0 * numDetections + i] : row[0];
        const float cy = attrsFirst ? data[1 * numDetections + i] : row[1];
        const float bw = attrsFirst ? data[2 * numDetections + i] : row[2];
        const float bh = attrsFirst ? data[3 * numDetections + i] : row[3];

        int bestClass = 0;
        float bestScore = attrsFirst ? data[4 * numDetections + i] : row[4];
        for (int c = 1; c < attrCount - 4; ++c)
        {
            const float score = attrsFirst
                ? data[static_cast<size_t>(4 + c) * numDetections + i]
                : row[4 + c];
            if (score > bestScore)
            {
                bestScore = score;
                bestClass = c;
            }
        }

        if (bestScore < confidenceThreshold_)
            continue;

        if (!state_.loggedRawDet)
        {
            std::cout << "[Detector] Raw RT-DETR det: cx=" << cx << " cy=" << cy
                << " w=" << bw << " h=" << bh
                << " score=" << bestScore << " class=" << bestClass
                << std::endl;
            state_.loggedRawDet = true;
        }

        DetectionBox box;
        box.x1 = cx - bw * 0.5f;
        box.y1 = cy - bh * 0.5f;
        box.x2 = cx + bw * 0.5f;
        box.y2 = cy + bh * 0.5f;
        box.confidence = bestScore;
        box.classId = bestClass;
        UndoLetterbox(box, state_.inputWidth, state_.inputHeight, w, h);

        if (box.x2 - box.x1 > 0.001f && box.y2 - box.y1 > 0.001f)
            result.boxes.push_back(box);
    }

    return result;
}

DetectionResult DetectorOutputDecoder::DecodeRtDetrOutputs(const std::vector<Ort::Value>& outputs, int w, int h)
{
    DetectionResult result;
    result.frameWidth = w;
    result.frameHeight = h;

    const Ort::Value* boxOut = nullptr;
    const Ort::Value* labelOut = nullptr;
    const Ort::Value* scoreOut = nullptr;

    for (const auto& output : outputs)
    {
        const auto shape = output.GetTensorTypeAndShapeInfo().GetShape();
        const auto type = output.GetTensorTypeAndShapeInfo().GetElementType();
        if (shape.size() == 3 && shape[2] >= 4)
        {
            if (!boxOut || shape[2] == 4 || shape[2] == 6)
                boxOut = &output;
        }
        else if ((shape.size() == 2 || (shape.size() == 3 && shape[2] == 1)) &&
            type == ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64)
        {
            labelOut = &output;
        }
        else if (shape.size() == 2 || (shape.size() == 3 && shape[2] == 1))
        {
            scoreOut = &output;
        }
    }

    if (!boxOut)
        return result;

    const auto boxShape = boxOut->GetTensorTypeAndShapeInfo().GetShape();
    const int numDetections = static_cast<int>(std::max<int64_t>(1, boxShape[1]));
    const int boxStride = static_cast<int>(std::max<int64_t>(1, boxShape[2]));
    const float* boxData = boxOut->GetTensorData<float>();

    for (int i = 0; i < numDetections; ++i)
    {
        float cx = 0.0f;
        float cy = 0.0f;
        float bw = 0.0f;
        float bh = 0.0f;
        float score = 1.0f;
        int classId = 0;

        if (boxStride == 4)
        {
            cx = boxData[i * 4 + 0];
            cy = boxData[i * 4 + 1];
            bw = boxData[i * 4 + 2];
            bh = boxData[i * 4 + 3];
        }
        else if (boxStride >= 6)
        {
            cx = boxData[i * boxStride + 0];
            cy = boxData[i * boxStride + 1];
            bw = boxData[i * boxStride + 2];
            bh = boxData[i * boxStride + 3];
            score = boxData[i * boxStride + 4];
            classId = static_cast<int>(boxData[i * boxStride + 5]);
        }
        else
        {
            continue;
        }

        if (labelOut)
        {
            const auto labelShape = labelOut->GetTensorTypeAndShapeInfo().GetShape();
            const auto labelType = labelOut->GetTensorTypeAndShapeInfo().GetElementType();
            const int labelStride = (labelShape.size() == 3) ? static_cast<int>(labelShape[2]) : 1;
            if (labelType == ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64)
                classId = static_cast<int>(labelOut->GetTensorData<int64_t>()[i * labelStride]);
            else if (!scoreOut)
                score = labelOut->GetTensorData<float>()[i * labelStride];
        }

        if (scoreOut)
        {
            const auto scoreShape = scoreOut->GetTensorTypeAndShapeInfo().GetShape();
            const int scoreStride = (scoreShape.size() == 3) ? static_cast<int>(scoreShape[2]) : 1;
            score = scoreOut->GetTensorData<float>()[i * scoreStride];
        }

        if (score < confidenceThreshold_)
            continue;
        if (cx <= 0.0f && cy <= 0.0f && bw <= 0.0f && bh <= 0.0f)
            continue;

        DetectionBox box;
        box.x1 = cx - bw * 0.5f;
        box.y1 = cy - bh * 0.5f;
        box.x2 = cx + bw * 0.5f;
        box.y2 = cy + bh * 0.5f;
        box.confidence = score;
        box.classId = classId;
        UndoLetterbox(box, state_.inputWidth, state_.inputHeight, w, h);

        if (box.x2 - box.x1 > 0.001f && box.y2 - box.y1 > 0.001f)
            result.boxes.push_back(box);
    }

    return result;
}

DetectionResult DetectorOutputDecoder::DecodeYoloOutput(const Ort::Value& output, int w, int h)
{
    DetectionResult result;
    result.frameWidth = w;
    result.frameHeight = h;

    const auto shape = output.GetTensorTypeAndShapeInfo().GetShape();
    if (shape.size() != 3)
        return result;

    const int dim1 = static_cast<int>(std::max<int64_t>(1, shape[1]));
    const int dim2 = static_cast<int>(std::max<int64_t>(1, shape[2]));
    const bool attrsFirst = dim1 < dim2;
    const int attrCount = attrsFirst ? dim1 : dim2;
    const int numAnchors = attrsFirst ? dim2 : dim1;
    if (attrCount <= 4)
        return result;

    bool useObjectness = state_.yoloUsesObjectness;
    if (!resources_.classNames.empty())
    {
        if (attrCount - 5 == static_cast<int>(resources_.classNames.size()))
            useObjectness = true;
        else if (attrCount - 4 == static_cast<int>(resources_.classNames.size()))
            useObjectness = false;
    }

    const int classStart = useObjectness ? 5 : 4;
    const int numClasses = std::max(1, attrCount - classStart);
    state_.decodedClassCount = std::max(state_.decodedClassCount, numClasses);

    const float* data = output.GetTensorData<float>();
    std::vector<DetectionBox> boxes;
    boxes.reserve(numAnchors);

    for (int i = 0; i < numAnchors; ++i)
    {
        auto valueAt = [&](int attrIndex) -> float {
            return attrsFirst
                ? data[static_cast<size_t>(attrIndex) * numAnchors + i]
                : data[static_cast<size_t>(i) * attrCount + attrIndex];
        };

        float cx = valueAt(0);
        float cy = valueAt(1);
        float bw = valueAt(2);
        float bh = valueAt(3);

        if (std::max(std::fabs(cx), std::fabs(cy)) > 1.5f ||
            std::max(std::fabs(bw), std::fabs(bh)) > 1.5f)
        {
            cx /= std::max(1, state_.inputWidth);
            cy /= std::max(1, state_.inputHeight);
            bw /= std::max(1, state_.inputWidth);
            bh /= std::max(1, state_.inputHeight);
        }

        const float objectness = useObjectness ? valueAt(4) : 1.0f;
        int bestClass = 0;
        float bestClassScore = valueAt(classStart);
        for (int c = 1; c < numClasses; ++c)
        {
            const float classScore = valueAt(classStart + c);
            if (classScore > bestClassScore)
            {
                bestClassScore = classScore;
                bestClass = c;
            }
        }

        const float score = objectness * bestClassScore;
        if (score < confidenceThreshold_)
            continue;

        if (!state_.loggedRawDet)
        {
            std::cout << "[Detector] Raw YOLO det: cx=" << cx << " cy=" << cy
                << " w=" << bw << " h=" << bh
                << " score=" << score << " class=" << bestClass
                << " obj=" << objectness << std::endl;
            state_.loggedRawDet = true;
        }

        DetectionBox box;
        box.x1 = cx - bw * 0.5f;
        box.y1 = cy - bh * 0.5f;
        box.x2 = cx + bw * 0.5f;
        box.y2 = cy + bh * 0.5f;
        box.confidence = score;
        box.classId = bestClass;
        UndoLetterbox(box, state_.inputWidth, state_.inputHeight, w, h);

        if (box.x2 - box.x1 > 0.001f && box.y2 - box.y1 > 0.001f)
            boxes.push_back(box);
    }

    LimitBoxesByConfidence(boxes, kMaxYoloCandidatesBeforeNms);
    result.boxes = ApplyNms(boxes, kYoloNmsThreshold);
    return result;
}

DetectorTrackingStage::DetectorTrackingStage(DetectorThreadState& state, DetectorResources& resources)
    : state_(state)
    , resources_(resources)
{
}

void DetectorTrackingStage::Reset()
{
    std::lock_guard<std::mutex> lock(resources_.trackerMux);
    resources_.tracker = std::make_unique<BYTETracker>(30, 30);
    state_.trackerBackend.store(DetectorBackend::Cpu);
}

DetectionResult DetectorTrackingStage::Track(const DetectionResult& input) const
{
    DetectionResult tracked;
    tracked.frameWidth = input.frameWidth;
    tracked.frameHeight = input.frameHeight;
    tracked.generation = input.generation;
    tracked.serial = input.serial;

    if (input.boxes.empty())
        return tracked;

    std::vector<Object> objects;
    objects.reserve(input.boxes.size());
    for (const auto& b : input.boxes)
    {
        Object obj;
        obj.x = b.x1 * input.frameWidth;
        obj.y = b.y1 * input.frameHeight;
        obj.width = (b.x2 - b.x1) * input.frameWidth;
        obj.height = (b.y2 - b.y1) * input.frameHeight;
        obj.label = b.classId;
        obj.prob = b.confidence;
        objects.push_back(obj);
    }

    std::lock_guard<std::mutex> lock(resources_.trackerMux);
    if (!resources_.tracker)
        return tracked;

    const auto tracks = resources_.tracker->update(objects);
    tracked.boxes.reserve(tracks.size());
    for (const auto& t : tracks)
    {
        DetectionBox box;
        box.x1 = Clamp01(t.tlbr[0] / std::max(1, input.frameWidth));
        box.y1 = Clamp01(t.tlbr[1] / std::max(1, input.frameHeight));
        box.x2 = Clamp01(t.tlbr[2] / std::max(1, input.frameWidth));
        box.y2 = Clamp01(t.tlbr[3] / std::max(1, input.frameHeight));
        box.confidence = t.score;
        box.classId = t.classId;
        box.className = ResolveClassName(box.classId);
        box.trackId = t.track_id;
        tracked.boxes.push_back(box);
    }

    return tracked;
}

QString DetectorTrackingStage::ResolveClassName(int classId) const
{
    if (classId >= 0 && classId < static_cast<int>(resources_.classNames.size()))
        return resources_.classNames[classId];
    return QString("#%1").arg(classId);
}

DetectorWorkerLoop::DetectorWorkerLoop(
    DetectorThreadState& state,
    DetectorResources& resources,
    DetectorModelSession& modelSession,
    DetectorPreprocessPipeline& preprocessPipeline,
    DetectorOutputDecoder& outputDecoder,
    DetectorTrackingStage& trackingStage)
    : state_(state)
    , resources_(resources)
    , modelSession_(modelSession)
    , preprocessPipeline_(preprocessPipeline)
    , outputDecoder_(outputDecoder)
    , trackingStage_(trackingStage)
{
}

void DetectorWorkerLoop::Run(
    const std::function<void(bool)>& modelReadyCallback,
    const std::function<void(DetectionResult)>& detectionsReadyCallback)
{
    if (!state_.pendingModelPath.empty() && !state_.modelLoaded.load())
    {
        if (!modelSession_.LoadModel(state_.pendingModelPath, state_.pendingLabelsPath))
        {
            Logger::Instance().Log(
                LogLevel::Error,
                "detector",
                "worker.model_load_fail",
                "Detector worker failed to load model and is exiting",
                { { "model_path", state_.pendingModelPath } });
            modelReadyCallback(false);
            return;
        }
        modelReadyCallback(true);
    }

    trackingStage_.Reset();

    std::vector<float> blob;
    std::vector<uint8_t> localRgb;
    int localOrigW = 0;
    int localOrigH = 0;
    bool localGpuPath = false;
    int localReadIdx = 0;
    quint64 localGeneration = 0;
    quint64 localSerial = 0;
    quint64 localFrameToken = 0;

    while (!state_.isExit.load())
    {
        {
            std::unique_lock<std::mutex> lock(resources_.frameMux);
            resources_.frameCV.wait(lock, [this] { return state_.hasNewFrame || state_.isExit.load(); });

            if (state_.isExit.load())
                break;

            localGpuPath = state_.gpuPathActive;
            if (localGpuPath)
                localReadIdx = resources_.blobReadIdx;
            else
                localRgb.swap(resources_.pendingCpuRgb);
            localOrigW = state_.origWidth;
            localOrigH = state_.origHeight;
            localGeneration = state_.pendingFrameGeneration;
            localSerial = state_.pendingFrameSerial;
            localFrameToken = state_.pendingFrameToken.load();
            state_.hasNewFrame = false;
        }

        if (localOrigW <= 0 || localOrigH <= 0)
            continue;
        if (localGeneration != state_.mediaGeneration.load() || localSerial != state_.mediaSerial.load())
            continue;
        if (!localGpuPath && localRgb.empty())
            continue;
        if (!state_.modelLoaded.load() || !resources_.sharedModel || !resources_.sharedModel->ortSession)
            continue;

        const DetectorSharedModel& sharedModel = *resources_.sharedModel;

        InferenceTaskTicket schedulerTicket;
        bool schedulerAcquired = false;
        try
        {
            if (state_.scheduler)
            {
                schedulerTicket = state_.scheduler->Enqueue(BuildDetectorTaskProfile(state_, modelSession_));
                const InferenceAcquireResult acquireResult = state_.scheduler->Acquire(
                    schedulerTicket,
                    [this, localGeneration, localSerial, localFrameToken]() {
                        if (state_.isExit.load())
                            return true;
                        if (localGeneration != state_.mediaGeneration.load()
                            || localSerial != state_.mediaSerial.load())
                            return true;
                        return state_.liveMode.load()
                            && state_.pendingFrameToken.load() != localFrameToken;
                    });
                if (acquireResult != InferenceAcquireResult::Acquired)
                {
                    localOrigW = 0;
                    localOrigH = 0;
                    localGpuPath = false;
                    localRgb.clear();
                    continue;
                }
                schedulerAcquired = true;
            }

            Ort::Value inputTensor{ nullptr };
            std::vector<int64_t> inputShape = { 1, 3, state_.inputHeight, state_.inputWidth };

            if (localGpuPath && resources_.d_blob[localReadIdx])
            {
                if (resources_.preprocessEvent)
                    cudaEventSynchronize(resources_.preprocessEvent);

                Ort::MemoryInfo cudaMem("Cuda", OrtDeviceAllocator, 0, OrtMemTypeDefault);
                inputTensor = Ort::Value::CreateTensor<float>(
                    cudaMem, resources_.d_blob[localReadIdx],
                    3ull * state_.inputWidth * state_.inputHeight,
                    inputShape.data(), inputShape.size());
            }
            else
            {
                preprocessPipeline_.NormalizeToBlob(localRgb.data(), blob);
                Ort::MemoryInfo memInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
                inputTensor = Ort::Value::CreateTensor<float>(
                    memInfo, blob.data(), blob.size(),
                    inputShape.data(), inputShape.size());
            }

            const size_t numInputs = sharedModel.ortSession->GetInputCount();
            const size_t numOutputs = sharedModel.ortOutputNameViews.size();
            const char* inputNames[] = { sharedModel.ortInputName.c_str() };

            if (!state_.loggedModelInfo)
            {
                Logger::Instance().Log(
                    LogLevel::Info,
                    "detector",
                    "worker.model_info",
                    "Detector runtime model info",
                    {
                        { "inputs", std::to_string(numInputs) },
                        { "outputs", std::to_string(numOutputs) },
                        { "pipeline", modelSession_.GetPipelineSummary().toStdString() },
                    });
                state_.loggedModelInfo = true;
            }

            auto outputs = sharedModel.ortSession->Run(Ort::RunOptions{ nullptr },
                inputNames, &inputTensor, 1,
                sharedModel.ortOutputNameViews.data(), numOutputs);

            DetectionResult result = outputDecoder_.DecodeOutputs(outputs, localOrigW, localOrigH);
            result.generation = localGeneration;
            result.serial = localSerial;
            outputDecoder_.LimitBoxesByConfidence(result.boxes, kMaxBoxesBeforeTracking);
            if (!state_.loggedFirstResult)
            {
                Logger::Instance().Log(
                    LogLevel::Info,
                    "detector",
                    "worker.first_result",
                    "Detector produced first inference result",
                    { { "boxes", std::to_string(result.boxes.size()) } });
                state_.loggedFirstResult = true;
            }

            DetectionResult tracked = trackingStage_.Track(result);
            tracked.generation = localGeneration;
            tracked.serial = localSerial;
            if (!tracked.boxes.empty()
                && tracked.generation == state_.mediaGeneration.load()
                && tracked.serial == state_.mediaSerial.load())
                detectionsReadyCallback(std::move(tracked));

            if (schedulerAcquired)
                state_.scheduler->Release(schedulerTicket, true);
        }
        catch (const Ort::Exception& ex)
        {
            if (schedulerAcquired)
                state_.scheduler->Release(schedulerTicket, false);
            Logger::Instance().Log(
                LogLevel::Error,
                "detector",
                "worker.inference_fail",
                "Detector inference failed",
                { { "error", ex.what() } });
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        localOrigW = 0;
        localOrigH = 0;
        localGpuPath = false;
        localRgb.clear();
    }
}
