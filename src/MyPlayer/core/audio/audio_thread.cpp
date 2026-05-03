#include "audio_thread.h"

#include <QtGlobal>
#include <QAudio>
#include <QAudioSink>

#if QT_VERSION < QT_VERSION_CHECK(6, 5, 0)
namespace MyPlayerAudio = QAudio;
#else
namespace MyPlayerAudio = QtAudio;
#endif

class AudioBufferMetrics
{
public:
    static long long QueuedMs(const QAudioSink* output, int sampleRate, int sampleSize, int channels)
    {
        if (!output)
            return 0;

        const double queuedBytes = output->bufferSize() - output->bytesFree();
        const double bytesPerSecond = static_cast<double>(sampleRate) * (sampleSize / 8) * channels;
        if (bytesPerSecond <= 0.0)
            return 0;

        return static_cast<long long>((queuedBytes / bytesPerSecond) * 1000.0);
    }

    static qsizetype DesiredBufferBytes(int sampleRate, int sampleSize, int channels, int targetBufferMs)
    {
        if (targetBufferMs <= 0)
            return 0;

        const long long bytesPerSecond = static_cast<long long>(sampleRate) * (sampleSize / 8) * channels;
        const long long desiredBytes = (bytesPerSecond * targetBufferMs) / 1000;
        if (desiredBytes <= 0)
            return 0;

        return static_cast<qsizetype>(desiredBytes);
    }
};

#include "resample.h"

#include <mutex>

class QThread;
class QtAudioOutputWorker;

class QtAudioOutputBackend
{
public:
    QtAudioOutputBackend();
    ~QtAudioOutputBackend();

    bool Open(int sampleRate, int sampleSize, int channels, double volume, int targetBufferMs);
    void Close();
    void Clear();
    long long GetNoPlayMs(int sampleRate, int sampleSize, int channels) const;
    bool Write(const unsigned char* data, int dataSize);
    int GetFree() const;
    AudioOutputFormat GetOutputFormat() const;
    std::string GetLastError() const;
    bool RecoverWriteDevice();
    void SetPause(bool isPause);
    void SetTargetBufferMs(int bufferMs);
    void SetVolume(double volume);

private:
    void ShutdownWorkerThread();

    QThread* controlThread_ = nullptr;
    QtAudioOutputWorker* worker_ = nullptr;
    mutable std::mutex mux_;
};

class AudioPlay
{
public:

    static AudioPlay* Create();

    AudioPlay();
    virtual ~AudioPlay();

    virtual bool Open() = 0;

    virtual void Close() = 0;

    virtual void Clear() = 0;

    virtual long long GetNoPlayMs() = 0;

    virtual void SetTargetBufferMs(int bufferMs) = 0;

    virtual bool Write(const unsigned char* data, int datasize) = 0;

    virtual int GetFree() = 0;
    virtual std::string GetLastError() const = 0;

    virtual void SetPause(bool isPause) = 0;

    virtual void SetVolume(double vol) = 0;

    virtual bool RecoverWriteDevice() = 0;

    int sampleRate = 44100;
    int sampleSize = 16;
    int channels = 2;
    AudioOutputSampleFormat sampleFormat = AudioOutputSampleFormat::Int16;
};

#include <memory>
#include <mutex>

class AudioOutputBridge
{
public:
    ~AudioOutputBridge();

    bool Open(int sampleRate, int channels, int targetBufferMs);
    void Close();
    void Clear();

    void SetPause(bool isPause);
    void SetVolume(double volume);
    void SetTargetBufferMs(int bufferMs);
    bool RecoverWriteDevice();

    long long GetBufferedMs();
    int GetFree();
    bool Write(const unsigned char* data, int dataSize);
    int GetOutputSampleRate();
    int GetOutputChannels();
    int GetOutputSampleSize();
    AudioOutputSampleFormat GetOutputSampleFormat();
    std::string GetLastError();

private:
    std::mutex mux_;
    std::unique_ptr<AudioPlay> output_;
};

class AudioOutputBridge;
class Decode;
class DecodeThread;
struct AudioThreadState;

class AudioClockController
{
public:
    explicit AudioClockController(AudioThreadState& state);

    void SetPlaybackSpeed(double speed);
    void SetTargetOutputBufferMs(int bufferMs);
    void ResetLiveLatencyStats();

    void SetPts(long long pts);
    long long GetPts() const;
    int GetLiveCatchUpCount() const;
    int GetProgressiveThrottleCount() const;

    long long OutputMsToMediaMs(long long outputMs) const;
    long long SyncPtsToOutput(Decode* decode, AudioOutputBridge& outputBridge);
    int ComputeProgressiveThrottleDelayMs(long long deviceBufferMs, int queueLimit);
    bool MaybeCatchUpToLive(DecodeThread& owner, Decode* decode,
        AudioOutputBridge& outputBridge, long long deviceBufferMs, int queueLimit);
    bool ShouldDropNonMonotonicLiveFrame(long long framePtsMs, int queueLimit) const;
    void MarkQueuedFrame(long long framePtsMs);

private:
    bool UsesAggressiveLiveSync(int queueLimit) const;
    AudioThreadState& state_;
};

#include <atomic>
#include <memory>
#include <mutex>

class AudioClockController;
class AudioOutputBridge;
class Decode;
class DecodeThread;
class WhisperThread;
struct AVCodecParameters;
struct AVFrame;
struct AudioThreadState;

struct AudioResampleResult
{
    int pcmSize = 0;
    int asrSize = 0;
    long long framePtsMs = -1;
};

class AudioResampleStage
{
public:
    AudioResampleStage();
    ~AudioResampleStage();

    bool Open(AVCodecParameters* parameters, int outputSampleRate, int outputChannels,
        AudioOutputSampleFormat outputSampleFormat);
    void Close();
    void SetSpeed(double speed);
    bool ResampleFrame(AVFrame* frame, Decode* decode, unsigned char* pcm,
        unsigned char* asrPcm, AudioResampleResult& result);

private:
    std::mutex mux_;
    std::unique_ptr<Resample> resample_;
};

class AudioAsrBridge
{
public:
    void SetWhisperThread(WhisperThread* thread);
    bool WantsAudio() const;
    void PushAudio(const unsigned char* data, int bytes, int sampleRate, long long startMs);

private:
    mutable std::mutex mux_;
    WhisperThread* whisperThread_ = nullptr;
};

class AudioPacketPump
{
public:
    AudioPacketPump(DecodeThread& owner, std::atomic<bool>& exitFlag, AudioThreadState& state,
        AudioOutputBridge& outputBridge, AudioResampleStage& resampleStage,
        AudioClockController& clockController, AudioAsrBridge& asrBridge);

    void Run();

private:
    DecodeThread& owner_;
    std::atomic<bool>& exitFlag_;
    AudioThreadState& state_;
    AudioOutputBridge& outputBridge_;
    AudioResampleStage& resampleStage_;
    AudioClockController& clockController_;
    AudioAsrBridge& asrBridge_;
};

