#include "logger.h"

#include <cstdio>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#include <DbgHelp.h>
#endif

namespace
{

    struct LoggerState
    {
        std::mutex mux;
        LoggerConfig config;
        std::ofstream file;
        bool initialized = false;
    };

    LoggerState& State()
    {
        static LoggerState state;
        return state;
    }

    const char* ToLevelText(LogLevel level)
    {
        switch (level)
        {
        case LogLevel::Debug:   return "debug";
        case LogLevel::Info:    return "info";
        case LogLevel::Warning: return "warning";
        case LogLevel::Error:   return "error";
        case LogLevel::Fatal:   return "fatal";
        }
        return "unknown";
    }

    int ToSeverityRank(LogLevel level)
    {
        switch (level)
        {
        case LogLevel::Debug:   return 0;
        case LogLevel::Info:    return 1;
        case LogLevel::Warning: return 2;
        case LogLevel::Error:   return 3;
        case LogLevel::Fatal:   return 4;
        }
        return 1;
    }

    LogLevel QtTypeToLevel(QtMsgType type)
    {
        switch (type)
        {
        case QtDebugMsg:    return LogLevel::Debug;
        case QtInfoMsg:     return LogLevel::Info;
        case QtWarningMsg:  return LogLevel::Warning;
        case QtCriticalMsg: return LogLevel::Error;
        case QtFatalMsg:    return LogLevel::Fatal;
        }
        return LogLevel::Info;
    }

    void RecordMetrics(LogLevel level)
    {
        switch (level)
        {
        case LogLevel::Debug:   MetricsRegistry::Instance().RecordDebugLog(); break;
        case LogLevel::Info:    MetricsRegistry::Instance().RecordInfoLog(); break;
        case LogLevel::Warning: MetricsRegistry::Instance().RecordWarningLog(); break;
        case LogLevel::Error:   MetricsRegistry::Instance().RecordErrorLog(); break;
        case LogLevel::Fatal:   MetricsRegistry::Instance().RecordFatalLog(); break;
        }
    }

    std::string EscapeJson(const std::string& input)
    {
        std::string output;
        output.reserve(input.size() + 16);
        for (const char ch : input)
        {
            switch (ch)
            {
            case '\\': output += "\\\\"; break;
            case '"':  output += "\\\""; break;
            case '\b': output += "\\b"; break;
            case '\f': output += "\\f"; break;
            case '\n': output += "\\n"; break;
            case '\r': output += "\\r"; break;
            case '\t': output += "\\t"; break;
            default:

                if (static_cast<unsigned char>(ch) < 0x20)
                {
                    std::ostringstream oss;
                    oss << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                        << static_cast<int>(static_cast<unsigned char>(ch));
                    output += oss.str();
                }
                else
                {
                    output += ch;
                }
                break;
            }
        }
        return output;
    }

    std::string IsoTimestampNow()
    {
        const auto now = std::chrono::system_clock::now();
        const std::time_t nowTime = std::chrono::system_clock::to_time_t(now);
        std::tm localTm{};
#ifdef _WIN32
        localtime_s(&localTm, &nowTime);
#else
        localtime_r(&nowTime, &localTm);
#endif
        std::ostringstream oss;
        oss << std::put_time(&localTm, "%Y-%m-%dT%H:%M:%S");
        return oss.str();
    }

    std::string FileTimestampNow()
    {
        const auto now = std::chrono::system_clock::now();
        const std::time_t nowTime = std::chrono::system_clock::to_time_t(now);
        std::tm localTm{};
#ifdef _WIN32
        localtime_s(&localTm, &nowTime);
#else
        localtime_r(&nowTime, &localTm);
#endif
        std::ostringstream oss;
        oss << std::put_time(&localTm, "%Y%m%d-%H%M%S");
        return oss.str();
    }

    unsigned long CurrentThreadIdValue()
    {
#ifdef _WIN32
        return static_cast<unsigned long>(::GetCurrentThreadId());
#else
        return static_cast<unsigned long>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
#endif
    }

