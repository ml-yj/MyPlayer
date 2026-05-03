

#include "detector_common.h"

#ifndef ORT_API_MANUAL_INIT
#define ORT_API_MANUAL_INIT
#endif
#include <onnxruntime_cxx_api.h>

#include <algorithm>
#include <cctype>

#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#endif

bool EnsureOrtApiInitialized()
{
    static bool initialized = false;
    static bool ok = false;
    if (initialized)
        return ok;

    initialized = true;
    const OrtApiBase* base = OrtGetApiBase();
    if (!base)
        return false;

    const OrtApi* api = base->GetApi(kOrtApiVersion);
    if (!api)
        return false;

    Ort::InitApi(api);
    ok = true;
    return true;
}

std::vector<QString> DefaultCocoNames()
{
    static const char* const names[] = {
        "person", "bicycle", "car", "motorcycle", "airplane", "bus", "train", "truck",
        "boat", "traffic light", "fire hydrant", "stop sign", "parking meter", "bench",
        "bird", "cat", "dog", "horse", "sheep", "cow", "elephant", "bear", "zebra",
        "giraffe", "backpack", "umbrella", "handbag", "tie", "suitcase", "frisbee",
        "skis", "snowboard", "sports ball", "kite", "baseball bat", "baseball glove",
        "skateboard", "surfboard", "tennis racket", "bottle", "wine glass", "cup",
        "fork", "knife", "spoon", "bowl", "banana", "apple", "sandwich", "orange",
        "broccoli", "carrot", "hot dog", "pizza", "donut", "cake", "chair", "couch",
        "potted plant", "bed", "dining table", "toilet", "tv", "laptop", "mouse",
        "remote", "keyboard", "cell phone", "microwave", "oven", "toaster", "sink",
        "refrigerator", "book", "clock", "vase", "scissors", "teddy bear",
        "hair drier", "toothbrush"
    };

    std::vector<QString> labels;
    labels.reserve(std::size(names));
    for (const char* name : names)
        labels.emplace_back(QString::fromUtf8(name));
    return labels;
}

std::string ToLowerCopy(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(),
        [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return text;
}

std::wstring Utf8ToWide(const std::string& text)
{
#ifdef _WIN32
    const int wlen = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    std::wstring wtext(wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, &wtext[0], wlen);
    return wtext;
#else
    return std::wstring();
#endif
}

DetectorModelFamily GuessFamilyFromPath(const std::string& modelPath)
{
    const std::string lowered = ToLowerCopy(modelPath);
    if (lowered.find("rtdetr") != std::string::npos ||
        lowered.find("rt-detr") != std::string::npos ||
        lowered.find("detr") != std::string::npos)
        return DetectorModelFamily::RtDetr;
    if (lowered.find("yolo") != std::string::npos)
        return DetectorModelFamily::Yolo;
    return DetectorModelFamily::Unknown;
}

float Clamp01(float value)
{
    return std::max(0.0f, std::min(1.0f, value));
}

void UndoLetterbox(DetectionBox& box, int inputWidth, int inputHeight, int origWidth, int origHeight)
{
    const float scale = std::min(
        static_cast<float>(inputWidth) / std::max(1, origWidth),
        static_cast<float>(inputHeight) / std::max(1, origHeight));
    const float scaledW = origWidth * scale;
    const float scaledH = origHeight * scale;
    const float padX = (inputWidth - scaledW) * 0.5f;
    const float padY = (inputHeight - scaledH) * 0.5f;

    box.x1 = ((box.x1 * inputWidth) - padX) / std::max(1.0f, scaledW);
    box.y1 = ((box.y1 * inputHeight) - padY) / std::max(1.0f, scaledH);
    box.x2 = ((box.x2 * inputWidth) - padX) / std::max(1.0f, scaledW);
    box.y2 = ((box.y2 * inputHeight) - padY) / std::max(1.0f, scaledH);

    box.x1 = Clamp01(box.x1);
    box.y1 = Clamp01(box.y1);
    box.x2 = Clamp01(box.x2);
    box.y2 = Clamp01(box.y2);
}

float IntersectionOverUnion(const DetectionBox& a, const DetectionBox& b)
{
    const float x1 = std::max(a.x1, b.x1);
    const float y1 = std::max(a.y1, b.y1);
    const float x2 = std::min(a.x2, b.x2);
    const float y2 = std::min(a.y2, b.y2);
    const float interW = std::max(0.0f, x2 - x1);
    const float interH = std::max(0.0f, y2 - y1);
    const float inter = interW * interH;
    const float areaA = std::max(0.0f, a.x2 - a.x1) * std::max(0.0f, a.y2 - a.y1);
    const float areaB = std::max(0.0f, b.x2 - b.x1) * std::max(0.0f, b.y2 - b.y1);
    const float denom = areaA + areaB - inter;
    if (denom <= 1e-6f)
        return 0.0f;
    return inter / denom;
}

const char* BackendToCString(DetectorBackend backend)
{
    switch (backend)
    {
    case DetectorBackend::Cpu: return "CPU";
    case DetectorBackend::Gpu: return "GPU";
    default: return "unknown";
    }
}

QString BackendToQString(DetectorBackend backend)
{
    return QString::fromLatin1(BackendToCString(backend));
}