#include "../media/decode.h"
#include "../../common/diagnostics/logger.h"

#include <iostream>

AudioThread::AudioThread()
{
    state_ = std::make_unique<AudioThreadState>();
    outputBridge_ = std::make_unique<AudioOutputBridge>();
    resampleStage_ = std::make_unique<AudioResampleStage>();
    clockController_ = std::make_unique<AudioClockController>(*state_);
    asrBridge_ = std::make_unique<AudioAsrBridge>();
    packetPump_ = std::make_unique<AudioPacketPump>(
        *this, isExit, *state_, *outputBridge_, *resampleStage_, *clockController_, *asrBridge_);
}

AudioThread::~AudioThread()
{
    Close();
}

bool AudioThread::Open(AVCodecParameters* para, int sampleRate, int channels)
{
    lastOpenError_.clear();
    (void)channels;
    if (!para)
    {
        lastOpenError_ = "Audio parameters are missing";
        return false;
    }

    if (!decode)
        decode = new Decode();

    isExit.store(false);
    Clear();
    clockController_->SetPts(0);
    clockController_->ResetLiveLatencyStats();

    decode->isAudio = true;

    if (!outputBridge_->Open(sampleRate, channels, state_->targetOutputBufferMs.load()))
    {
        lastOpenError_ = outputBridge_->GetLastError();
        if (lastOpenError_.empty())
            lastOpenError_ = "Failed to open audio output device";
        std::cout << "XAudioPlay Open failed!" << std::endl;
        return false;
    }

    if (!resampleStage_->Open(para,
        outputBridge_->GetOutputSampleRate(),
        outputBridge_->GetOutputChannels(),
        outputBridge_->GetOutputSampleFormat()))
    {
        lastOpenError_ = "Failed to initialize audio resampler";
        std::cout << "XResample Open failed!" << std::endl;
        outputBridge_->Close();
        return false;
    }
    resampleStage_->SetSpeed(state_->playbackSpeed.load());

    if (!decode->Open(para))
    {
        lastOpenError_ = "Failed to initialize audio decoder";
        std::cout << "Audio Decode Open failed!" << std::endl;
        outputBridge_->Close();
        resampleStage_->Close();
        return false;
    }

    lastOpenError_.clear();
    std::cout << "XAudioThread::Open Success" << std::endl;
    return true;
}

void AudioThread::Close()
{
    isExit.store(true);
    cv.notify_all();
    wait();

    if (outputBridge_)
        outputBridge_->Close();
    if (resampleStage_)
        resampleStage_->Close();

    DecodeThread::Close();
}

void AudioThread::Clear()
{
    DecodeThread::Clear();
    if (state_)
    {
        state_->lastQueuedFramePtsMs.store(-1);
        state_->progressiveThrottleActive.store(false);
    }
    if (outputBridge_ && state_ && state_->playbackKind.load() == StreamPlaybackKind::Live)
        outputBridge_->Clear();
}

void AudioThread::DiscardQueuedDataKeepOutput()
{
    DecodeThread::Clear();
    if (state_)
    {
        state_->lastQueuedFramePtsMs.store(-1);
        state_->progressiveThrottleActive.store(false);
    }
    if (clockController_)
        clockController_->SetPts(0);
}

void AudioThread::DiscardQueuedDataAndResetOutput()
{
    DiscardQueuedDataKeepOutput();
    if (outputBridge_)
        outputBridge_->Clear();
}

void AudioThread::run()
{
    Logger::Instance().Log(
        LogLevel::Info,
        "audio",
        "thread.run",
        "Audio thread started");
    if (packetPump_)
        packetPump_->Run();
}

void AudioThread::SetPause(bool isPause)
{
    state_->isPause.store(isPause);
    if (outputBridge_)
        outputBridge_->SetPause(isPause);
}

void AudioThread::SetVolume(double volume)
{
    if (outputBridge_)
        outputBridge_->SetVolume(volume);
}

void AudioThread::SetSpeed(double speed)
{
    if (clockController_)
        clockController_->SetPlaybackSpeed(speed);
    if (resampleStage_)
        resampleStage_->SetSpeed(speed);
}

void AudioThread::SetOutputBufferMs(int bufferMs)
{
    if (clockController_)
        clockController_->SetTargetOutputBufferMs(bufferMs);
    if (outputBridge_)
        outputBridge_->SetTargetBufferMs(bufferMs > 0 ? bufferMs : 0);
}

void AudioThread::SetPlaybackKind(StreamPlaybackKind playbackKind)
{
    if (state_)
        state_->playbackKind.store(playbackKind);
}

long long AudioThread::GetAudioDeviceBufferMs()
{
    return outputBridge_ ? outputBridge_->GetBufferedMs() : 0;
}

int AudioThread::GetLiveCatchUpCount() const
{
    return clockController_ ? clockController_->GetLiveCatchUpCount() : 0;
}

int AudioThread::GetProgressiveThrottleCount() const
{
    return clockController_ ? clockController_->GetProgressiveThrottleCount() : 0;
}

void AudioThread::ResetLiveLatencyStats()
{
    if (clockController_)
        clockController_->ResetLiveLatencyStats();
}

void AudioThread::SetWhisperThread(WhisperThread* thread)
{
    if (asrBridge_)
        asrBridge_->SetWhisperThread(thread);
}

void AudioThread::SetPts(long long pts)
{
    if (clockController_)
        clockController_->SetPts(pts);
}

long long AudioThread::GetPts() const
{
    return clockController_ ? clockController_->GetPts() : 0;
}

std::string AudioThread::GetLastOpenError() const
{
    return lastOpenError_;
}

#include <QAudio>
#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSink>
#include <QByteArray>
#include <QIODevice>
#include <QMediaDevices>
#include <QMetaObject>
#include <QString>
#include <QThread>

#include "../../common/diagnostics/logger.h"