    void PrintConsole(
        LogLevel level,
        const std::string& category,
        const std::string& event,
        const std::string& message,
        const std::vector<LogField>& fields)
    {

        std::fprintf(stderr, "[%s] %s/%s: %s",
            ToLevelText(level), category.c_str(), event.c_str(), message.c_str());

        for (const auto& field : fields)
            std::fprintf(stderr, " [%s=%s]", field.key.c_str(), field.value.c_str());
        std::fprintf(stderr, "\n");
    }

    bool ShouldEmit(LogLevel level, const LoggerConfig& config)
    {
        return ToSeverityRank(level) >= ToSeverityRank(config.minimumLevel);
    }

    std::string BuildJsonLine(
        LogLevel level,
        const std::string& category,
        const std::string& event,
        const std::string& message,
        const std::vector<LogField>& fields)
    {
        std::ostringstream oss;
        oss << "{"
            << "\"ts\":\"" << EscapeJson(IsoTimestampNow()) << "\","
            << "\"level\":\"" << ToLevelText(level) << "\","
            << "\"category\":\"" << EscapeJson(category) << "\","
            << "\"event\":\"" << EscapeJson(event) << "\","
            << "\"thread_id\":" << CurrentThreadIdValue() << ","
            << "\"message\":\"" << EscapeJson(message) << "\","
            << "\"fields\":{";
        for (std::size_t i = 0; i < fields.size(); ++i)
        {
            if (i > 0)
                oss << ",";
            oss << "\"" << EscapeJson(fields[i].key) << "\":\"" << EscapeJson(fields[i].value) << "\"";
        }
        oss << "}}";
        return oss.str();
    }

#ifdef _WIN32

    std::string CaptureStackTraceText(std::size_t framesToSkip, std::size_t maxFrames)
    {
        HANDLE process = ::GetCurrentProcess();

        static std::once_flag symInitOnce;
        std::call_once(symInitOnce, [process]() {
            ::SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME | SYMOPT_LOAD_LINES);
            ::SymInitialize(process, nullptr, TRUE);
            });

        void* frames[32]{};

        const USHORT captured = ::CaptureStackBackTrace(
            static_cast<DWORD>(framesToSkip),
            static_cast<DWORD>(std::min<std::size_t>(maxFrames, std::size(frames))),
            frames,
            nullptr);
        if (captured == 0)
            return {};

        std::ostringstream oss;
        for (USHORT i = 0; i < captured; ++i)
        {
            DWORD64 address = reinterpret_cast<DWORD64>(frames[i]);
            char symbolBuffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME] = {};
            auto* symbol = reinterpret_cast<SYMBOL_INFO*>(symbolBuffer);
            symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
            symbol->MaxNameLen = MAX_SYM_NAME;

            DWORD displacement = 0;
            IMAGEHLP_LINE64 lineInfo{};
            lineInfo.SizeOfStruct = sizeof(IMAGEHLP_LINE64);

            if (i > 0)
                oss << " | ";

            if (::SymFromAddr(process, address, nullptr, symbol))
            {
                oss << symbol->Name;

                if (::SymGetLineFromAddr64(process, address, &displacement, &lineInfo))
                    oss << "@" << lineInfo.FileName << ":" << lineInfo.LineNumber;
            }
            else
            {
                oss << "0x" << std::hex << address << std::dec;
            }
        }
        return oss.str();
    }
#endif
}

MetricsRegistry& MetricsRegistry::Instance()
{
    static MetricsRegistry instance;
    return instance;
}

MetricsRegistry::MetricsRegistry()
    : startTime_(std::chrono::steady_clock::now())
{
}

void MetricsRegistry::RecordDebugLog()
{
    logLinesTotal_.fetch_add(1);
    logDebugLines_.fetch_add(1);
}

void MetricsRegistry::RecordInfoLog()
{
    logLinesTotal_.fetch_add(1);
    logInfoLines_.fetch_add(1);
}

void MetricsRegistry::RecordWarningLog()
{
    logLinesTotal_.fetch_add(1);
    logWarningLines_.fetch_add(1);
}

void MetricsRegistry::RecordErrorLog()
{
    logLinesTotal_.fetch_add(1);
    logErrorLines_.fetch_add(1);
}

void MetricsRegistry::RecordFatalLog()
{
    logLinesTotal_.fetch_add(1);
    logFatalLines_.fetch_add(1);
}

