

#include "subtitle_manager.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QtGlobal>

#include <algorithm>

namespace
{
constexpr long long kCueMergeGapMs = 200;
constexpr long long kCueDedupStartWindowMs = 300;
constexpr long long kCueDedupEndWindowMs = 500;
constexpr long long kDisplayToleranceMs = 120;

void TouchTrack(SubtitleTrack& track)
{
    ++track.revision;
}

QString NormalizeText(const QString& text)
{
    QString normalized = text;
    normalized.replace("\r\n", "\n");
    normalized.replace('\r', '\n');

    QStringList lines = normalized.split('\n');
    for (QString& line : lines)
        line = line.trimmed();

    while (!lines.isEmpty() && lines.front().isEmpty())
        lines.pop_front();
    while (!lines.isEmpty() && lines.back().isEmpty())
        lines.pop_back();

    return lines.join("\n").trimmed();
}

QString NormalizeAssText(QString text)
{
    text.replace("\r\n", "\n");
    text.replace('\r', '\n');
    text.replace('\n', "\\N");
    return text.trimmed();
}
}

class SubtitleTrackRegistry
{
public:
    void Clear();
    void ClearCues(SubtitleSourceType source);
    void RemoveTracks(SubtitleSourceType source);

    int EnsureTrack(SubtitleSourceType source, const QString& name, const QString& filePath = QString());

    const SubtitleTrack* FindTrack(int trackId) const;
    SubtitleTrack* FindTrack(int trackId);
    int FindTrackBySource(SubtitleSourceType source) const;
    const QVector<SubtitleTrack>& Tracks() const;

private:
    QVector<SubtitleTrack> tracks_;
    int nextTrackId_ = 1;
};

class SubtitleCueTimeline
{
public:
    bool AddCue(SubtitleTrackRegistry& registry, int trackId, const SubtitleCue& inputCue) const;
    QString TextAt(const SubtitleTrackRegistry& registry, int trackId, long long clockMs, long long offsetMs) const;
};

class SubtitleSrtCodec
{
public:
    int LoadSrt(SubtitleTrackRegistry& registry, const QString& path, QString* error = nullptr) const;
    bool ExportTrackToSrt(const SubtitleTrackRegistry& registry, int trackId, const QString& path, QString* error = nullptr) const;

private:
    bool ParseSrt(const QString& content, QVector<SubtitleCue>& cues, QString* error) const;
    long long ParseTimestamp(const QString& token, bool* ok) const;
    QString FormatTimestamp(long long ms) const;
};

void SubtitleTrackRegistry::Clear()
{
    tracks_.clear();
    nextTrackId_ = 1;
}

void SubtitleTrackRegistry::ClearCues(SubtitleSourceType source)
{
    for (SubtitleTrack& track : tracks_)
    {
        if (track.source != source)
            continue;
        if (!track.cues.isEmpty())
            TouchTrack(track);
        track.cues.clear();
    }
}

void SubtitleTrackRegistry::RemoveTracks(SubtitleSourceType source)
{
    auto it = std::remove_if(tracks_.begin(), tracks_.end(),
        [source](const SubtitleTrack& track) { return track.source == source; });
    tracks_.erase(it, tracks_.end());
}

int SubtitleTrackRegistry::EnsureTrack(SubtitleSourceType source, const QString& name, const QString& filePath)
{
    const QString normalizedPath = QFileInfo(filePath).exists()
        ? QFileInfo(filePath).absoluteFilePath()
        : filePath;

    for (SubtitleTrack& track : tracks_)
    {
        if (track.source != source)
            continue;

        if (!normalizedPath.isEmpty() && !track.filePath.isEmpty() &&
            QFileInfo(track.filePath).absoluteFilePath() == normalizedPath)
        {
            if (!name.isEmpty())
                track.name = name;
            return track.id;
        }

        if (source == SubtitleSourceType::Whisper)
        {
            if (!name.isEmpty())
                track.name = name;
            if (!normalizedPath.isEmpty())
                track.filePath = normalizedPath;
            return track.id;
        }
    }

    SubtitleTrack track;
    track.id = nextTrackId_++;
    track.source = source;
    track.name = name;
    track.filePath = normalizedPath;
    tracks_.push_back(track);
    return track.id;
}

