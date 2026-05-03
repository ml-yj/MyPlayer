
#include "resample.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <iostream>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/samplefmt.h>
#include <libavutil/version.h>
#include <libswresample/swresample.h>
}

namespace
{
#if LIBAVUTIL_VERSION_MAJOR >= 57
int AudioCodecParameterChannels(const AVCodecParameters* parameters)
{
    return parameters ? parameters->ch_layout.nb_channels : 0;
}

int AudioFrameChannels(const AVFrame* frame)
{
    return frame ? frame->ch_layout.nb_channels : 0;
}

void AssignAudioFrameLayout(AVFrame* frame, int channels)
{
    if (frame)
        av_channel_layout_default(&frame->ch_layout, channels > 0 ? channels : 2);
}
#else
int AudioCodecParameterChannels(const AVCodecParameters* parameters)
{
    return parameters ? parameters->channels : 0;
}

int AudioFrameChannels(const AVFrame* frame)
{
    return frame ? frame->channels : 0;
}

uint64_t DefaultAudioChannelLayout(int channels)
{
    return av_get_default_channel_layout(channels > 0 ? channels : 2);
}

void AssignAudioFrameLayout(AVFrame* frame, int channels)
{
    if (!frame)
        return;

    frame->channels = channels > 0 ? channels : 2;
    frame->channel_layout = DefaultAudioChannelLayout(frame->channels);
}
#endif
}

SwrResampleContext::~SwrResampleContext()
{
    Close();
}

bool SwrResampleContext::Open(AVCodecParameters* para, int outFormat, const ResampleState& state,
    bool isClearPara)
{
    if (!para)
        return false;

    Close();

#if LIBAVUTIL_VERSION_MAJOR >= 57
    AVChannelLayout outLayout = {};
    av_channel_layout_default(&outLayout, state.outChannels > 0 ? state.outChannels : 2);

    AVChannelLayout inLayout = para->ch_layout;
    const int ret = swr_alloc_set_opts2(
        &context_,
        &outLayout,
        static_cast<AVSampleFormat>(outFormat),
        state.outSampleRate > 0 ? state.outSampleRate : para->sample_rate,
        &inLayout,
        static_cast<AVSampleFormat>(para->format),
        para->sample_rate,
        0,
        nullptr);

    av_channel_layout_uninit(&outLayout);
#else
    const int outChannels = state.outChannels > 0 ? state.outChannels : 2;
    const int inChannels = AudioCodecParameterChannels(para) > 0 ? AudioCodecParameterChannels(para) : 2;
    const uint64_t outLayout = DefaultAudioChannelLayout(outChannels);
    const uint64_t inLayout = para->channel_layout != 0
        ? para->channel_layout
        : DefaultAudioChannelLayout(inChannels);
    context_ = swr_alloc_set_opts(
        nullptr,
        static_cast<int64_t>(outLayout),
        static_cast<AVSampleFormat>(outFormat),
        state.outSampleRate > 0 ? state.outSampleRate : para->sample_rate,
        static_cast<int64_t>(inLayout),
        static_cast<AVSampleFormat>(para->format),
        para->sample_rate,
        0,
        nullptr);
    const int ret = context_ ? 0 : AVERROR(ENOMEM);
#endif

    if (isClearPara)
        avcodec_parameters_free(&para);

    if (ret != 0)
    {
        std::cout << "swr_alloc_set_opts2 failed" << std::endl;
        Close();
        return false;
    }

    const int initRet = swr_init(context_);
    if (initRet != 0)
    {
        char buf[1024] = { 0 };
        av_strerror(initRet, buf, sizeof(buf) - 1);
        std::cout << "swr_init failed: " << buf << std::endl;
        Close();
        return false;
    }

    return true;
}