void MetricsRegistry::RecordQtWarning()
{
    qtWarnings_.fetch_add(1);
}

void MetricsRegistry::RecordQtCritical()
{
    qtCriticals_.fetch_add(1);
}

void MetricsRegistry::RecordQtFatal()
{
    qtFatals_.fetch_add(1);
}

void MetricsRegistry::RecordFileWriteFailure()
{
    fileWriteFailures_.fetch_add(1);
}

void MetricsRegistry::RecordCrashHandlerInstalled()
{
    crashHandlerInstalled_.store(true);
}

void MetricsRegistry::RecordCrashDumpWritten()
{
    crashDumpsWritten_.fetch_add(1);
}

DiagnosticsMetricsSnapshot MetricsRegistry::GetSnapshot() const
{
    DiagnosticsMetricsSnapshot snapshot;
    snapshot.uptimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - startTime_).count();

    snapshot.logLinesTotal = logLinesTotal_.load();
    snapshot.logDebugLines = logDebugLines_.load();
    snapshot.logInfoLines = logInfoLines_.load();
    snapshot.logWarningLines = logWarningLines_.load();
    snapshot.logErrorLines = logErrorLines_.load();
    snapshot.logFatalLines = logFatalLines_.load();
    snapshot.qtWarnings = qtWarnings_.load();
    snapshot.qtCriticals = qtCriticals_.load();
    snapshot.qtFatals = qtFatals_.load();
    snapshot.fileWriteFailures = fileWriteFailures_.load();
    snapshot.crashDumpsWritten = crashDumpsWritten_.load();
    snapshot.crashHandlerInstalled = crashHandlerInstalled_.load();
    return snapshot;
}

Logger& Logger::Instance()
{
    static Logger logger;
    return logger;
}

bool Logger::Initialize(const LoggerConfig& config)
{
    LoggerState& state = State();
    std::lock_guard<std::mutex> lock(state.mux);
    state.config = config;

    if (state.config.fileEnabled)
    {
        try
        {

            std::filesystem::create_directories(state.config.logDirectory);

            const std::filesystem::path filePath = std::filesystem::path(state.config.logDirectory)
                / (state.config.filePrefix + "-" + FileTimestampNow() + ".jsonl");

            state.file.open(filePath, std::ios::out | std::ios::app);
            if (!state.file.is_open())
            {
                MetricsRegistry::Instance().RecordFileWriteFailure();
                state.config.fileEnabled = false;
            }
        }
        catch (...)
        {
            MetricsRegistry::Instance().RecordFileWriteFailure();
            state.config.fileEnabled = false;
        }
    }

    state.initialized = true;

    if (state.config.fileEnabled && state.file.is_open())
    {
        RecordMetrics(LogLevel::Info);
        state.file << BuildJsonLine(
            LogLevel::Info,
            "diag",
            "logger.init",
            "Logger initialized",
            { { "log_dir", state.config.logDirectory } }) << '\n';
        state.file.flush();
    }
    return true;
}

void Logger::Shutdown()
{
    LoggerState& state = State();
    std::lock_guard<std::mutex> lock(state.mux);
    if (state.file.is_open())
    {
        RecordMetrics(LogLevel::Info);

        state.file << BuildJsonLine(
            LogLevel::Info,
            "diag",
            "logger.shutdown",
            "Logger shutdown",
            {}) << '\n';
        state.file.flush();
        state.file.close();
    }
    state.initialized = false;
}

void Logger::Log(
    LogLevel level,
    const std::string& category,
    const std::string& event,
    const std::string& message,
    const std::vector<LogField>& fields)
{
    LoggerState& state = State();
    std::lock_guard<std::mutex> lock(state.mux);

    if (!ShouldEmit(level, state.config))
        return;

    RecordMetrics(level);

    if (state.config.consoleEnabled)
        PrintConsole(level, category, event, message, fields);

    if (!state.config.fileEnabled || !state.file.is_open())
        return;

    const std::string line = BuildJsonLine(level, category, event, message, fields);

    state.file << line << '\n';

    if (!state.file.good())
    {
        MetricsRegistry::Instance().RecordFileWriteFailure();
        state.file.clear();
    }
    else
    {
        state.file.flush();
    }
}