const SubtitleTrack* SubtitleTrackRegistry::FindTrack(int trackId) const
{
    for (const SubtitleTrack& track : tracks_)
    {
        if (track.id == trackId)
            return &track;
    }
    return nullptr;
}

SubtitleTrack* SubtitleTrackRegistry::FindTrack(int trackId)
{
    for (SubtitleTrack& track : tracks_)
    {
        if (track.id == trackId)
            return &track;
    }
    return nullptr;
}

int SubtitleTrackRegistry::FindTrackBySource(SubtitleSourceType source) const
{
    for (const SubtitleTrack& track : tracks_)
    {
        if (track.source == source)
            return track.id;
    }
    return 0;
}

const QVector<SubtitleTrack>& SubtitleTrackRegistry::Tracks() const
{
    return tracks_;
}

bool SubtitleCueTimeline::AddCue(SubtitleTrackRegistry& registry, int trackId, const SubtitleCue& inputCue) const
{
    SubtitleTrack* track = registry.FindTrack(trackId);
    if (!track)
        return false;

    SubtitleCue cue = inputCue;
    cue.text = NormalizeText(cue.text);
    cue.assText = NormalizeAssText(cue.assText);
    if ((cue.text.isEmpty() && cue.assText.isEmpty()) || cue.endMs <= cue.startMs)
        return false;

    if (track->cues.isEmpty())
    {
        track->cues.push_back(cue);
        ++track->revision;
        return true;
    }

    SubtitleCue& last = track->cues.last();
    if (cue.startMs >= last.startMs)
    {
        if (last.text == cue.text && last.assText == cue.assText &&
            cue.startMs <= last.endMs + kCueMergeGapMs)
        {
            last.endMs = std::max(last.endMs, cue.endMs);
            ++track->revision;
            return true;
        }

        if (last.text == cue.text && last.assText == cue.assText &&
            qAbs(last.startMs - cue.startMs) <= kCueDedupStartWindowMs &&
            qAbs(last.endMs - cue.endMs) <= kCueDedupEndWindowMs)
        {
            return false;
        }

        track->cues.push_back(cue);
        ++track->revision;
        return true;
    }

    int insertIndex = 0;
    while (insertIndex < track->cues.size() && track->cues[insertIndex].startMs <= cue.startMs)
        ++insertIndex;
    track->cues.insert(insertIndex, cue);
    ++track->revision;
    return true;
}

QString SubtitleCueTimeline::TextAt(
    const SubtitleTrackRegistry& registry, int trackId, long long clockMs, long long offsetMs) const
{
    const SubtitleTrack* track = registry.FindTrack(trackId);
    if (!track)
        return {};

    const long long adjustedClockMs = clockMs - offsetMs;
    for (const SubtitleCue& cue : track->cues)
    {
        if (cue.endMs < adjustedClockMs - kDisplayToleranceMs)
            continue;
        if (cue.startMs <= adjustedClockMs + kDisplayToleranceMs &&
            cue.endMs >= adjustedClockMs - kDisplayToleranceMs)
        {
            return cue.text;
        }
        if (cue.startMs > adjustedClockMs + kDisplayToleranceMs)
            break;
    }

    return {};
}

int SubtitleSrtCodec::LoadSrt(SubtitleTrackRegistry& registry, const QString& path, QString* error) const
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        if (error)
            *error = QString("Failed to open subtitle file: %1").arg(path);
        return 0;
    }

    QVector<SubtitleCue> cues;
    if (!ParseSrt(QString::fromUtf8(file.readAll()), cues, error))
        return 0;

    QFileInfo info(path);
    const QString absolutePath = info.absoluteFilePath();
    const QString name = info.completeBaseName();
    const int trackId = registry.EnsureTrack(SubtitleSourceType::ExternalFile, name, absolutePath);
    SubtitleTrack* track = registry.FindTrack(trackId);
    if (!track)
    {
        if (error)
            *error = "Failed to create subtitle track.";
        return 0;
    }

    track->name = name;
    track->filePath = absolutePath;
    track->renderWithLibass = false;
    track->cues = cues;
    ++track->revision;
    return trackId;
}