namespace
{
QAudioFormat BuildFormat(int sampleRate, int sampleSize, int channels)
{
    QAudioFormat format;
    format.setSampleRate(sampleRate);
    format.setChannelCount(channels);
    if (sampleSize == 16)
        format.setSampleFormat(QAudioFormat::Int16);
    else if (sampleSize == 8)
        format.setSampleFormat(QAudioFormat::UInt8);
    else
        format.setSampleFormat(QAudioFormat::Float);
    return format;
}

int SampleSizeBits(QAudioFormat::SampleFormat format)
{
    switch (format)
    {
    case QAudioFormat::UInt8:
        return 8;
    case QAudioFormat::Int16:
        return 16;
    case QAudioFormat::Int32:
    case QAudioFormat::Float:
        return 32;
    default:
        return 0;
    }
}

AudioOutputSampleFormat MapSampleFormat(QAudioFormat::SampleFormat format)
{
    switch (format)
    {
    case QAudioFormat::UInt8:
        return AudioOutputSampleFormat::UInt8;
    case QAudioFormat::Int16:
        return AudioOutputSampleFormat::Int16;
    case QAudioFormat::Int32:
        return AudioOutputSampleFormat::Int32;
    case QAudioFormat::Float:
        return AudioOutputSampleFormat::Float32;
    default:
        return AudioOutputSampleFormat::Unknown;
    }
}

QString DescribeFormat(const QAudioFormat& format)
{
    return QString("%1 Hz / %2 ch / %3 bits / %4")
        .arg(format.sampleRate())
        .arg(format.channelCount())
        .arg(SampleSizeBits(format.sampleFormat()))
        .arg(static_cast<int>(format.sampleFormat()));
}

const char* DescribeAudioError(MyPlayerAudio::Error error)
{
    switch (error)
    {
    case MyPlayerAudio::NoError: return "NoError";
    case MyPlayerAudio::OpenError: return "OpenError";
    case MyPlayerAudio::IOError: return "IOError";
    case MyPlayerAudio::UnderrunError: return "UnderrunError";
    case MyPlayerAudio::FatalError: return "FatalError";
    default: return "UnknownError";
    }
}

QAudioFormat PickSupportedFormat(const QAudioDevice& device, const QAudioFormat& requested)
{
    if (device.isNull() || device.isFormatSupported(requested))
        return requested;

    const QAudioFormat preferred = device.preferredFormat();
    const int preferredRate = preferred.sampleRate() > 0 ? preferred.sampleRate() : requested.sampleRate();
    const int preferredChannels = preferred.channelCount() > 0 ? preferred.channelCount() : requested.channelCount();

    const QAudioFormat candidates[] = {
        BuildFormat(preferredRate, 16, requested.channelCount()),
        BuildFormat(requested.sampleRate(), 16, preferredChannels),
        BuildFormat(preferredRate, 16, preferredChannels),
    };

    for (const QAudioFormat& candidate : candidates)
    {
        if (device.isFormatSupported(candidate))
            return candidate;
    }

    return preferred;
}

template<typename Fn>
void InvokeOnWorker(QObject* context, QThread* ownerThread, Fn&& fn)
{
    if (!context || !ownerThread)
        return;

    if (QThread::currentThread() == ownerThread)
    {
        fn();
        return;
    }

    QMetaObject::invokeMethod(context, std::forward<Fn>(fn), Qt::BlockingQueuedConnection);
}
}

class QtAudioOutputWorker final : public QObject
{
public:
    ~QtAudioOutputWorker() override
    {
        Close();
    }

    bool Open(int sampleRate, int sampleSize, int channels, double volume, int targetBufferMs)
    {
        Close();
        lastError_.clear();
        targetBufferMs_ = targetBufferMs > 0 ? targetBufferMs : 0;

        const QAudioFormat requestedFormat = BuildFormat(sampleRate, sampleSize, channels);
        const QAudioDevice device = QMediaDevices::defaultAudioOutput();
        const bool hasEnumeratedDevice = !QMediaDevices::audioOutputs().isEmpty();
        if (!hasEnumeratedDevice || device.isNull())
        {
            lastError_ = "No audio output device detected";
            Logger::Instance().Log(
                LogLevel::Warning,
                "audio",
                "output.device_missing",
                "No audio output device detected",
                {
                    { "requested", DescribeFormat(requestedFormat).toStdString() },
                    { "enumerated_device_count", std::to_string(QMediaDevices::audioOutputs().size()) },
                });
            return false;
        }

        const QAudioFormat chosenFormat = PickSupportedFormat(device, requestedFormat);

        output_ = new QAudioSink(device, chosenFormat);
        output_->setVolume(volume);

        format_.sampleRate = chosenFormat.sampleRate();
        format_.channels = chosenFormat.channelCount();
        format_.sampleSize = SampleSizeBits(chosenFormat.sampleFormat());
        format_.sampleFormat = MapSampleFormat(chosenFormat.sampleFormat());

        const qsizetype desiredBytes = AudioBufferMetrics::DesiredBufferBytes(
            format_.sampleRate, format_.sampleSize, format_.channels, targetBufferMs_);
        if (desiredBytes > 0)
            output_->setBufferSize(desiredBytes);

        io_ = output_->start();
        if (!io_)
        {
            lastError_ = QString("Failed to start audio output device (%1)")
                .arg(DescribeAudioError(output_->error()))
                .toStdString();
            Logger::Instance().Log(
                LogLevel::Warning,
                "audio",
                "output.open_fail",
                "Failed to start audio output device",
                {
                    { "requested", DescribeFormat(requestedFormat).toStdString() },
                    { "chosen", DescribeFormat(chosenFormat).toStdString() },
                    { "device", device.description().toStdString() },
                    { "state", std::to_string(static_cast<int>(output_->state())) },
                    { "error", DescribeAudioError(output_->error()) },
                    { "target_buffer_ms", std::to_string(targetBufferMs_) },
                });
            return false;
        }

        Logger::Instance().Log(
            LogLevel::Info,
            "audio",
            "output.format",
            "Opened audio output device",
            {
                { "requested", DescribeFormat(requestedFormat).toStdString() },
                { "chosen", DescribeFormat(chosenFormat).toStdString() },
                { "device", device.description().toStdString() },
                { "exact_match", chosenFormat == requestedFormat ? "true" : "false" },
                { "target_buffer_ms", std::to_string(targetBufferMs_) },
            });
        return true;
    }

    void Close()
    {
        io_ = nullptr;
        if (output_)
        {
            output_->stop();
            delete output_;
            output_ = nullptr;
        }
        format_ = {};
    }

    void Clear()
    {
        if (!output_)
            return;

        const bool wasPaused = output_->state() == MyPlayerAudio::SuspendedState;
        const double currentVolume = output_->volume();
        io_ = nullptr;

        output_->reset();
        io_ = output_->start();
        output_->setVolume(currentVolume);

        if (io_ && io_->isOpen())
        {
            const int silenceSize = format_.sampleRate * format_.channels * (format_.sampleSize / 8) / 100;
            if (silenceSize > 0 && output_->bytesFree() >= silenceSize)
            {
                QByteArray silence(silenceSize, 0);
                io_->write(silence);
            }
        }

        if (wasPaused)
            output_->suspend();
    }

    long long GetNoPlayMs(int sampleRate, int sampleSize, int channels) const
    {
        return AudioBufferMetrics::QueuedMs(output_, sampleRate, sampleSize, channels);
    }

