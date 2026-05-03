
#include "application_bootstrap.h"

#include "../service/config_service.h"
#include "../ui/my_player.h"
#include "../../common/diagnostics/logger.h"
#include "../../common/diagnostics/logger.h"

#include <QCoreApplication>
#include <QDir>
#include <QLibraryInfo>
#include <QMetaType>
#include <QString>
#include <QTimer>
#include <QtWidgets/QApplication>

#include <cstdlib>

namespace
{
    struct StartupArguments
    {
        QString startupPath;
        bool startupDetector = false;
    };

    StartupArguments ParseStartupArguments(int argc, char* argv[])
    {
        StartupArguments arguments;
        for (int i = 1; i < argc; ++i)
        {
            const QString arg = QString::fromLocal8Bit(argv[i]);
            if (arg == "--detector-on")
            {
                arguments.startupDetector = true;
                continue;
            }

            if (arguments.startupPath.isEmpty())
                arguments.startupPath = arg;
        }

        return arguments;
    }

    void MessageFilter(QtMsgType type, const QMessageLogContext& ctx, const QString& msg)
    {

        Logger::Instance().HandleQtMessage(type, ctx, msg);

        if (type == QtFatalMsg)
            abort();
    }

    std::string ResolveDiagnosticsDirectory(const QString& configured, const QString& fallbackSuffix)
    {

        if (!configured.trimmed().isEmpty())
            return QDir::cleanPath(configured).toStdString();

        return QDir(QCoreApplication::applicationDirPath()).filePath(fallbackSuffix).toStdString();
    }

    LogLevel ResolveMinimumLogLevel(int configuredLevel)
    {
        switch (configuredLevel)
        {
        case 0: return LogLevel::Debug;
        case 2: return LogLevel::Warning;
        case 3: return LogLevel::Error;
        case 4: return LogLevel::Fatal;
        default: return LogLevel::Info;
        }
    }

    void ApplyStartupActions(MyPlayer& window, const StartupArguments& arguments)
    {

        if (!arguments.startupDetector && arguments.startupPath.isEmpty())
            return;

        QTimer::singleShot(0, &window, [&window, arguments]() {
            if (!arguments.startupPath.isEmpty())
                window.OpenMediaPath(arguments.startupPath);
            if (arguments.startupDetector)
                window.SetStartupDetectorEnabled(true);
            });
    }

    void ConfigureQtPluginPaths()
    {
        QStringList pluginRoots;
        const QString appDir = QCoreApplication::applicationDirPath();

        pluginRoots << appDir
            << QDir(appDir).filePath("plugins")
            << QDir(appDir).filePath("..");

        const QString qtPluginPath = QLibraryInfo::path(QLibraryInfo::PluginsPath);
        if (!qtPluginPath.trimmed().isEmpty())
            pluginRoots << QDir::cleanPath(qtPluginPath);

        pluginRoots.removeDuplicates();

        for (const QString& root : pluginRoots)
        {
            const QFileInfo info(root);
            if (!info.exists() || !info.isDir())
                continue;
            QCoreApplication::addLibraryPath(info.absoluteFilePath());
        }
    }
}

ApplicationBootstrap::ApplicationBootstrap(QApplication& app)
    : app_(app)
{
}

int ApplicationBootstrap::Run(int argc, char* argv[])
{

    RegisterMetaTypes();

    ConfigureQtPluginPaths();

    const StartupArguments startupArguments = ParseStartupArguments(argc, argv);

    ConfigService diagnosticsConfig;
    const DiagnosticsPreferences diagnosticsPreferences = diagnosticsConfig.LoadDiagnosticsPreferences();

    LoggerConfig loggerConfig;
    loggerConfig.consoleEnabled = diagnosticsPreferences.consoleLoggingEnabled;
    loggerConfig.fileEnabled = diagnosticsPreferences.fileLoggingEnabled;
#ifdef _DEBUG

    loggerConfig.fileEnabled = true;
#endif
    loggerConfig.minimumLevel = ResolveMinimumLogLevel(diagnosticsPreferences.minimumLogLevel);
    loggerConfig.logDirectory = ResolveDiagnosticsDirectory(diagnosticsPreferences.logDirectory, "logs");
    Logger::Instance().Initialize(loggerConfig);

    qInstallMessageHandler(MessageFilter);

    int result = 0;

    {
        MyPlayer window;

        CrashHandlerConfig crashConfig;
        crashConfig.enabled = diagnosticsPreferences.crashDumpEnabled;
        crashConfig.dumpDirectory = ResolveDiagnosticsDirectory(diagnosticsPreferences.crashDirectory, "crash");

        CrashHandler::Instance().Initialize(crashConfig, [&window]() {
            DiagnosticsCrashContext context;
            context.commandLine = QCoreApplication::arguments().join(' ').toStdString();
            context.session = window.GetPlaybackSessionSnapshot();
            context.media = window.GetPlaybackMediaSnapshot();
            context.stats = window.GetPlaybackStatsSnapshot();
            return context;
            });

        window.show();

        ApplyStartupActions(window, startupArguments);

        result = app_.exec();

        CrashHandler::Instance().Shutdown();
    }

    Logger::Instance().Shutdown();

    return result;
}

void ApplicationBootstrap::RegisterMetaTypes() const
{
    qRegisterMetaType<long long>("long long");
    qRegisterMetaType<quint64>("quint64");
    qRegisterMetaType<unsigned long long>("unsigned long long");
    qRegisterMetaType<qint64>("qint64");
    qRegisterMetaType<qulonglong>("qulonglong");
}
