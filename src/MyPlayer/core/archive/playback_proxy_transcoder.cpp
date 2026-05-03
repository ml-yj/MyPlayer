

#include "playback_proxy_transcoder.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QUuid>

namespace
{

QString ResolveFfmpegExecutable()
{
    const QString appDirExecutable = QDir(QCoreApplication::applicationDirPath()).filePath("ffmpeg.exe");
    if (QFileInfo::exists(appDirExecutable))
        return QDir::cleanPath(appDirExecutable);

    const QString discovered = QStandardPaths::findExecutable("ffmpeg");
    return discovered.trimmed();
}
}

bool PlaybackProxyTranscoder::Transcode(
    const PlaybackProxyTranscodeRequest& request,
    PlaybackProxyTranscodeResult* result,
    QString* errorMessage)
{
    if (!result)
    {
        if (errorMessage)
            *errorMessage = "Playback proxy result is null.";
        return false;
    }

    const QString sourcePath = QDir::cleanPath(request.sourceAbsolutePath.trimmed());
    if (sourcePath.isEmpty() || !QFileInfo::exists(sourcePath))
    {
        if (errorMessage)
            *errorMessage = "Playback source segment is missing.";
        return false;
    }

    if (request.sourceSegment.segmentId.trimmed().isEmpty()
        || request.sourceSegment.cameraId.trimmed().isEmpty()
        || !request.sourceSegment.startUtc.isValid()
        || !request.sourceSegment.endUtc.isValid())
    {
        if (errorMessage)
            *errorMessage = "Playback source segment metadata is incomplete.";
        return false;
    }

    const QString ffmpegExecutable = ResolveFfmpegExecutable();
    if (ffmpegExecutable.isEmpty())
    {
        if (errorMessage)
            *errorMessage = "ffmpeg executable is not available.";
        return false;
    }

    const QString codecProfile = request.codecProfile.trimmed().isEmpty()
        ? QStringLiteral("h264_aac_mp4")
        : request.codecProfile.trimmed().toLower();
    const QString proxyRelativePath = request.policy.BuildPlaybackProxyRelativePath(
        request.sourceSegment.cameraId,
        request.sourceSegment.startUtc,
        request.sourceSegment.endUtc,
        codecProfile);
    const QString proxyAbsolutePath = QDir(request.policy.archiveRootDir).filePath(proxyRelativePath);
    if (proxyRelativePath.isEmpty() || proxyAbsolutePath.isEmpty())
    {
        if (errorMessage)
            *errorMessage = "Failed to resolve playback proxy path.";
        return false;
    }

    QDir().mkpath(QFileInfo(proxyAbsolutePath).absolutePath());
    QFile::remove(proxyAbsolutePath);

    QStringList arguments;
    arguments
        << "-y"
        << "-hide_banner"
        << "-loglevel" << "error"
        << "-i" << sourcePath
        << "-map" << "0:v:0?"
        << "-map" << "0:a:0?";
    if (request.sourceSegment.hasVideo)
    {
        arguments
            << "-c:v" << "libx264"
            << "-preset" << "veryfast"
            << "-pix_fmt" << "yuv420p"
            << "-vf" << "scale=trunc(iw/2)*2:trunc(ih/2)*2";
    }
    if (request.sourceSegment.hasAudio)
    {
        arguments
            << "-c:a" << "aac"
            << "-b:a" << "128k";
    }
    arguments
        << "-movflags" << "+faststart"
        << proxyAbsolutePath;

    QProcess process;
    process.setProgram(ffmpegExecutable);
    process.setArguments(arguments);
    process.setWorkingDirectory(QFileInfo(ffmpegExecutable).absolutePath());
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start();

    if (!process.waitForStarted())
    {
        if (errorMessage)
            *errorMessage = "Failed to start ffmpeg for playback proxy generation.";
        return false;
    }
    process.closeWriteChannel();
    process.waitForFinished(-1);

    const QString stderrText = QString::fromUtf8(process.readAllStandardError()).trimmed();
    const QString stdoutText = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0 || !QFileInfo::exists(proxyAbsolutePath))
    {
        QFile::remove(proxyAbsolutePath);
        if (errorMessage)
        {
            *errorMessage = !stderrText.isEmpty()
                ? stderrText
                : (!stdoutText.isEmpty()
                    ? stdoutText
                    : QString("ffmpeg exited with code %1").arg(process.exitCode()));
        }
        return false;
    }

    PlaybackProxyTranscodeResult localResult;
    localResult.sourceAbsolutePath = sourcePath;
    localResult.proxyAbsolutePath = proxyAbsolutePath;
    localResult.proxyRecord.proxyId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    localResult.proxyRecord.sourceSegmentId = request.sourceSegment.segmentId;
    localResult.proxyRecord.cameraId = request.sourceSegment.cameraId;
    localResult.proxyRecord.startUtc = request.sourceSegment.startUtc;
    localResult.proxyRecord.endUtc = request.sourceSegment.endUtc;
    localResult.proxyRecord.relativePath = proxyRelativePath;
    localResult.proxyRecord.codecProfile = codecProfile;
    localResult.proxyRecord.container = QStringLiteral("mp4");
    localResult.proxyRecord.videoCodec = request.sourceSegment.hasVideo ? QStringLiteral("h264") : QString{};
    localResult.proxyRecord.audioCodec = request.sourceSegment.hasAudio ? QStringLiteral("aac") : QString{};
    localResult.proxyRecord.fileSizeBytes = QFileInfo(proxyAbsolutePath).size();
    *result = std::move(localResult);
    return true;
}