    bool Write(const QByteArray& bytes)
    {
        if (!output_ || !io_ || bytes.isEmpty())
            return false;
        if (!io_->isOpen())
        {
            const int oldState = static_cast<int>(output_->state());
            const int oldError = static_cast<int>(output_->error());
            const double currentVolume = output_->volume();

            if (output_->state() == MyPlayerAudio::SuspendedState)
                output_->resume();

            if (!io_ || !io_->isOpen())
            {
                io_ = nullptr;
                const bool needsHardReset =
                    output_->state() == MyPlayerAudio::StoppedState
                    && output_->error() != MyPlayerAudio::NoError;

                if (needsHardReset)
                    output_->reset();
                io_ = output_->start();
                output_->setVolume(currentVolume);
            }

            if (!io_ || !io_->isOpen())
            {
                Logger::Instance().Log(
                    LogLevel::Warning,
                    "audio",
                    "output.device_closed",
                    "Audio output device is closed before write",
                    {
                        { "state", std::to_string(oldState) },
                        { "error", std::to_string(oldError) },
                    });
                return false;
            }

            Logger::Instance().Log(
                LogLevel::Info,
                "audio",
                "output.recovered",
                "Recovered closed audio output device before write",
                {
                    { "previous_state", std::to_string(oldState) },
                    { "previous_error", std::to_string(oldError) },
                });
        }

        qint64 totalWritten = 0;
        int stalledWrites = 0;
        bool loggedSlowWrite = false;
        while (totalWritten < bytes.size())
        {
            if (output_->state() == MyPlayerAudio::StoppedState && output_->error() != MyPlayerAudio::NoError)
            {
                Logger::Instance().Log(
                    LogLevel::Warning,
                    "audio",
                    "output.write_error",
                    "Audio output stopped while writing",
                    {
                        { "state", std::to_string(static_cast<int>(output_->state())) },
                        { "error", std::to_string(static_cast<int>(output_->error())) },
                        { "written", std::to_string(totalWritten) },
                        { "size", std::to_string(bytes.size()) },
                    });
                return false;
            }

            const qint64 remaining = bytes.size() - totalWritten;
            const qint64 freeBytes = output_->bytesFree();
            if (freeBytes >= 0 && freeBytes < remaining)
            {
                ++stalledWrites;
                if (!loggedSlowWrite && stalledWrites >= 25)
                {
                    Logger::Instance().Log(
                        LogLevel::Info,
                        "audio",
                        "output.write_wait",
                        "Audio output is waiting for enough contiguous device space",
                        {
                            { "state", std::to_string(static_cast<int>(output_->state())) },
                            { "error", std::to_string(static_cast<int>(output_->error())) },
                            { "written", std::to_string(totalWritten) },
                            { "size", std::to_string(bytes.size()) },
                            { "free_bytes", std::to_string(freeBytes) },
                        });
                    loggedSlowWrite = true;
                }
                if (stalledWrites > 5000)
                {
                    Logger::Instance().Log(
                        LogLevel::Warning,
                        "audio",
                        "output.write_timeout",
                        "Audio output write timed out while waiting for contiguous device space",
                        {
                            { "state", std::to_string(static_cast<int>(output_->state())) },
                            { "error", std::to_string(static_cast<int>(output_->error())) },
                            { "written", std::to_string(totalWritten) },
                            { "size", std::to_string(bytes.size()) },
                            { "free_bytes", std::to_string(freeBytes) },
                        });
                    return false;
                }
                QThread::msleep(1);
                continue;
            }

            const qint64 written = io_->write(
                bytes.constData() + totalWritten,
                remaining);
            if (written > 0)
            {
                totalWritten += written;
                stalledWrites = 0;
                continue;
            }

            ++stalledWrites;
            if (!loggedSlowWrite && stalledWrites >= 25)
            {
                Logger::Instance().Log(
                    LogLevel::Info,
                    "audio",
                    "output.write_wait",
                    "Audio output write is waiting for device space",
                    {
                        { "state", std::to_string(static_cast<int>(output_->state())) },
                        { "error", std::to_string(static_cast<int>(output_->error())) },
                        { "written", std::to_string(totalWritten) },
                        { "size", std::to_string(bytes.size()) },
                    });
                loggedSlowWrite = true;
            }
            if (stalledWrites > 5000)
            {
                Logger::Instance().Log(
                    LogLevel::Warning,
                    "audio",
                    "output.write_timeout",
                    "Audio output write timed out while waiting for device space",
                    {
                        { "state", std::to_string(static_cast<int>(output_->state())) },
                        { "error", std::to_string(static_cast<int>(output_->error())) },
                        { "written", std::to_string(totalWritten) },
                        { "size", std::to_string(bytes.size()) },
                    });
                return false;
            }
            QThread::msleep(1);
        }
        return true;
    }

    int GetFree() const
    {
        return output_ ? output_->bytesFree() : 0;
    }

    AudioOutputFormat GetOutputFormat() const
    {
        return format_;
    }

    std::string GetLastError() const
    {
        return lastError_;
    }

    bool RecoverWriteDevice()
    {
        if (!output_)
            return false;

        const double currentVolume = output_->volume();
        if (output_->state() == MyPlayerAudio::SuspendedState)
            output_->resume();

        if (io_ && io_->isOpen())
            return true;

        io_ = nullptr;
        const bool needsHardReset =
            output_->state() == MyPlayerAudio::StoppedState
            && output_->error() != MyPlayerAudio::NoError;
        if (needsHardReset)
            output_->reset();
        io_ = output_->start();
        output_->setVolume(currentVolume);
        return io_ && io_->isOpen();
    }

    void SetPause(bool isPause)
    {
        if (!output_)
            return;
        if (isPause)
            output_->suspend();
        else
        {
            if (output_->state() == MyPlayerAudio::SuspendedState)
            {
                output_->resume();
            }
            else if (!io_ || !io_->isOpen())
            {
                const double currentVolume = output_->volume();
                io_ = nullptr;
                const bool needsHardReset =
                    output_->state() == MyPlayerAudio::StoppedState
                    && output_->error() != MyPlayerAudio::NoError;
                if (needsHardReset)
                    output_->reset();
                io_ = output_->start();
                output_->setVolume(currentVolume);
            }
        }
    }

    void SetTargetBufferMs(int bufferMs)
    {
        const int normalizedBufferMs = bufferMs > 0 ? bufferMs : 0;
        if (targetBufferMs_ == normalizedBufferMs)
            return;

        targetBufferMs_ = normalizedBufferMs;
        if (!output_ || format_.sampleRate <= 0 || format_.sampleSize <= 0 || format_.channels <= 0)
            return;

        const bool wasPaused = output_->state() == MyPlayerAudio::SuspendedState;
        const double currentVolume = output_->volume();
        const int sampleRate = format_.sampleRate;
        const int sampleSize = format_.sampleSize;
        const int channels = format_.channels;
        const bool reopened = Open(sampleRate, sampleSize, channels, currentVolume, targetBufferMs_);
        if (reopened && wasPaused)
            SetPause(true);
    }

    void SetVolume(double volume)
    {
        if (output_)
            output_->setVolume(volume);
    }

private:
    QAudioSink* output_ = nullptr;
    QIODevice* io_ = nullptr;
    AudioOutputFormat format_{};
    int targetBufferMs_ = 0;
    std::string lastError_;
};

