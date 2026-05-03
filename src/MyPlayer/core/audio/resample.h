#pragma once

#include <cstdint>
#include <mutex>

struct AVCodecParameters;
struct AVFilterContext;
struct AVFilterGraph;
struct AVFrame;
struct SwrContext;

enum class AudioOutputSampleFormat
{
    Unknown = 0,
    UInt8,
    Int16,
    Int32,
    Float32,
};

struct AudioOutputFormat
{
    int sampleRate = 0;
    int channels = 0;
    int sampleSize = 0;
    AudioOutputSampleFormat sampleFormat = AudioOutputSampleFormat::Unknown;
};

struct ResampleState
{
    double speed = 1.0;
    int inSampleRate = 0;
    int inFmt = 0;
    int inChannels = 0;
    int outSampleRate = 0;
    int outChannels = 2;
};

class SwrResampleContext
{
public:
    SwrResampleContext() = default;
    ~SwrResampleContext();

    bool Open(AVCodecParameters* para, int outFormat, const ResampleState& state, bool isClearPara);
    bool OpenFromFrame(const AVFrame* frame, int outFormat, const ResampleState& state);
    void Close();
    bool IsOpen() const { return context_ != nullptr; }
    int Convert(AVFrame* frame, uint8_t** outData, int maxOutSamples) const;

private:
    SwrContext* context_ = nullptr;
};

class AtempoFilterGraph
{
public:
    AtempoFilterGraph() = default;
    ~AtempoFilterGraph();

    bool Init(const ResampleState& state, int outFormat);
    void Close();
    bool IsActive() const { return filterGraph_ != nullptr; }
    int PushFrame(AVFrame* frame) const;
    int Drain(unsigned char* output, int bytesPerSample) const;

private:
    AVFilterGraph* filterGraph_ = nullptr;
    AVFilterContext* abufferCtx_ = nullptr;
    AVFilterContext* atempoCtx_ = nullptr;
    AVFilterContext* abuffersinkCtx_ = nullptr;
};

class AudioResamplePipeline
{
public:
    int Run(AVFrame* inputFrame, unsigned char* output, unsigned char* preSpeedData,
        int* preSpeedSize, int outFormat, const ResampleState& state,
        SwrResampleContext& swrContext, AtempoFilterGraph& filterGraph) const;
};

class Resample
{
public:
    Resample();
    virtual ~Resample();

    virtual bool Open(AVCodecParameters* para, int outputSampleRate, int outputChannels,
        AudioOutputSampleFormat outputSampleFormat, bool isClearPara = false);
    virtual void Close();
    virtual int ResampleFinal(AVFrame* indata, unsigned char* data,
        unsigned char* preSpeedData = nullptr, int* preSpeedSize = nullptr);
    bool SetSpeed(double speed);

    int outFormat = 1;

protected:
    bool EnsureInputFormat(AVFrame* frame);
    static int ToAvSampleFormat(AudioOutputSampleFormat format);

    std::mutex mux;
    ResampleState state_;
    SwrResampleContext swrContext_;
    AtempoFilterGraph atempoFilter_;
    AudioResamplePipeline pipeline_;
};