bool SubtitleSrtCodec::ExportTrackToSrt(
    const SubtitleTrackRegistry& registry, int trackId, const QString& path, QString* error) const
{
    const SubtitleTrack* track = registry.FindTrack(trackId);
    if (!track)
    {
        if (error)
            *error = "Subtitle track not found.";
        return false;
    }

    if (track->cues.isEmpty())
    {
        if (error)
            *error = "Subtitle track is empty.";
        return false;
    }

    const QFileInfo outInfo(path);
    QDir().mkpath(outInfo.absolutePath());

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        if (error)
            *error = QString("Failed to write subtitle file: %1").arg(path);
        return false;
    }

    QString content;
    for (int i = 0; i < track->cues.size(); ++i)
    {
        const SubtitleCue& cue = track->cues[i];
        content += QString::number(i + 1) + "\r\n";
        content += FormatTimestamp(cue.startMs) + " --> " + FormatTimestamp(cue.endMs) + "\r\n";
        content += cue.text + "\r\n\r\n";
    }

    QByteArray bytes;
    bytes.append("\xEF\xBB\xBF", 3);
    bytes.append(content.toUtf8());
    file.write(bytes);
    return true;
}

bool SubtitleSrtCodec::ParseSrt(const QString& content, QVector<SubtitleCue>& cues, QString* error) const
{
    cues.clear();

    QString normalized = content;
    normalized.replace("\r\n", "\n");
    normalized.replace('\r', '\n');

    const QStringList lines = normalized.split('\n');
    int i = 0;
    while (i < lines.size())
    {
        while (i < lines.size() && lines[i].trimmed().isEmpty())
            ++i;
        if (i >= lines.size())
            break;

        bool isIndex = false;
        lines[i].trimmed().toInt(&isIndex);
        if (isIndex)
            ++i;
        if (i >= lines.size())
            break;

        const QString timingLine = lines[i].trimmed();
        const int arrowPos = timingLine.indexOf("-->");
        if (arrowPos < 0)
        {
            ++i;
            continue;
        }

        const QString startToken = timingLine.left(arrowPos).trimmed();
        const QString endToken = timingLine.mid(arrowPos + 3).trimmed().section(' ', 0, 0).trimmed();
        bool okStart = false;
        bool okEnd = false;
        const long long startMs = ParseTimestamp(startToken, &okStart);
        const long long endMs = ParseTimestamp(endToken, &okEnd);

        ++i;
        QStringList textLines;
        while (i < lines.size() && !lines[i].trimmed().isEmpty())
        {
            textLines.push_back(lines[i].trimmed());
            ++i;
        }

        if (!okStart || !okEnd || endMs <= startMs)
            continue;

        SubtitleCue cue;
        cue.startMs = startMs;
        cue.endMs = endMs;
        cue.text = NormalizeText(textLines.join("\n"));
        if (!cue.text.isEmpty())
            cues.push_back(cue);
    }

    std::sort(cues.begin(), cues.end(),
        [](const SubtitleCue& a, const SubtitleCue& b) {
            if (a.startMs != b.startMs)
                return a.startMs < b.startMs;
            return a.endMs < b.endMs;
        });

    if (cues.isEmpty())
    {
        if (error)
            *error = "No valid subtitle cues found in SRT file.";
        return false;
    }

    return true;
}

long long SubtitleSrtCodec::ParseTimestamp(const QString& token, bool* ok) const
{
    if (ok)
        *ok = false;

    const QString cleaned = token.trimmed().section(' ', 0, 0).trimmed();
    const QChar separator = cleaned.contains(',') ? QChar(',') : QChar('.');
    const QStringList timeParts = cleaned.split(separator);
    if (timeParts.size() != 2)
        return 0;

    const QStringList hms = timeParts[0].split(':');
    if (hms.size() != 3)
        return 0;

    bool okHours = false;
    bool okMinutes = false;
    bool okSeconds = false;
    bool okMillis = false;
    const int hours = hms[0].toInt(&okHours);
    const int minutes = hms[1].toInt(&okMinutes);
    const int seconds = hms[2].toInt(&okSeconds);
    QString millisText = timeParts[1];
    while (millisText.size() < 3)
        millisText.append('0');
    if (millisText.size() > 3)
        millisText = millisText.left(3);
    const int millis = millisText.toInt(&okMillis);

    if (!(okHours && okMinutes && okSeconds && okMillis))
        return 0;

    if (ok)
        *ok = true;

    return (((static_cast<long long>(hours) * 60LL) + minutes) * 60LL + seconds) * 1000LL + millis;
}