QtAudioOutputBackend::QtAudioOutputBackend()
{
    controlThread_ = new QThread();
    controlThread_->setObjectName("QtAudioOutputBackendThread");
    worker_ = new QtAudioOutputWorker();
    worker_->moveToThread(controlThread_);
    controlThread_->start();
}

QtAudioOutputBackend::~QtAudioOutputBackend()
{
    Close();
    ShutdownWorkerThread();
}

void QtAudioOutputBackend::ShutdownWorkerThread()
{
    QtAudioOutputWorker* worker = nullptr;
    QThread* controlThread = nullptr;
    {
        std::lock_guard<std::mutex> lock(mux_);
        worker = worker_;
        controlThread = controlThread_;
        worker_ = nullptr;
        controlThread_ = nullptr;
    }

    if (!worker || !controlThread)
        return;

    InvokeOnWorker(worker, controlThread, [worker]() {
        delete worker;
    });
    controlThread->quit();
    controlThread->wait();
    delete controlThread;
}

bool QtAudioOutputBackend::Open(int sampleRate, int sampleSize, int channels, double volume,
    int targetBufferMs)
{
    QtAudioOutputWorker* worker = nullptr;
    QThread* controlThread = nullptr;
    {
        std::lock_guard<std::mutex> lock(mux_);
        worker = worker_;
        controlThread = controlThread_;
    }

    bool opened = false;
    InvokeOnWorker(worker, controlThread, [&]() {
        opened = worker->Open(sampleRate, sampleSize, channels, volume, targetBufferMs);
    });
    return opened;
}

void QtAudioOutputBackend::Close()
{
    QtAudioOutputWorker* worker = nullptr;
    QThread* controlThread = nullptr;
    {
        std::lock_guard<std::mutex> lock(mux_);
        worker = worker_;
        controlThread = controlThread_;
    }

    InvokeOnWorker(worker, controlThread, [worker]() {
        worker->Close();
    });
}

void QtAudioOutputBackend::Clear()
{
    QtAudioOutputWorker* worker = nullptr;
    QThread* controlThread = nullptr;
    {
        std::lock_guard<std::mutex> lock(mux_);
        worker = worker_;
        controlThread = controlThread_;
    }

    InvokeOnWorker(worker, controlThread, [worker]() {
        worker->Clear();
    });
}

long long QtAudioOutputBackend::GetNoPlayMs(int sampleRate, int sampleSize, int channels) const
{
    QtAudioOutputWorker* worker = nullptr;
    QThread* controlThread = nullptr;
    {
        std::lock_guard<std::mutex> lock(mux_);
        worker = worker_;
        controlThread = controlThread_;
    }

    long long queuedMs = 0;
    InvokeOnWorker(worker, controlThread, [&]() {
        queuedMs = worker->GetNoPlayMs(sampleRate, sampleSize, channels);
    });
    return queuedMs;
}

bool QtAudioOutputBackend::Write(const unsigned char* data, int dataSize)
{
    if (!data || dataSize <= 0)
        return false;

    QtAudioOutputWorker* worker = nullptr;
    QThread* controlThread = nullptr;
    {
        std::lock_guard<std::mutex> lock(mux_);
        worker = worker_;
        controlThread = controlThread_;
    }

    bool written = false;
    const QByteArray bytes(reinterpret_cast<const char*>(data), dataSize);
    InvokeOnWorker(worker, controlThread, [&]() {
        written = worker->Write(bytes);
    });
    return written;
}

int QtAudioOutputBackend::GetFree() const
{
    QtAudioOutputWorker* worker = nullptr;
    QThread* controlThread = nullptr;
    {
        std::lock_guard<std::mutex> lock(mux_);
        worker = worker_;
        controlThread = controlThread_;
    }

    int freeBytes = 0;
    InvokeOnWorker(worker, controlThread, [&]() {
        freeBytes = worker->GetFree();
    });
    return freeBytes;
}

AudioOutputFormat QtAudioOutputBackend::GetOutputFormat() const
{
    QtAudioOutputWorker* worker = nullptr;
    QThread* controlThread = nullptr;
    {
        std::lock_guard<std::mutex> lock(mux_);
        worker = worker_;
        controlThread = controlThread_;
    }

    AudioOutputFormat format;
    InvokeOnWorker(worker, controlThread, [&]() {
        format = worker->GetOutputFormat();
    });
    return format;
}

std::string QtAudioOutputBackend::GetLastError() const
{
    QtAudioOutputWorker* worker = nullptr;
    QThread* controlThread = nullptr;
    {
        std::lock_guard<std::mutex> lock(mux_);
        worker = worker_;
        controlThread = controlThread_;
    }

    std::string error;
    InvokeOnWorker(worker, controlThread, [&]() {
        error = worker->GetLastError();
    });
    return error;
}

bool QtAudioOutputBackend::RecoverWriteDevice()
{
    QtAudioOutputWorker* worker = nullptr;
    QThread* controlThread = nullptr;
    {
        std::lock_guard<std::mutex> lock(mux_);
        worker = worker_;
        controlThread = controlThread_;
    }

    bool recovered = false;
    InvokeOnWorker(worker, controlThread, [&]() {
        recovered = worker->RecoverWriteDevice();
    });
    return recovered;
}

void QtAudioOutputBackend::SetPause(bool isPause)
{
    QtAudioOutputWorker* worker = nullptr;
    QThread* controlThread = nullptr;
    {
        std::lock_guard<std::mutex> lock(mux_);
        worker = worker_;
        controlThread = controlThread_;
    }

    InvokeOnWorker(worker, controlThread, [worker, isPause]() {
        worker->SetPause(isPause);
    });
}

void QtAudioOutputBackend::SetTargetBufferMs(int bufferMs)
{
    QtAudioOutputWorker* worker = nullptr;
    QThread* controlThread = nullptr;
    {
        std::lock_guard<std::mutex> lock(mux_);
        worker = worker_;
        controlThread = controlThread_;
    }

    InvokeOnWorker(worker, controlThread, [worker, bufferMs]() {
        worker->SetTargetBufferMs(bufferMs);
    });
}

void QtAudioOutputBackend::SetVolume(double volume)
{
    QtAudioOutputWorker* worker = nullptr;
    QThread* controlThread = nullptr;
    {
        std::lock_guard<std::mutex> lock(mux_);
        worker = worker_;
        controlThread = controlThread_;
    }

    InvokeOnWorker(worker, controlThread, [worker, volume]() {
        worker->SetVolume(volume);
    });
}

#include <mutex>

class CXAudioPlay : public AudioPlay
{
public:

    long long GetNoPlayMs() override
    {
        return backend_.GetNoPlayMs(sampleRate, sampleSize, channels);
    }