void Logger::HandleQtMessage(QtMsgType type, const QMessageLogContext& ctx, const QString& msg)
{
    const LogLevel level = QtTypeToLevel(type);

    if (type == QtWarningMsg)
        MetricsRegistry::Instance().RecordQtWarning();
    else if (type == QtCriticalMsg)
        MetricsRegistry::Instance().RecordQtCritical();
    else if (type == QtFatalMsg)
        MetricsRegistry::Instance().RecordQtFatal();

    std::vector<LogField> fields;

    if (ctx.category && ctx.category[0] != '\0')
        fields.push_back({ "qt_category", ctx.category });
    if (ctx.file && ctx.file[0] != '\0')
        fields.push_back({ "file", ctx.file });
    if (ctx.line > 0)
        fields.push_back({ "line", std::to_string(ctx.line) });
    if (ctx.function && ctx.function[0] != '\0')
        fields.push_back({ "function", ctx.function });

#ifdef _WIN32

    if (msg.contains("QObject::startTimer: Timers cannot be started from another thread"))
    {
        const std::string stack = CaptureStackTraceText(2, 16);
        if (!stack.empty())
            fields.push_back({ "stack", stack });
    }
#endif

    Log(level, "qt", "message", msg.toStdString(), fields);
}

#include <csignal>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>

#ifdef _WIN32

#define NOMINMAX
#include <windows.h>
#include <DbgHelp.h>
#endif

namespace
{

    struct CrashHandlerState
    {
        std::mutex mux;
        CrashHandlerConfig config;
        std::function<DiagnosticsCrashContext()> contextProvider;
        bool initialized = false;
    };

    CrashHandlerState& CrashState()
    {
        static CrashHandlerState state;
        return state;
    }

    std::string EscapeCrashJson(const std::string& input)
    {
        std::string output;
        output.reserve(input.size() + 16);
        for (const char ch : input)
        {
            switch (ch)
            {
            case '\\': output += "\\\\"; break;
            case '"':  output += "\\\""; break;
            case '\b': output += "\\b"; break;
            case '\f': output += "\\f"; break;
            case '\n': output += "\\n"; break;
            case '\r': output += "\\r"; break;
            case '\t': output += "\\t"; break;
            default:
                if (static_cast<unsigned char>(ch) < 0x20)
                {
                    std::ostringstream oss;
                    oss << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                        << static_cast<int>(static_cast<unsigned char>(ch));
                    output += oss.str();
                }
                else
                {
                    output += ch;
                }
                break;
            }
        }
        return output;
    }

    std::string CrashFileTimestampNow()
    {
        const auto now = std::chrono::system_clock::now();
        const std::time_t nowTime = std::chrono::system_clock::to_time_t(now);
        std::tm localTm{};
#ifdef _WIN32
        localtime_s(&localTm, &nowTime);
#else
        localtime_r(&nowTime, &localTm);
#endif
        std::ostringstream oss;
        oss << std::put_time(&localTm, "%Y%m%d-%H%M%S");
        return oss.str();
    }

    std::string BuildBaseName(const std::string& reason)
    {
#ifdef _WIN32
        const unsigned long pid = static_cast<unsigned long>(::GetCurrentProcessId());
#else
        const unsigned long pid = 0;
#endif
        return "myplayer-crash-" + CrashFileTimestampNow() + "-" + std::to_string(pid) + "-" + reason;
    }

