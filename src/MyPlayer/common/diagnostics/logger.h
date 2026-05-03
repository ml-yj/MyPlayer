
#pragma once

#include <QtGlobal>
#include <QString>
#include "../../core/session/stream_config.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <string>
#include <vector>

class QMessageLogContext;

enum class LogLevel
{
    Debug,
    Info,
    Warning,
    Error,
    Fatal,
};

struct LogField
{
    std::string key;
    std::string value;
};

struct LoggerConfig
{
    bool consoleEnabled = true;
    bool fileEnabled = true;
    LogLevel minimumLevel = LogLevel::Info;
    std::string logDirectory;
    std::string filePrefix = "myplayer";
};

struct DiagnosticsMetricsSnapshot
{
    long long uptimeMs = 0;
    int logLinesTotal = 0;
    int logDebugLines = 0;
    int logInfoLines = 0;
    int logWarningLines = 0;
    int logErrorLines = 0;
    int logFatalLines = 0;
    int qtWarnings = 0;
    int qtCriticals = 0;
    int qtFatals = 0;
    int fileWriteFailures = 0;
    int crashDumpsWritten = 0;
    bool crashHandlerInstalled = false;
};

class MetricsRegistry
{
public:
    static MetricsRegistry& Instance();

    void RecordDebugLog();
    void RecordInfoLog();
    void RecordWarningLog();
    void RecordErrorLog();
    void RecordFatalLog();
    void RecordQtWarning();
    void RecordQtCritical();
    void RecordQtFatal();
    void RecordFileWriteFailure();
    void RecordCrashHandlerInstalled();
    void RecordCrashDumpWritten();

    DiagnosticsMetricsSnapshot GetSnapshot() const;

private:
    MetricsRegistry();

    std::chrono::steady_clock::time_point startTime_;
    std::atomic<int> logLinesTotal_{ 0 };
    std::atomic<int> logDebugLines_{ 0 };
    std::atomic<int> logInfoLines_{ 0 };
    std::atomic<int> logWarningLines_{ 0 };
    std::atomic<int> logErrorLines_{ 0 };
    std::atomic<int> logFatalLines_{ 0 };
    std::atomic<int> qtWarnings_{ 0 };
    std::atomic<int> qtCriticals_{ 0 };
    std::atomic<int> qtFatals_{ 0 };
    std::atomic<int> fileWriteFailures_{ 0 };
    std::atomic<int> crashDumpsWritten_{ 0 };
    std::atomic<bool> crashHandlerInstalled_{ false };
};

struct CrashHandlerConfig
{
    bool enabled = true;
    std::string dumpDirectory;
};

struct DiagnosticsCrashContext
{
    std::string commandLine;
    PlaybackSessionSnapshot session;
    PlaybackMediaSnapshot media;
    StreamStatsSnapshot stats;
};

class CrashHandler
{
public:
    static CrashHandler& Instance();
    bool Initialize(const CrashHandlerConfig& config,
        std::function<DiagnosticsCrashContext()> contextProvider);
    void Shutdown();

private:
    CrashHandler() = default;
};
class Logger
{
public:

    static Logger& Instance();

    bool Initialize(const LoggerConfig& config);

    void Shutdown();

    void Log(
        LogLevel level,
        const std::string& category,
        const std::string& event,
        const std::string& message,
        const std::vector<LogField>& fields = {});

    void HandleQtMessage(QtMsgType type, const QMessageLogContext& ctx, const QString& msg);

private:

    Logger() = default;
};