    void SetTargetBufferMs(int bufferMs) override
    {
        std::lock_guard<std::mutex> lock(mux_);
        targetBufferMs_ = bufferMs > 0 ? bufferMs : 0;
        backend_.SetTargetBufferMs(targetBufferMs_);
    }

    void Clear() override
    {
        backend_.Clear();
    }

    void Close() override
    {
        backend_.Close();
    }

    bool Open() override
    {
        const int targetBufferMs = [this]() {
            std::lock_guard<std::mutex> lock(mux_);
            return targetBufferMs_;
        }();
        const bool opened = backend_.Open(sampleRate, sampleSize, channels, volume_, targetBufferMs);
        if (!opened)
            return false;

        const AudioOutputFormat actualFormat = backend_.GetOutputFormat();
        if (actualFormat.sampleRate > 0)
            sampleRate = actualFormat.sampleRate;
        if (actualFormat.sampleSize > 0)
            sampleSize = actualFormat.sampleSize;
        if (actualFormat.channels > 0)
            channels = actualFormat.channels;
        if (actualFormat.sampleFormat != AudioOutputSampleFormat::Unknown)
            sampleFormat = actualFormat.sampleFormat;
        return true;
    }

    void SetPause(bool isPause) override
    {
        backend_.SetPause(isPause);
    }

    void SetVolume(double vol) override
    {
        if (vol < 0.0)
            vol = 0.0;
        if (vol > 1.0)
            vol = 1.0;
        volume_ = vol;
        backend_.SetVolume(vol);
    }

    bool RecoverWriteDevice() override
    {
        return backend_.RecoverWriteDevice();
    }

    bool Write(const unsigned char* data, int dataSize) override
    {
        return backend_.Write(data, dataSize);
    }

    int GetFree() override
    {
        return backend_.GetFree();
    }

    std::string GetLastError() const override
    {
        return backend_.GetLastError();
    }

private:
    QtAudioOutputBackend backend_;
    std::mutex mux_;
    double volume_ = 0.5;
    int targetBufferMs_ = 0;
};

AudioPlay* AudioPlay::Create()
{
    return new CXAudioPlay();
}

AudioPlay::AudioPlay() = default;
AudioPlay::~AudioPlay() = default;

AudioOutputBridge::~AudioOutputBridge()
{
    Close();
}

bool AudioOutputBridge::Open(int sampleRate, int channels, int targetBufferMs)
{
    std::lock_guard<std::mutex> lock(mux_);
    if (!output_)
        output_.reset(AudioPlay::Create());

    output_->sampleRate = sampleRate;
    output_->channels = channels > 0 ? channels : 2;
    output_->sampleSize = 16;
    output_->sampleFormat = AudioOutputSampleFormat::Int16;
    output_->SetTargetBufferMs(targetBufferMs);
    return output_->Open();
}

void AudioOutputBridge::Close()
{
    std::lock_guard<std::mutex> lock(mux_);
    if (output_)
    {
        output_->Close();
        output_.reset();
    }
}

void AudioOutputBridge::Clear()
{
    std::lock_guard<std::mutex> lock(mux_);
    if (output_)
        output_->Clear();
}

void AudioOutputBridge::SetPause(bool isPause)
{
    std::lock_guard<std::mutex> lock(mux_);
    if (output_)
        output_->SetPause(isPause);
}

void AudioOutputBridge::SetVolume(double volume)
{
    std::lock_guard<std::mutex> lock(mux_);
    if (output_)
        output_->SetVolume(volume);
}

void AudioOutputBridge::SetTargetBufferMs(int bufferMs)
{
    std::lock_guard<std::mutex> lock(mux_);
    if (output_)
        output_->SetTargetBufferMs(bufferMs);
}

bool AudioOutputBridge::RecoverWriteDevice()
{
    std::lock_guard<std::mutex> lock(mux_);
    if (!output_)
        return false;
    return output_->RecoverWriteDevice();
}

long long AudioOutputBridge::GetBufferedMs()
{
    std::lock_guard<std::mutex> lock(mux_);
    if (!output_)
        return 0;
    return output_->GetNoPlayMs();
}

int AudioOutputBridge::GetFree()
{
    std::lock_guard<std::mutex> lock(mux_);
    if (!output_)
        return 0;
    return output_->GetFree();
}

bool AudioOutputBridge::Write(const unsigned char* data, int dataSize)
{
    std::lock_guard<std::mutex> lock(mux_);
    if (!output_)
        return false;
    return output_->Write(data, dataSize);
}

int AudioOutputBridge::GetOutputSampleRate()
{
    std::lock_guard<std::mutex> lock(mux_);
    if (!output_)
        return 0;
    return output_->sampleRate;
}

int AudioOutputBridge::GetOutputChannels()
{
    std::lock_guard<std::mutex> lock(mux_);
    if (!output_)
        return 0;
    return output_->channels;
}

int AudioOutputBridge::GetOutputSampleSize()
{
    std::lock_guard<std::mutex> lock(mux_);
    if (!output_)
        return 0;
    return output_->sampleSize;
}

AudioOutputSampleFormat AudioOutputBridge::GetOutputSampleFormat()
{
    std::lock_guard<std::mutex> lock(mux_);
    if (!output_)
        return AudioOutputSampleFormat::Unknown;
    return output_->sampleFormat;
}

std::string AudioOutputBridge::GetLastError()
{
    std::lock_guard<std::mutex> lock(mux_);
    if (!output_)
        return std::string();
    return output_->GetLastError();
}

#include "../media/decode.h"
#include "../media/decode_thread.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>

namespace
{

double ClampPlaybackSpeed(double speed)
{
    if (speed < 0.5)
        return 0.5;
    if (speed > 4.0)
        return 4.0;
    return speed;
}

long long SteadyNowMs()
{
    return static_cast<long long>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}
}

AudioClockController::AudioClockController(AudioThreadState& state)
    : state_(state)
{
}

bool AudioClockController::UsesAggressiveLiveSync(int queueLimit) const
{
    const int targetMs = state_.targetOutputBufferMs.load();
    if (targetMs <= 0)
        return false;

    return targetMs <= 80 || queueLimit <= 8;
}

void AudioClockController::SetPlaybackSpeed(double speed)
{
    state_.playbackSpeed.store(ClampPlaybackSpeed(speed));
}

void AudioClockController::SetTargetOutputBufferMs(int bufferMs)
{
    state_.targetOutputBufferMs.store(bufferMs > 0 ? bufferMs : 0);
}

void AudioClockController::ResetLiveLatencyStats()
{
    state_.liveCatchUpCount.store(0);
    state_.lastLiveCatchUpMs.store(0);
    state_.lastQueuedFramePtsMs.store(-1);
    state_.progressiveThrottleCount.store(0);
    state_.progressiveThrottleActive.store(false);
}

void AudioClockController::SetPts(long long pts)
{
    state_.pts.store(pts);
}