    void WriteCrashSidecar(const std::filesystem::path& path, const DiagnosticsCrashContext& context)
    {
        std::ofstream file(path, std::ios::out | std::ios::trunc);
        if (!file.is_open()) return;

        file << "{\n";
        file << "  \"command_line\": \"" << EscapeCrashJson(context.commandLine) << "\",\n";

        file << "  \"session\": {\n";
        file << "    \"state\": \"" << EscapeCrashJson(StreamSessionStateName(context.session.state)) << "\",\n";
        file << "    \"is_paused\": " << (context.session.isPaused ? "true" : "false") << ",\n";
        file << "    \"is_complete\": " << (context.session.isComplete ? "true" : "false") << ",\n";
        file << "    \"is_live\": " << (context.session.isLiveStream ? "true" : "false") << ",\n";
        file << "    \"is_buffering\": " << (context.session.isBuffering ? "true" : "false") << ",\n";
        file << "    \"has_error\": " << (context.session.hasError ? "true" : "false") << ",\n";
        file << "    \"position_ms\": " << context.session.positionMs << ",\n";
        file << "    \"total_ms\": " << context.session.totalMs << ",\n";
        file << "    \"generation\": " << context.session.epoch.generation << ",\n";
        file << "    \"serial\": " << context.session.epoch.serial << ",\n";
        file << "    \"current_url\": \"" << EscapeCrashJson(context.session.currentUrl) << "\",\n";
        file << "    \"last_error\": \"" << EscapeCrashJson(context.session.lastError) << "\"\n";
        file << "  },\n";

        file << "  \"media\": {\n";
        file << "    \"video_width\": " << context.media.videoWidth << ",\n";
        file << "    \"video_height\": " << context.media.videoHeight << ",\n";
        file << "    \"video_fps_num\": " << context.media.videoFpsNum << ",\n";
        file << "    \"video_fps_den\": " << context.media.videoFpsDen << ",\n";
        file << "    \"bitrate\": " << context.media.bitrate << ",\n";
        file << "    \"audio_sample_rate\": " << context.media.audioSampleRate << ",\n";
        file << "    \"audio_channels\": " << context.media.audioChannels << "\n";
        file << "  },\n";

        file << "  \"stream_stats\": {\n";
        file << "    \"state\": \"" << EscapeCrashJson(StreamSessionStateName(context.stats.state)) << "\",\n";
        file << "    \"playback_kind\": \"" << EscapeCrashJson(StreamPlaybackKindName(context.stats.playbackKind)) << "\",\n";
        file << "    \"open_latency_ms\": " << context.stats.openLatencyMs << ",\n";
        file << "    \"reconnect_attempts\": " << context.stats.reconnectAttempts << ",\n";
        file << "    \"reconnect_successes\": " << context.stats.reconnectSuccesses << ",\n";
        file << "    \"buffering_events\": " << context.stats.bufferingEvents << ",\n";
        file << "    \"video_queue_packets\": " << context.stats.videoQueuePackets << ",\n";
        file << "    \"audio_queue_packets\": " << context.stats.audioQueuePackets << ",\n";
        file << "    \"dropped_video_packets\": " << context.stats.droppedVideoPackets << ",\n";
        file << "    \"dropped_audio_packets\": " << context.stats.droppedAudioPackets << ",\n";
        file << "    \"dropped_late_video_frames\": " << context.stats.droppedLateVideoFrames << ",\n";
        file << "    \"audio_catch_up_events\": " << context.stats.audioCatchUpEvents << ",\n";
        file << "    \"audio_throttle_events\": " << context.stats.audioThrottleEvents << ",\n";
        file << "    \"ai_gpu_queue_depth\": " << context.stats.aiGpuQueueDepth << ",\n";
        file << "    \"ai_cpu_queue_depth\": " << context.stats.aiCpuQueueDepth << ",\n";
        file << "    \"ai_completed_tasks\": " << context.stats.aiCompletedTasks << ",\n";
        file << "    \"ai_dropped_tasks\": " << context.stats.aiDroppedTasks << ",\n";
        file << "    \"ai_cancelled_tasks\": " << context.stats.aiCancelledTasks << ",\n";
        file << "    \"diag_uptime_ms\": " << context.stats.diagnosticsUptimeMs << ",\n";
        file << "    \"diag_log_lines\": " << context.stats.diagnosticsLogLines << ",\n";
        file << "    \"diag_warning_lines\": " << context.stats.diagnosticsWarningLines << ",\n";
        file << "    \"diag_error_lines\": " << context.stats.diagnosticsErrorLines << ",\n";
        file << "    \"diag_qt_warnings\": " << context.stats.diagnosticsQtWarnings << ",\n";
        file << "    \"diag_qt_criticals\": " << context.stats.diagnosticsQtCriticals << ",\n";
        file << "    \"diag_file_write_failures\": " << context.stats.diagnosticsFileWriteFailures << ",\n";
        file << "    \"diag_crash_dumps_written\": " << context.stats.diagnosticsCrashDumpsWritten << "\n";
        file << "  }\n";
        file << "}\n";
    }

#ifdef _WIN32