bool SwrResampleContext::OpenFromFrame(const AVFrame* frame, int outFormat, const ResampleState& state)
{
    if (!frame)
        return false;

    Close();

#if LIBAVUTIL_VERSION_MAJOR >= 57
    AVChannelLayout outLayout = {};
    av_channel_layout_default(&outLayout, state.outChannels > 0 ? state.outChannels : 2);

    AVChannelLayout inLayout = {};
    bool ownsInLayout = false;
    if (frame->ch_layout.nb_channels > 0)
    {
        inLayout = frame->ch_layout;
    }
    else
    {
        av_channel_layout_default(&inLayout, state.inChannels);
        ownsInLayout = true;
    }

    const int ret = swr_alloc_set_opts2(
        &context_,
        &outLayout,
        static_cast<AVSampleFormat>(outFormat),
        state.outSampleRate > 0 ? state.outSampleRate : frame->sample_rate,
        &inLayout,
        static_cast<AVSampleFormat>(frame->format),
        frame->sample_rate,
        0,
        nullptr);

    av_channel_layout_uninit(&outLayout);
    if (ownsInLayout)
        av_channel_layout_uninit(&inLayout);
#else
    const int outChannels = state.outChannels > 0 ? state.outChannels : 2;
    const int inChannels = AudioFrameChannels(frame) > 0 ? AudioFrameChannels(frame) : state.inChannels;
    const uint64_t outLayout = DefaultAudioChannelLayout(outChannels);
    const uint64_t inLayout = frame->channel_layout != 0
        ? frame->channel_layout
        : DefaultAudioChannelLayout(inChannels);
    context_ = swr_alloc_set_opts(
        nullptr,
        static_cast<int64_t>(outLayout),
        static_cast<AVSampleFormat>(outFormat),
        state.outSampleRate > 0 ? state.outSampleRate : frame->sample_rate,
        static_cast<int64_t>(inLayout),
        static_cast<AVSampleFormat>(frame->format),
        frame->sample_rate,
        0,
        nullptr);
    const int ret = context_ ? 0 : AVERROR(ENOMEM);
#endif

    if (ret != 0)
    {
        std::cout << "swr_alloc_set_opts2 failed (frame)" << std::endl;
        Close();
        return false;
    }

    const int initRet = swr_init(context_);
    if (initRet != 0)
    {
        char buf[1024] = { 0 };
        av_strerror(initRet, buf, sizeof(buf) - 1);
        std::cout << "swr_init failed (frame): " << buf << std::endl;
        Close();
        return false;
    }

    return true;
}

void SwrResampleContext::Close()
{
    if (context_)
        swr_free(&context_);
}

int SwrResampleContext::Convert(AVFrame* frame, uint8_t** outData, int maxOutSamples) const
{
    if (!context_ || !frame || !outData || maxOutSamples <= 0)
        return 0;

    return swr_convert(context_, outData, maxOutSamples,
        const_cast<const uint8_t**>(frame->data), frame->nb_samples);
}

AtempoFilterGraph::~AtempoFilterGraph()
{
    Close();
}

bool AtempoFilterGraph::Init(const ResampleState& state, int outFormat)
{
    Close();

    const double speed = state.speed;
    if (speed <= 0.01 || (speed > 0.99 && speed < 1.01))
        return false;

    filterGraph_ = avfilter_graph_alloc();
    if (!filterGraph_)
        return false;

    const AVFilter* abuffer = avfilter_get_by_name("abuffer");
    if (!abuffer)
    {
        Close();
        return false;
    }

    const int outChannels = state.outChannels > 0 ? state.outChannels : 2;
#if LIBAVUTIL_VERSION_MAJOR >= 57
    AVChannelLayout chLayout = {};
    av_channel_layout_default(&chLayout, outChannels);

    char chLayoutStr[64] = {};
    av_channel_layout_describe(&chLayout, chLayoutStr, sizeof(chLayoutStr));

    char args[512];
    std::snprintf(args, sizeof(args),
        "sample_rate=%d:sample_fmt=%s:channel_layout=%s",
        state.outSampleRate > 0 ? state.outSampleRate : state.inSampleRate,
        av_get_sample_fmt_name(static_cast<AVSampleFormat>(outFormat)),
        chLayoutStr);

    av_channel_layout_uninit(&chLayout);
#else
    const uint64_t chLayout = DefaultAudioChannelLayout(outChannels);
    char chLayoutStr[64] = {};
    av_get_channel_layout_string(chLayoutStr, sizeof(chLayoutStr), outChannels, chLayout);

    char args[512];
    std::snprintf(args, sizeof(args),
        "sample_rate=%d:sample_fmt=%s:channel_layout=%s",
        state.outSampleRate > 0 ? state.outSampleRate : state.inSampleRate,
        av_get_sample_fmt_name(static_cast<AVSampleFormat>(outFormat)),
        chLayoutStr);
#endif

    int ret = avfilter_graph_create_filter(&abufferCtx_, abuffer, "in", args, nullptr, filterGraph_);
    if (ret < 0)
    {
        Close();
        return false;
    }

    const AVFilter* atempo = avfilter_get_by_name("atempo");
    if (!atempo)
    {
        Close();
        return false;
    }

    char tempoArgs[64];
    double clampedSpeed = speed;
    if (clampedSpeed < 0.5)
        clampedSpeed = 0.5;
    if (clampedSpeed > 100.0)
        clampedSpeed = 100.0;
    std::snprintf(tempoArgs, sizeof(tempoArgs), "tempo=%f", clampedSpeed);

    ret = avfilter_graph_create_filter(&atempoCtx_, atempo, "atempo", tempoArgs, nullptr, filterGraph_);
    if (ret < 0)
    {
        Close();
        return false;
    }

    const AVFilter* abuffersink = avfilter_get_by_name("abuffersink");
    if (!abuffersink)
    {
        Close();
        return false;
    }

    ret = avfilter_graph_create_filter(&abuffersinkCtx_, abuffersink, "out", nullptr, nullptr, filterGraph_);
    if (ret < 0)
    {
        Close();
        return false;
    }

    ret = avfilter_link(abufferCtx_, 0, atempoCtx_, 0);
    if (ret < 0)
    {
        Close();
        return false;
    }

    ret = avfilter_link(atempoCtx_, 0, abuffersinkCtx_, 0);
    if (ret < 0)
    {
        Close();
        return false;
    }

    ret = avfilter_graph_config(filterGraph_, nullptr);
    if (ret < 0)
    {
        Close();
        return false;
    }

    std::cout << "atempo filter initialized: speed=" << speed << "x" << std::endl;
    return true;
}