long long AudioClockController::GetPts() const
{
    return state_.pts.load();
}

int AudioClockController::GetLiveCatchUpCount() const
{
    return state_.liveCatchUpCount.load();
}

int AudioClockController::GetProgressiveThrottleCount() const
{
    return state_.progressiveThrottleCount.load();
}

long long AudioClockController::OutputMsToMediaMs(long long outputMs) const
{
    if (outputMs <= 0)
        return 0;

    const double speed = ClampPlaybackSpeed(state_.playbackSpeed.load());
    return static_cast<long long>(std::llround(static_cast<double>(outputMs) * speed));
}

long long AudioClockController::SyncPtsToOutput(Decode* decode, AudioOutputBridge& outputBridge)
{
    if (!decode)
        return 0;

    const long long decodePts = decode->pts.load();
    if (decodePts <= 0)
        return 0;

    const long long bufferedOutputMs = outputBridge.GetBufferedMs();
    state_.pts.store(decodePts - OutputMsToMediaMs(bufferedOutputMs));
    return bufferedOutputMs;
}

int AudioClockController::ComputeProgressiveThrottleDelayMs(long long deviceBufferMs, int queueLimit)
{
    if (!UsesAggressiveLiveSync(queueLimit))
    {
        state_.progressiveThrottleActive.store(false);
        return 0;
    }

    const int targetMs = state_.targetOutputBufferMs.load();
    if (targetMs <= 0 || deviceBufferMs <= 0)
    {
        state_.progressiveThrottleActive.store(false);
        return 0;
    }

    const int deadbandMs = std::clamp(targetMs / 6, 4, 18);
    const long long backlogMs = deviceBufferMs - (targetMs + deadbandMs);
    if (backlogMs <= 0)
    {
        state_.progressiveThrottleActive.store(false);
        return 0;
    }

    if (!state_.progressiveThrottleActive.exchange(true))
        state_.progressiveThrottleCount.fetch_add(1);

    return std::clamp(static_cast<int>(backlogMs / 12), 1, 6);
}

bool AudioClockController::MaybeCatchUpToLive(DecodeThread& owner, Decode* decode,
    AudioOutputBridge& outputBridge, long long deviceBufferMs, int queueLimit)
{
    if (!UsesAggressiveLiveSync(queueLimit))
        return false;

    const int targetMs = state_.targetOutputBufferMs.load();
    if (targetMs <= 0 || deviceBufferMs <= 0)
        return false;

    const int queuePackets = owner.GetQueueSize();
    const int queueThreshold = std::max(2, owner.maxList - 1);
    const bool severeBacklog = deviceBufferMs > targetMs + 120;
    const bool queuePressure = queuePackets >= queueThreshold && deviceBufferMs > targetMs + 40;
    if (!severeBacklog && !queuePressure)
        return false;

    const long long nowMs = SteadyNowMs();
    const long long lastCatchUpMs = state_.lastLiveCatchUpMs.load();
    if (lastCatchUpMs > 0 && nowMs - lastCatchUpMs < 400)
        return false;

    owner.DecodeThread::Clear();
    outputBridge.Clear();
    if (decode)
        state_.pts.store(decode->pts.load());

    state_.progressiveThrottleActive.store(false);
    state_.lastLiveCatchUpMs.store(nowMs);
    state_.liveCatchUpCount.fetch_add(1);
    std::cout << "[live] audio catch-up: device=" << deviceBufferMs
              << "ms queue=" << queuePackets << "/" << owner.maxList
              << " target=" << targetMs << "ms" << std::endl;
    return true;
}

bool AudioClockController::ShouldDropNonMonotonicLiveFrame(long long framePtsMs, int queueLimit) const
{
    if (!UsesAggressiveLiveSync(queueLimit) || framePtsMs < 0)
        return false;

    const long long lastPtsMs = state_.lastQueuedFramePtsMs.load();
    return lastPtsMs >= 0 && framePtsMs <= lastPtsMs;
}

void AudioClockController::MarkQueuedFrame(long long framePtsMs)
{
    if (framePtsMs >= 0)
        state_.lastQueuedFramePtsMs.store(framePtsMs);
}

#include "../media/decode.h"
#include "../media/decode_thread.h"
#include "../../common/diagnostics/logger.h"
#include "../../features/asr/whisper_thread.h"

#include <QThread>

#include <iostream>
#include <memory>

extern "C" {
#include <libavcodec/packet.h>
#include <libavutil/frame.h>
}

namespace
{
double ClampPacketPumpPlaybackSpeed(double speed)
{
    if (speed < 0.5)
        return 0.5;
    if (speed > 4.0)
        return 4.0;
    return speed;
}
}

void AudioAsrBridge::SetWhisperThread(WhisperThread* thread)
{
    std::lock_guard<std::mutex> lock(mux_);
    whisperThread_ = thread;
}

bool AudioAsrBridge::WantsAudio() const
{
    std::lock_guard<std::mutex> lock(mux_);
    return whisperThread_ && whisperThread_->IsModelLoaded();
}

void AudioAsrBridge::PushAudio(const unsigned char* data, int bytes, int sampleRate, long long startMs)
{
    if (!data || bytes <= 0 || sampleRate <= 0 || startMs < 0)
        return;

    WhisperThread* thread = nullptr;
    {
        std::lock_guard<std::mutex> lock(mux_);
        thread = whisperThread_;
    }

    if (thread)
        thread->PushAudio(data, bytes, sampleRate, startMs);
}

AudioResampleStage::AudioResampleStage()
    : resample_(std::make_unique<Resample>())
{
}

AudioResampleStage::~AudioResampleStage()
{
    Close();
}

bool AudioResampleStage::Open(AVCodecParameters* parameters, int outputSampleRate, int outputChannels,
    AudioOutputSampleFormat outputSampleFormat)
{
    std::lock_guard<std::mutex> lock(mux_);
    if (!resample_)
        resample_ = std::make_unique<Resample>();
    return resample_->Open(parameters, outputSampleRate, outputChannels, outputSampleFormat, false);
}

void AudioResampleStage::Close()
{
    std::lock_guard<std::mutex> lock(mux_);
    if (resample_)
    {
        resample_->Close();
        resample_.reset();
    }
}

void AudioResampleStage::SetSpeed(double speed)
{
    std::lock_guard<std::mutex> lock(mux_);
    if (resample_)
        resample_->SetSpeed(ClampPacketPumpPlaybackSpeed(speed));
}

bool AudioResampleStage::ResampleFrame(AVFrame* frame, Decode* decode, unsigned char* pcm,
    unsigned char* asrPcm, AudioResampleResult& result)
{
    result = {};
    if (!frame)
        return false;

    long long framePtsMs = frame->pts;
    if (framePtsMs < 0 && decode)
        framePtsMs = decode->pts.load();

    std::lock_guard<std::mutex> lock(mux_);
    if (!resample_)
    {
        av_frame_free(&frame);
        return false;
    }

    result.framePtsMs = framePtsMs;
    result.pcmSize = resample_->ResampleFinal(frame, pcm, asrPcm, &result.asrSize);
    return true;
}