    void WriteCrashArtifacts(const std::string& reason, EXCEPTION_POINTERS* exceptionPointers)
    {
        CrashHandlerState& state = CrashState();
        std::lock_guard<std::mutex> lock(state.mux);
        if (!state.initialized || !state.config.enabled) return;

        try
        {
            std::filesystem::create_directories(state.config.dumpDirectory);
            const std::string baseName = BuildBaseName(reason);

            const std::filesystem::path dumpPath = std::filesystem::path(state.config.dumpDirectory) / (baseName + ".dmp");
            const std::filesystem::path jsonPath = std::filesystem::path(state.config.dumpDirectory) / (baseName + ".json");

            HANDLE fileHandle = ::CreateFileW(
                dumpPath.wstring().c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

            if (fileHandle != INVALID_HANDLE_VALUE)
            {

                MINIDUMP_EXCEPTION_INFORMATION dumpExceptionInfo{};
                dumpExceptionInfo.ThreadId = ::GetCurrentThreadId();
                dumpExceptionInfo.ExceptionPointers = exceptionPointers;
                dumpExceptionInfo.ClientPointers = FALSE;

                const BOOL wroteDump = ::MiniDumpWriteDump(
                    ::GetCurrentProcess(), ::GetCurrentProcessId(), fileHandle,
                    static_cast<MINIDUMP_TYPE>(MiniDumpWithThreadInfo | MiniDumpWithIndirectlyReferencedMemory),
                    exceptionPointers ? &dumpExceptionInfo : nullptr, nullptr, nullptr);

                ::CloseHandle(fileHandle);
                if (wroteDump)
                    MetricsRegistry::Instance().RecordCrashDumpWritten();
            }

            DiagnosticsCrashContext context;
            if (state.contextProvider)
                context = state.contextProvider();

            WriteCrashSidecar(jsonPath, context);
        }
        catch (...)
        {

        }
    }

    LONG WINAPI UnhandledExceptionFilterImpl(EXCEPTION_POINTERS* exceptionPointers)
    {

        WriteCrashArtifacts("seh", exceptionPointers);
        return EXCEPTION_EXECUTE_HANDLER;
    }
#endif

    void TerminateHandler()
    {
#ifdef _WIN32
        WriteCrashArtifacts("terminate", nullptr);
#endif

        std::_Exit(EXIT_FAILURE);
    }

    void SignalHandler(int signalValue)
    {
#ifdef _WIN32
        WriteCrashArtifacts("signal-" + std::to_string(signalValue), nullptr);
#endif
        std::_Exit(EXIT_FAILURE);
    }

}

CrashHandler& CrashHandler::Instance()
{
    static CrashHandler handler;
    return handler;
}

bool CrashHandler::Initialize(const CrashHandlerConfig& config,
    std::function<DiagnosticsCrashContext()> contextProvider)
{
    CrashHandlerState& state = CrashState();
    std::lock_guard<std::mutex> lock(state.mux);
    state.config = config;

    state.contextProvider = std::move(contextProvider);

    state.initialized = config.enabled;
    if (!state.initialized)
        return true;

    try
    {
        std::filesystem::create_directories(state.config.dumpDirectory);
    }
    catch (...) {}

#ifdef _WIN32

    ::SetUnhandledExceptionFilter(UnhandledExceptionFilterImpl);
#endif

    std::set_terminate(TerminateHandler);

    std::signal(SIGABRT, SignalHandler);
    std::signal(SIGSEGV, SignalHandler);

    MetricsRegistry::Instance().RecordCrashHandlerInstalled();
    Logger::Instance().Log(LogLevel::Info, "diag", "crash_handler.init", "Crash handler installed",
        { { "dump_dir", state.config.dumpDirectory } });

    return true;
}

void CrashHandler::Shutdown()
{
    CrashHandlerState& state = CrashState();
    std::lock_guard<std::mutex> lock(state.mux);
    state.contextProvider = {};
    state.initialized = false;

}