QString SubtitleSrtCodec::FormatTimestamp(long long ms) const
{
    ms = std::max(0LL, ms);

    const long long hours = ms / 3600000LL;
    ms %= 3600000LL;
    const long long minutes = ms / 60000LL;
    ms %= 60000LL;
    const long long seconds = ms / 1000LL;
    const long long millis = ms % 1000LL;

    return QString("%1:%2:%3,%4")
        .arg(hours, 2, 10, QChar('0'))
        .arg(minutes, 2, 10, QChar('0'))
        .arg(seconds, 2, 10, QChar('0'))
        .arg(millis, 3, 10, QChar('0'));
}

SubtitleManager::SubtitleManager()
    : registry_(std::make_unique<SubtitleTrackRegistry>())
    , cueTimeline_(std::make_unique<SubtitleCueTimeline>())
    , srtCodec_(std::make_unique<SubtitleSrtCodec>())
{
}

SubtitleManager::~SubtitleManager() = default;

void SubtitleManager::Clear()
{
    registry_->Clear();
}

void SubtitleManager::ClearCues(SubtitleSourceType source)
{
    registry_->ClearCues(source);
}

void SubtitleManager::RemoveTracks(SubtitleSourceType source)
{
    registry_->RemoveTracks(source);
}

int SubtitleManager::EnsureTrack(SubtitleSourceType source, const QString& name, const QString& filePath)
{
    return registry_->EnsureTrack(source, name, filePath);
}

int SubtitleManager::LoadSrt(const QString& path, QString* error)
{
    return srtCodec_->LoadSrt(*registry_, path, error);
}

bool SubtitleManager::AddCue(int trackId, const SubtitleCue& cue)
{
    return cueTimeline_->AddCue(*registry_, trackId, cue);
}

const SubtitleTrack* SubtitleManager::FindTrack(int trackId) const
{
    return registry_->FindTrack(trackId);
}

SubtitleTrack* SubtitleManager::FindTrack(int trackId)
{
    return registry_->FindTrack(trackId);
}

int SubtitleManager::FindTrackBySource(SubtitleSourceType source) const
{
    return registry_->FindTrackBySource(source);
}

const QVector<SubtitleTrack>& SubtitleManager::Tracks() const
{
    return registry_->Tracks();
}

QString SubtitleManager::FindMatchingSrt(const QString& mediaPath) const
{
    const QStringList matches = FindMatchingSrts(mediaPath);
    return matches.isEmpty() ? QString() : matches.first();
}

QStringList SubtitleManager::FindMatchingSrts(const QString& mediaPath) const
{
    QFileInfo mediaInfo(mediaPath);
    if (!mediaInfo.exists() || !mediaInfo.isFile())
        return {};

    QDir dir = mediaInfo.dir();
    const QString baseName = mediaInfo.completeBaseName();
    QStringList results;

    const QString exactPath = QFileInfo(dir.filePath(baseName + ".srt")).absoluteFilePath();
    if (QFileInfo::exists(exactPath))
        results.push_back(exactPath);

    const QStringList matches = dir.entryList(QStringList(baseName + "*.srt"), QDir::Files, QDir::Name);
    for (const QString& match : matches)
    {
        const QString absolutePath = QFileInfo(dir.filePath(match)).absoluteFilePath();
        if (!results.contains(absolutePath))
            results.push_back(absolutePath);
    }

    return results;
}

QString SubtitleManager::TextAt(int trackId, long long clockMs, long long offsetMs) const
{
    return cueTimeline_->TextAt(*registry_, trackId, clockMs, offsetMs);
}

bool SubtitleManager::ExportTrackToSrt(int trackId, const QString& path, QString* error) const
{
    return srtCodec_->ExportTrackToSrt(*registry_, trackId, path, error);
}