void AtempoFilterGraph::Close()
{
    if (filterGraph_)
        avfilter_graph_free(&filterGraph_);
    filterGraph_ = nullptr;
    abufferCtx_ = nullptr;
    atempoCtx_ = nullptr;
    abuffersinkCtx_ = nullptr;
}

int AtempoFilterGraph::PushFrame(AVFrame* frame) const
{
    if (!abufferCtx_ || !frame)
        return -1;
    return av_buffersrc_add_frame(abufferCtx_, frame);
}

int AtempoFilterGraph::Drain(unsigned char* output, int bytesPerSample) const
{
    if (!abuffersinkCtx_ || !output || bytesPerSample <= 0)
        return 0;

    int totalSize = 0;
    AVFrame* filtFrame = av_frame_alloc();
    if (!filtFrame)
        return 0;

    while (true)
    {
        const int ret = av_buffersink_get_frame(abuffersinkCtx_, filtFrame);
        if (ret < 0)
            break;

        const int frameChannels = AudioFrameChannels(filtFrame) > 0
            ? AudioFrameChannels(filtFrame)
            : 2;
        const int frameBytes = filtFrame->nb_samples * frameChannels * bytesPerSample;
        std::memcpy(output + totalSize, filtFrame->data[0], frameBytes);
        totalSize += frameBytes;
        av_frame_unref(filtFrame);
    }

    av_frame_free(&filtFrame);
    return totalSize;
}

int AudioResamplePipeline::Run(AVFrame* inputFrame, unsigned char* output,
    unsigned char* preSpeedData, int* preSpeedSize, int outFormat, const ResampleState& state,
    SwrResampleContext& swrContext, AtempoFilterGraph& filterGraph) const
{
    if (!inputFrame)
        return 0;

    if (!output)
    {
        av_frame_free(&inputFrame);
        return 0;
    }

    if (preSpeedSize)
        *preSpeedSize = 0;

    if (!swrContext.IsOpen())
    {
        av_frame_free(&inputFrame);
        return 0;
    }

    const int maxOutSamples = inputFrame->nb_samples + 256;
    const int bytesPerSample = av_get_bytes_per_sample(static_cast<AVSampleFormat>(outFormat));
    const int outChannels = state.outChannels > 0 ? state.outChannels : 2;

    if (!filterGraph.IsActive())
    {
        uint8_t* data[2] = { output, nullptr };
        const int convertedSamples = swrContext.Convert(inputFrame, data, maxOutSamples);
        av_frame_free(&inputFrame);
        if (convertedSamples <= 0)
            return 0;

        const int outBytes = convertedSamples * outChannels * bytesPerSample;
        if (preSpeedData && preSpeedData != output)
            std::memcpy(preSpeedData, output, outBytes);
        if (preSpeedSize)
            *preSpeedSize = outBytes;
        return outBytes;
    }

    AVFrame* resampledFrame = av_frame_alloc();
    if (!resampledFrame)
    {
        av_frame_free(&inputFrame);
        return 0;
    }

    resampledFrame->format = outFormat;
    resampledFrame->sample_rate = state.outSampleRate > 0 ? state.outSampleRate : state.inSampleRate;
    resampledFrame->nb_samples = maxOutSamples;
    AssignAudioFrameLayout(resampledFrame, outChannels);
    if (av_frame_get_buffer(resampledFrame, 0) < 0)
    {
        av_frame_free(&inputFrame);
        av_frame_free(&resampledFrame);
        return 0;
    }

    uint8_t* data[2] = { resampledFrame->data[0], resampledFrame->data[1] };
    const int convertedSamples = swrContext.Convert(inputFrame, data, maxOutSamples);
    av_frame_free(&inputFrame);

    if (convertedSamples <= 0)
    {
        av_frame_free(&resampledFrame);
        return 0;
    }

    resampledFrame->nb_samples = convertedSamples;

    if (preSpeedData && resampledFrame->data[0])
    {
        const int rawBytes = resampledFrame->nb_samples * outChannels * bytesPerSample;
        std::memcpy(preSpeedData, resampledFrame->data[0], rawBytes);
        if (preSpeedSize)
            *preSpeedSize = rawBytes;
    }

    const int pushRet = filterGraph.PushFrame(resampledFrame);
    av_frame_free(&resampledFrame);
    if (pushRet < 0)
        return 0;

    return filterGraph.Drain(output, bytesPerSample);
}