AudioPacketPump::AudioPacketPump(DecodeThread& owner, std::atomic<bool>& exitFlag,
    AudioThreadState& state, AudioOutputBridge& outputBridge,
    AudioResampleStage& resampleStage, AudioClockController& clockController,
    AudioAsrBridge& asrBridge)
    : owner_(owner)
    , exitFlag_(exitFlag)
    , state_(state)
    , outputBridge_(outputBridge)
    , resampleStage_(resampleStage)
    , clockController_(clockController)
    , asrBridge_(asrBridge)
{
}

void AudioPacketPump::Run()
{
    auto pcm = std::make_unique<unsigned char[]>(1024 * 1024 * 2);
    auto whisperPcm = std::make_unique<unsigned char[]>(1024 * 1024 * 2);
    int failCount = 0;
    int consecutiveWriteFailures = 0;

    while (!exitFlag_.load())
    {
        if (state_.isPause.load())
        {
            QThread::msleep(5);
            continue;
        }

        Decode* decode = owner_.GetDecode();
        if (!decode)
        {
            QThread::msleep(1);
            continue;
        }

        const long long deviceBufferMs = outputBridge_.GetBufferedMs();
        if (clockController_.MaybeCatchUpToLive(owner_, decode, outputBridge_, deviceBufferMs, owner_.maxList))
        {
            QThread::msleep(1);
            continue;
        }

        PacketEnvelope item = owner_.Pop();
        if (item.kind == PacketEnvelopeKind::Empty)
        {
            const long long idleBufferedMs = clockController_.SyncPtsToOutput(decode, outputBridge_);
            if (clockController_.MaybeCatchUpToLive(owner_, decode, outputBridge_, idleBufferedMs, owner_.maxList))
            {
                QThread::msleep(1);
                continue;
            }
            QThread::msleep(1);
            continue;
        }

        if (item.kind == PacketEnvelopeKind::Flush)
        {
            if (Decode* flushDecode = owner_.GetDecode())
                flushDecode->Clear();
            failCount = 0;
            continue;
        }

        if (item.generation != owner_.GetQueueGeneration()
            || item.serial != owner_.GetQueueSerial())
        {
            av_packet_free(&item.packet);
            continue;
        }

        const bool isDrain = item.kind == PacketEnvelopeKind::Drain;
        const bool sendOk = isDrain ? decode->SendDrain() : decode->Send(item.packet);
        if (item.packet)
            av_packet_free(&item.packet);
        if (!sendOk)
        {
            ++failCount;
            const int sleepMs = (failCount < 10) ? 1 : (failCount < 50) ? 10 : 50;
            QThread::msleep(sleepMs);
            continue;
        }

        failCount = 0;

        while (!exitFlag_.load())
        {
            AVFrame* frame = decode->Recv();
            if (!frame)
                break;

            if (item.generation != owner_.GetQueueGeneration()
                || item.serial != owner_.GetQueueSerial())
            {
                av_frame_free(&frame);
                continue;
            }

            const long long bufferedMs = clockController_.SyncPtsToOutput(decode, outputBridge_);

            AudioResampleResult result;
            if (!resampleStage_.ResampleFrame(frame, decode, pcm.get(), whisperPcm.get(), result))
                continue;

            if (clockController_.ShouldDropNonMonotonicLiveFrame(result.framePtsMs, owner_.maxList))
            {
                std::cout << "[live] drop non-monotonic audio frame: pts="
                          << result.framePtsMs << "ms last="
                          << state_.lastQueuedFramePtsMs.load() << "ms" << std::endl;
                continue;
            }

            if (result.asrSize > 0
                && outputBridge_.GetOutputSampleFormat() == AudioOutputSampleFormat::Int16
                && outputBridge_.GetOutputChannels() == 2)
            {
                asrBridge_.PushAudio(whisperPcm.get(), result.asrSize,
                    outputBridge_.GetOutputSampleRate(), result.framePtsMs);
            }

            if (clockController_.MaybeCatchUpToLive(owner_, decode, outputBridge_, bufferedMs, owner_.maxList))
                break;

            while (!exitFlag_.load())
            {
                if (result.pcmSize <= 0)
                    break;

                const long long bufferedMsNow = outputBridge_.GetBufferedMs();
                const int progressiveDelayMs =
                    clockController_.ComputeProgressiveThrottleDelayMs(bufferedMsNow, owner_.maxList);
                if (progressiveDelayMs > 0)
                {
                    QThread::msleep(static_cast<unsigned long>(progressiveDelayMs));
                    continue;
                }

                if (outputBridge_.GetFree() < result.pcmSize || state_.isPause.load())
                {
                    QThread::msleep(1);
                    continue;
                }

                if (!outputBridge_.Write(pcm.get(), result.pcmSize))
                {
                    const StreamPlaybackKind playbackKind = state_.playbackKind.load();
                    ++consecutiveWriteFailures;

                    if (playbackKind != StreamPlaybackKind::Live)
                    {
                        const bool recovered = outputBridge_.RecoverWriteDevice();
                        if (consecutiveWriteFailures >= 50)
                        {
                            Logger::Instance().Log(
                                LogLevel::Warning,
                                "audio",
                                "output.retry_recover",
                                "Recovered audio write device after repeated write failures",
                                {
                                    { "playback_kind", StreamPlaybackKindName(playbackKind) },
                                    { "pts_ms", std::to_string(result.framePtsMs) },
                                    { "consecutive_failures", std::to_string(consecutiveWriteFailures) },
                                    { "recovered", recovered ? "true" : "false" },
                                });
                            consecutiveWriteFailures = 0;
                        }
                        QThread::msleep(1);
                        continue;
                    }

                    const int resetThreshold = 3;
                    Logger::Instance().Log(
                        LogLevel::Warning,
                        "audio",
                        "output.drop_frame",
                        "Dropped current audio frame after output write failure",
                        {
                            { "playback_kind", StreamPlaybackKindName(playbackKind) },
                            { "pts_ms", std::to_string(result.framePtsMs) },
                            { "pcm_size", std::to_string(result.pcmSize) },
                            { "consecutive_failures", std::to_string(consecutiveWriteFailures) },
                            { "buffered_ms", std::to_string(outputBridge_.GetBufferedMs()) },
                            { "free_bytes", std::to_string(outputBridge_.GetFree()) },
                        });

                    if (consecutiveWriteFailures >= resetThreshold)
                    {
                        outputBridge_.Clear();
                        consecutiveWriteFailures = 0;
                    }
                    break;
                }

                consecutiveWriteFailures = 0;
                clockController_.MarkQueuedFrame(result.framePtsMs);
                break;
            }
        }
    }
}