Resample::Resample() = default;

Resample::~Resample()
{
    Close();
}

int Resample::ToAvSampleFormat(AudioOutputSampleFormat format)
{
    switch (format)
    {
    case AudioOutputSampleFormat::UInt8:
        return AV_SAMPLE_FMT_U8;
    case AudioOutputSampleFormat::Int16:
        return AV_SAMPLE_FMT_S16;
    case AudioOutputSampleFormat::Int32:
        return AV_SAMPLE_FMT_S32;
    case AudioOutputSampleFormat::Float32:
        return AV_SAMPLE_FMT_FLT;
    default:
        return AV_SAMPLE_FMT_S16;
    }
}

bool Resample::Open(AVCodecParameters* para, int outputSampleRate, int outputChannels,
    AudioOutputSampleFormat outputSampleFormat, bool isClearPara)
{
    if (!para)
        return false;

    std::lock_guard<std::mutex> lock(mux);
    atempoFilter_.Close();
    state_.inSampleRate = para->sample_rate;
    state_.inFmt = para->format;
    state_.inChannels = AudioCodecParameterChannels(para) > 0 ? AudioCodecParameterChannels(para) : 2;
    state_.outSampleRate = outputSampleRate > 0 ? outputSampleRate : para->sample_rate;
    state_.outChannels = outputChannels > 0 ? outputChannels : 2;
    outFormat = ToAvSampleFormat(outputSampleFormat);

    if (state_.inSampleRate <= 0 || state_.inFmt == AV_SAMPLE_FMT_NONE)
        return true;

    if (!swrContext_.Open(para, outFormat, state_, isClearPara))
        return false;

    if (state_.speed > 0.01 && !(state_.speed > 0.99 && state_.speed < 1.01))
        atempoFilter_.Init(state_, outFormat);

    return true;
}

void Resample::Close()
{
    std::lock_guard<std::mutex> lock(mux);
    const double currentSpeed = state_.speed;
    atempoFilter_.Close();
    swrContext_.Close();
    state_ = ResampleState{};
    state_.speed = currentSpeed;
}

bool Resample::SetSpeed(double speed)
{
    if (speed < 0.5)
        speed = 0.5;
    if (speed > 4.0)
        speed = 4.0;

    std::lock_guard<std::mutex> lock(mux);
    if (state_.inSampleRate <= 0)
        return false;

    state_.speed = speed;
    if (speed > 0.99 && speed < 1.01)
    {
        atempoFilter_.Close();
        return true;
    }

    return atempoFilter_.Init(state_, outFormat);
}

bool Resample::EnsureInputFormat(AVFrame* frame)
{
    if (!frame)
        return false;

    const int frameSampleRate = frame->sample_rate > 0 ? frame->sample_rate : state_.inSampleRate;
    const int frameFormat = frame->format;
    int frameChannels = AudioFrameChannels(frame);
    if (frameChannels <= 0)
        frameChannels = state_.inChannels > 0 ? state_.inChannels : 2;

    const bool formatMatches = swrContext_.IsOpen()
        && state_.inSampleRate == frameSampleRate
        && state_.inFmt == frameFormat
        && state_.inChannels == frameChannels;
    if (formatMatches)
        return true;

    atempoFilter_.Close();
    state_.inSampleRate = frameSampleRate;
    state_.inFmt = frameFormat;
    state_.inChannels = frameChannels;
    if (!swrContext_.OpenFromFrame(frame, outFormat, state_))
        return false;

    if (state_.speed > 0.01 && !(state_.speed > 0.99 && state_.speed < 1.01))
        return atempoFilter_.Init(state_, outFormat);
    return true;
}

int Resample::ResampleFinal(AVFrame* indata, unsigned char* data,
    unsigned char* preSpeedData, int* preSpeedSize)
{
    std::lock_guard<std::mutex> lock(mux);
    if (!EnsureInputFormat(indata))
    {
        av_frame_free(&indata);
        return 0;
    }
    return pipeline_.Run(indata, data, preSpeedData, preSpeedSize, outFormat,
        state_, swrContext_, atempoFilter_);
}
