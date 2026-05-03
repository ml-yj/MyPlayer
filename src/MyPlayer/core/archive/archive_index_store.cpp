
#include "archive_index_store.h"

#include <QByteArray>
#include <QDir>
#include <QMap>
#include <QSqlError>
#include <QSqlQuery>
#include <QTime>
#include <QTimeZone>
#include <QVariant>
#include <QUuid>

namespace
{

QString SqlErrorText(const QSqlQuery& query)
{
    return query.lastError().text();
}

QString SqlErrorText(const QSqlDatabase& database)
{
    return database.lastError().text();
}
}

ArchiveIndexStore::ArchiveIndexStore() = default;

ArchiveIndexStore::~ArchiveIndexStore()
{
    Close();
}

bool ArchiveIndexStore::Open(const ArchivePathPolicy& policy, QString* errorMessage)
{
    Close();

    const QString databasePath = policy.DatabaseFilePath();
    if (databasePath.trimmed().isEmpty())
    {
        if (errorMessage)
            *errorMessage = "Archive database path is empty.";
        return false;
    }

    QDir().mkpath(QFileInfo(databasePath).absolutePath());
    connectionName_ = QString("archive_index_%1")
        .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    database_ = QSqlDatabase::addDatabase("QSQLITE", connectionName_);
    database_.setDatabaseName(databasePath);
    if (!database_.open())
    {
        if (errorMessage)
            *errorMessage = SqlErrorText(database_);
        Close();
        return false;
    }

    return EnsureSchema(errorMessage);
}

void ArchiveIndexStore::Close()
{
    if (connectionName_.isEmpty())
        return;

    const QString connectionName = connectionName_;
    if (database_.isValid())
        database_.close();
    database_ = QSqlDatabase();
    QSqlDatabase::removeDatabase(connectionName);
    connectionName_.clear();
}

bool ArchiveIndexStore::IsOpen() const
{
    return database_.isValid() && database_.isOpen();
}

bool ArchiveIndexStore::EnsureSchema(QString* errorMessage)
{
    return ExecuteStatement(
               "CREATE TABLE IF NOT EXISTS segments ("
               "segment_id TEXT PRIMARY KEY,"
               "camera_id TEXT NOT NULL,"
               "source_url TEXT,"
               "start_utc_ms INTEGER NOT NULL,"
               "end_utc_ms INTEGER NOT NULL,"
               "relative_path TEXT NOT NULL,"
               "container TEXT,"
               "video_codec TEXT,"
               "audio_codec TEXT,"
               "file_size_bytes INTEGER NOT NULL DEFAULT 0,"
               "has_video INTEGER NOT NULL DEFAULT 1,"
               "has_audio INTEGER NOT NULL DEFAULT 1,"
               "created_utc_ms INTEGER NOT NULL"
               ")",
               errorMessage)
        && ExecuteStatement(
               "CREATE INDEX IF NOT EXISTS idx_segments_camera_start "
               "ON segments(camera_id, start_utc_ms)",
               errorMessage)
        && ExecuteStatement(
               "CREATE TABLE IF NOT EXISTS playback_proxies ("
               "proxy_id TEXT PRIMARY KEY,"
               "source_segment_id TEXT NOT NULL UNIQUE,"
               "camera_id TEXT NOT NULL,"
               "start_utc_ms INTEGER NOT NULL,"
               "end_utc_ms INTEGER NOT NULL,"
               "relative_path TEXT NOT NULL,"
               "codec_profile TEXT NOT NULL,"
               "container TEXT NOT NULL DEFAULT 'mp4',"
               "video_codec TEXT,"
               "audio_codec TEXT,"
               "file_size_bytes INTEGER NOT NULL DEFAULT 0,"
               "created_utc_ms INTEGER NOT NULL,"
               "FOREIGN KEY(source_segment_id) REFERENCES segments(segment_id)"
               ")",
               errorMessage)
        && ExecuteStatement(
               "CREATE INDEX IF NOT EXISTS idx_playback_proxies_source "
               "ON playback_proxies(source_segment_id)",
               errorMessage)
        && ExecuteStatement(
               "CREATE TABLE IF NOT EXISTS day_summaries ("
               "camera_id TEXT NOT NULL,"
               "day TEXT NOT NULL,"
               "has_media INTEGER NOT NULL DEFAULT 0,"
               "has_alarm INTEGER NOT NULL DEFAULT 0,"
               "segment_count INTEGER NOT NULL DEFAULT 0,"
               "first_utc_ms INTEGER,"
               "last_utc_ms INTEGER,"
               "PRIMARY KEY(camera_id, day)"
               ")",
               errorMessage)
        && ExecuteStatement(
               "CREATE TABLE IF NOT EXISTS events ("
               "event_id TEXT PRIMARY KEY,"
               "camera_id TEXT NOT NULL,"
               "occurred_utc_ms INTEGER NOT NULL,"
               "type TEXT NOT NULL,"
               "severity INTEGER NOT NULL,"
               "segment_id TEXT,"
               "payload_json TEXT"
               ")",
               errorMessage)
        && ExecuteStatement(
               "CREATE INDEX IF NOT EXISTS idx_events_camera_time "
               "ON events(camera_id, occurred_utc_ms)",
               errorMessage);
}

bool ArchiveIndexStore::ExecuteStatement(const QString& sql, QString* errorMessage) const
{
    QSqlQuery query(database_);
    if (!query.exec(sql))
    {
        if (errorMessage)
            *errorMessage = SqlErrorText(query);
        return false;
    }
    return true;
}

ArchiveSegmentRecord ArchiveIndexStore::BuildSegmentRecordFromQuery(const QSqlQuery& query, int offset)
{
    ArchiveSegmentRecord record;
    record.segmentId = query.value(offset + 0).toString();
    record.cameraId = query.value(offset + 1).toString();
    record.sourceUrl = query.value(offset + 2).toString();
    record.startUtc = FromUtcMs(query.value(offset + 3).toLongLong());
    record.endUtc = FromUtcMs(query.value(offset + 4).toLongLong());
    record.relativePath = query.value(offset + 5).toString();
    record.container = query.value(offset + 6).toString();
    record.videoCodec = query.value(offset + 7).toString();
    record.audioCodec = query.value(offset + 8).toString();
    record.fileSizeBytes = query.value(offset + 9).toLongLong();
    record.hasVideo = query.value(offset + 10).toInt() != 0;
    record.hasAudio = query.value(offset + 11).toInt() != 0;
    record.playbackProxyId = query.value(offset + 12).toString();
    record.playbackProxyRelativePath = query.value(offset + 13).toString();
    record.playbackProxyCodecProfile = query.value(offset + 14).toString();
    record.playbackProxyContainer = query.value(offset + 15).toString();
    record.playbackProxyVideoCodec = query.value(offset + 16).toString();
    record.playbackProxyAudioCodec = query.value(offset + 17).toString();
    record.playbackProxyFileSizeBytes = query.value(offset + 18).toLongLong();
    record.playbackProxyReady =
        !record.playbackProxyId.trimmed().isEmpty() && !record.playbackProxyRelativePath.trimmed().isEmpty();
    return record;
}

bool ArchiveIndexStore::RefreshDaySummary(
    const QString& cameraId, const QDate& day, QString* errorMessage)
{
    if (!day.isValid())
        return true;

    const QDateTime begin(day.startOfDay(QTimeZone(QByteArrayLiteral("UTC"))));
    const QDateTime end(begin.addDays(1));

    QSqlQuery segmentQuery(database_);
    segmentQuery.prepare(
        "SELECT COUNT(*), MIN(start_utc_ms), MAX(end_utc_ms) "
        "FROM segments "
        "WHERE camera_id = :camera_id AND start_utc_ms >= :begin_utc_ms AND start_utc_ms < :end_utc_ms");
    segmentQuery.bindValue(":camera_id", cameraId);
    segmentQuery.bindValue(":begin_utc_ms", ToUtcMs(begin));
    segmentQuery.bindValue(":end_utc_ms", ToUtcMs(end));
    if (!segmentQuery.exec() || !segmentQuery.next())
    {
        if (errorMessage)
            *errorMessage = SqlErrorText(segmentQuery);
        return false;
    }

    const int segmentCount = segmentQuery.value(0).toInt();
    const bool hasMedia = segmentCount > 0;
    const qint64 firstUtcMs = segmentQuery.value(1).toLongLong();
    const qint64 lastUtcMs = segmentQuery.value(2).toLongLong();

    QSqlQuery eventQuery(database_);
    eventQuery.prepare(
        "SELECT COUNT(*) "
        "FROM events "
        "WHERE camera_id = :camera_id AND occurred_utc_ms >= :begin_utc_ms AND occurred_utc_ms < :end_utc_ms "
        "AND severity >= :alarm_severity");
    eventQuery.bindValue(":camera_id", cameraId);
    eventQuery.bindValue(":begin_utc_ms", ToUtcMs(begin));
    eventQuery.bindValue(":end_utc_ms", ToUtcMs(end));
    eventQuery.bindValue(":alarm_severity", SeverityToInt(ArchiveEventSeverity::Alarm));
    if (!eventQuery.exec() || !eventQuery.next())
    {
        if (errorMessage)
            *errorMessage = SqlErrorText(eventQuery);
        return false;
    }

    const bool hasAlarm = eventQuery.value(0).toInt() > 0;

    QSqlQuery upsertQuery(database_);
    upsertQuery.prepare(
        "INSERT INTO day_summaries (camera_id, day, has_media, has_alarm, segment_count, first_utc_ms, last_utc_ms) "
        "VALUES (:camera_id, :day, :has_media, :has_alarm, :segment_count, :first_utc_ms, :last_utc_ms) "
        "ON CONFLICT(camera_id, day) DO UPDATE SET "
        "has_media = excluded.has_media, "
        "has_alarm = excluded.has_alarm, "
        "segment_count = excluded.segment_count, "
        "first_utc_ms = excluded.first_utc_ms, "
        "last_utc_ms = excluded.last_utc_ms");
    upsertQuery.bindValue(":camera_id", cameraId);
    upsertQuery.bindValue(":day", day.toString(Qt::ISODate));
    upsertQuery.bindValue(":has_media", hasMedia ? 1 : 0);
    upsertQuery.bindValue(":has_alarm", hasAlarm ? 1 : 0);
    upsertQuery.bindValue(":segment_count", segmentCount);
    upsertQuery.bindValue(":first_utc_ms", hasMedia ? QVariant::fromValue(firstUtcMs) : QVariant(QMetaType::fromType<qint64>()));
    upsertQuery.bindValue(":last_utc_ms", hasMedia ? QVariant::fromValue(lastUtcMs) : QVariant(QMetaType::fromType<qint64>()));
    if (!upsertQuery.exec())
    {
        if (errorMessage)
            *errorMessage = SqlErrorText(upsertQuery);
        return false;
    }
    return true;
}

qint64 ArchiveIndexStore::ToUtcMs(const QDateTime& value)
{
    return value.toUTC().toMSecsSinceEpoch();
}

QDateTime ArchiveIndexStore::FromUtcMs(qint64 value)
{
    if (value <= 0)
        return {};
    return QDateTime::fromMSecsSinceEpoch(value, QTimeZone(QByteArrayLiteral("UTC")));
}

int ArchiveIndexStore::SeverityToInt(ArchiveEventSeverity severity)
{
    return static_cast<int>(severity);
}

ArchiveEventSeverity ArchiveIndexStore::SeverityFromInt(int severity)
{
    switch (severity)
    {
    case 2: return ArchiveEventSeverity::Alarm;
    case 1: return ArchiveEventSeverity::Warning;
    default: return ArchiveEventSeverity::Info;
    }
}

QList<ArchiveDaySummary> ArchiveIndexStore::QueryDays(
    const QString& cameraId, const QDate& month, QString* errorMessage) const
{
    QList<ArchiveDaySummary> result;
    if (!IsOpen() || !month.isValid())
        return result;

    const QDate monthStart(month.year(), month.month(), 1);
    const QDate monthEnd = monthStart.addMonths(1).addDays(-1);
    const QDate utcQueryStart = monthStart.addDays(-1);
    const QDate utcQueryEnd = monthEnd.addDays(1);

    QSqlQuery query(database_);
    query.prepare(
        "SELECT camera_id, day, has_media, has_alarm, segment_count, first_utc_ms, last_utc_ms "
        "FROM day_summaries "
        "WHERE camera_id = :camera_id AND day >= :day_begin AND day <= :day_end "
        "ORDER BY day ASC");
    query.bindValue(":camera_id", cameraId);
    query.bindValue(":day_begin", utcQueryStart.toString(Qt::ISODate));
    query.bindValue(":day_end", utcQueryEnd.toString(Qt::ISODate));
    if (!query.exec())
    {
        if (errorMessage)
            *errorMessage = SqlErrorText(query);
        return result;
    }

    QMap<QDate, ArchiveDaySummary> byLocalDay;
    while (query.next())
    {
        ArchiveDaySummary summary;
        summary.cameraId = query.value(0).toString();
        summary.day = QDate::fromString(query.value(1).toString(), Qt::ISODate);
        summary.hasMedia = query.value(2).toInt() != 0;
        summary.hasAlarm = query.value(3).toInt() != 0;
        summary.segmentCount = query.value(4).toInt();
        summary.firstUtc = FromUtcMs(query.value(5).toLongLong());
        summary.lastUtc = FromUtcMs(query.value(6).toLongLong());

        QDate localDay = summary.firstUtc.isValid()
            ? summary.firstUtc.toLocalTime().date()
            : (summary.lastUtc.isValid() ? summary.lastUtc.toLocalTime().date() : summary.day);
        if (!localDay.isValid()
            || localDay.year() != month.year()
            || localDay.month() != month.month())
        {
            continue;
        }

        ArchiveDaySummary& aggregate = byLocalDay[localDay];
        if (!aggregate.day.isValid())
        {
            aggregate.cameraId = summary.cameraId;
            aggregate.day = localDay;
        }
        aggregate.hasMedia = aggregate.hasMedia || summary.hasMedia;
        aggregate.hasAlarm = aggregate.hasAlarm || summary.hasAlarm;
        aggregate.segmentCount += summary.segmentCount;
        if (summary.firstUtc.isValid()
            && (!aggregate.firstUtc.isValid() || summary.firstUtc < aggregate.firstUtc))
        {
            aggregate.firstUtc = summary.firstUtc;
        }
        if (summary.lastUtc.isValid()
            && (!aggregate.lastUtc.isValid() || summary.lastUtc > aggregate.lastUtc))
        {
            aggregate.lastUtc = summary.lastUtc;
        }
    }

    for (auto it = byLocalDay.cbegin(); it != byLocalDay.cend(); ++it)
        result.push_back(it.value());
    return result;
}

QList<ArchiveSegmentRecord> ArchiveIndexStore::QuerySegments(
    const QString& cameraId, const QDate& day, QString* errorMessage) const
{
    QList<ArchiveSegmentRecord> result;
    if (!IsOpen() || !day.isValid())
        return result;

    const QTimeZone localZone = QTimeZone::systemTimeZone();
    const QDateTime begin(QDateTime(day, QTime(0, 0), localZone).toUTC());
    const QDateTime end(begin.addDays(1));

    QSqlQuery query(database_);
    query.prepare(
        "SELECT s.segment_id, s.camera_id, s.source_url, s.start_utc_ms, s.end_utc_ms, s.relative_path, "
        "s.container, s.video_codec, s.audio_codec, s.file_size_bytes, s.has_video, s.has_audio, "
        "p.proxy_id AS proxy_id, p.relative_path AS proxy_relative_path, "
        "p.codec_profile AS proxy_codec_profile, p.container AS proxy_container, "
        "p.video_codec AS proxy_video_codec, p.audio_codec AS proxy_audio_codec, "
        "p.file_size_bytes AS proxy_file_size_bytes "
        "FROM segments s "
        "LEFT JOIN playback_proxies p ON p.source_segment_id = s.segment_id "
        "WHERE s.camera_id = :camera_id AND s.start_utc_ms >= :begin_utc_ms AND s.start_utc_ms < :end_utc_ms "
        "ORDER BY s.start_utc_ms ASC");
    query.bindValue(":camera_id", cameraId);
    query.bindValue(":begin_utc_ms", ToUtcMs(begin));
    query.bindValue(":end_utc_ms", ToUtcMs(end));
    if (!query.exec())
    {
        if (errorMessage)
            *errorMessage = SqlErrorText(query);
        return result;
    }

    while (query.next())
        result.push_back(BuildSegmentRecordFromQuery(query));
    return result;
}

QList<ArchiveSegmentRecord> ArchiveIndexStore::QueryRecentSegments(
    const QString& cameraId,
    int limit,
    QString* errorMessage) const
{
    QList<ArchiveSegmentRecord> result;
    if (!IsOpen() || cameraId.trimmed().isEmpty())
        return result;

    QSqlQuery query(database_);
    query.prepare(
        "SELECT s.segment_id, s.camera_id, s.source_url, s.start_utc_ms, s.end_utc_ms, s.relative_path, "
        "s.container, s.video_codec, s.audio_codec, s.file_size_bytes, s.has_video, s.has_audio, "
        "p.proxy_id AS proxy_id, p.relative_path AS proxy_relative_path, "
        "p.codec_profile AS proxy_codec_profile, p.container AS proxy_container, "
        "p.video_codec AS proxy_video_codec, p.audio_codec AS proxy_audio_codec, "
        "p.file_size_bytes AS proxy_file_size_bytes "
        "FROM segments s "
        "LEFT JOIN playback_proxies p ON p.source_segment_id = s.segment_id "
        "WHERE s.camera_id = :camera_id "
        "ORDER BY s.start_utc_ms DESC "
        "LIMIT :limit");
    query.bindValue(":camera_id", cameraId);
    query.bindValue(":limit", std::max(1, limit));
    if (!query.exec())
    {
        if (errorMessage)
            *errorMessage = SqlErrorText(query);
        return result;
    }

    while (query.next())
        result.push_back(BuildSegmentRecordFromQuery(query));
    return result;
}

QList<ArchiveEventRecord> ArchiveIndexStore::QueryEvents(
    const QString& cameraId, const QDate& day, QString* errorMessage) const
{
    QList<ArchiveEventRecord> result;
    if (!IsOpen() || !day.isValid())
        return result;

    const QTimeZone localZone = QTimeZone::systemTimeZone();
    const QDateTime begin(QDateTime(day, QTime(0, 0), localZone).toUTC());
    const QDateTime end(begin.addDays(1));

    QSqlQuery query(database_);
    query.prepare(
        "SELECT event_id, camera_id, occurred_utc_ms, type, severity, segment_id, payload_json "
        "FROM events "
        "WHERE camera_id = :camera_id AND occurred_utc_ms >= :begin_utc_ms AND occurred_utc_ms < :end_utc_ms "
        "ORDER BY occurred_utc_ms ASC");
    query.bindValue(":camera_id", cameraId);
    query.bindValue(":begin_utc_ms", ToUtcMs(begin));
    query.bindValue(":end_utc_ms", ToUtcMs(end));
    if (!query.exec())
    {
        if (errorMessage)
            *errorMessage = SqlErrorText(query);
        return result;
    }

    while (query.next())
    {
        ArchiveEventRecord record;
        record.eventId = query.value(0).toString();
        record.cameraId = query.value(1).toString();
        record.occurredAtUtc = FromUtcMs(query.value(2).toLongLong());
        record.type = query.value(3).toString();
        record.severity = SeverityFromInt(query.value(4).toInt());
        record.segmentId = query.value(5).toString();
        record.payloadJson = query.value(6).toString();
        result.push_back(record);
    }
    return result;
}

std::optional<ArchiveSegmentRecord> ArchiveIndexStore::FindSegmentByRelativePath(
    const QString& relativePath,
    QString* errorMessage) const
{
    if (!IsOpen() || relativePath.trimmed().isEmpty())
        return std::nullopt;

    QSqlQuery query(database_);
    query.prepare(
        "SELECT s.segment_id, s.camera_id, s.source_url, s.start_utc_ms, s.end_utc_ms, s.relative_path, "
        "s.container, s.video_codec, s.audio_codec, s.file_size_bytes, s.has_video, s.has_audio, "
        "p.proxy_id AS proxy_id, p.relative_path AS proxy_relative_path, "
        "p.codec_profile AS proxy_codec_profile, p.container AS proxy_container, "
        "p.video_codec AS proxy_video_codec, p.audio_codec AS proxy_audio_codec, "
        "p.file_size_bytes AS proxy_file_size_bytes "
        "FROM segments s "
        "LEFT JOIN playback_proxies p ON p.source_segment_id = s.segment_id "
        "WHERE s.relative_path = :relative_path "
        "LIMIT 1");
    query.bindValue(":relative_path", relativePath.trimmed());
    if (!query.exec())
    {
        if (errorMessage)
            *errorMessage = SqlErrorText(query);
        return std::nullopt;
    }
    if (!query.next())
        return std::nullopt;
    return BuildSegmentRecordFromQuery(query);
}

bool ArchiveIndexStore::UpsertSegment(const ArchiveSegmentRecord& record, QString* errorMessage)
{
    if (!IsOpen())
    {
        if (errorMessage)
            *errorMessage = "Archive database is not open.";
        return false;
    }
    if (record.cameraId.trimmed().isEmpty() || record.relativePath.trimmed().isEmpty()
        || !record.startUtc.isValid() || !record.endUtc.isValid())
    {
        if (errorMessage)
            *errorMessage = "Archive segment record is incomplete.";
        return false;
    }

    QSqlQuery query(database_);
    query.prepare(
        "INSERT INTO segments ("
        "segment_id, camera_id, source_url, start_utc_ms, end_utc_ms, relative_path, "
        "container, video_codec, audio_codec, file_size_bytes, has_video, has_audio, created_utc_ms"
        ") VALUES ("
        ":segment_id, :camera_id, :source_url, :start_utc_ms, :end_utc_ms, :relative_path, "
        ":container, :video_codec, :audio_codec, :file_size_bytes, :has_video, :has_audio, :created_utc_ms"
        ") "
        "ON CONFLICT(segment_id) DO UPDATE SET "
        "camera_id = excluded.camera_id, "
        "source_url = excluded.source_url, "
        "start_utc_ms = excluded.start_utc_ms, "
        "end_utc_ms = excluded.end_utc_ms, "
        "relative_path = excluded.relative_path, "
        "container = excluded.container, "
        "video_codec = excluded.video_codec, "
        "audio_codec = excluded.audio_codec, "
        "file_size_bytes = excluded.file_size_bytes, "
        "has_video = excluded.has_video, "
        "has_audio = excluded.has_audio");
    query.bindValue(":segment_id",
        record.segmentId.trimmed().isEmpty()
            ? QUuid::createUuid().toString(QUuid::WithoutBraces)
            : record.segmentId);
    query.bindValue(":camera_id", record.cameraId);
    query.bindValue(":source_url", record.sourceUrl);
    query.bindValue(":start_utc_ms", ToUtcMs(record.startUtc));
    query.bindValue(":end_utc_ms", ToUtcMs(record.endUtc));
    query.bindValue(":relative_path", record.relativePath);
    query.bindValue(":container", record.container);
    query.bindValue(":video_codec", record.videoCodec);
    query.bindValue(":audio_codec", record.audioCodec);
    query.bindValue(":file_size_bytes", record.fileSizeBytes);
    query.bindValue(":has_video", record.hasVideo ? 1 : 0);
    query.bindValue(":has_audio", record.hasAudio ? 1 : 0);
    query.bindValue(":created_utc_ms", ToUtcMs(QDateTime::currentDateTimeUtc()));
    if (!query.exec())
    {
        if (errorMessage)
            *errorMessage = SqlErrorText(query);
        return false;
    }

    return RefreshDaySummary(record.cameraId, record.startUtc.date(), errorMessage);
}

bool ArchiveIndexStore::UpsertPlaybackProxy(const ArchivePlaybackProxyRecord& record, QString* errorMessage)
{
    if (!IsOpen())
    {
        if (errorMessage)
            *errorMessage = "Archive database is not open.";
        return false;
    }
    if (record.sourceSegmentId.trimmed().isEmpty()
        || record.cameraId.trimmed().isEmpty()
        || record.relativePath.trimmed().isEmpty()
        || !record.startUtc.isValid()
        || !record.endUtc.isValid())
    {
        if (errorMessage)
            *errorMessage = "Playback proxy record is incomplete.";
        return false;
    }

    QSqlQuery query(database_);
    query.prepare(
        "INSERT INTO playback_proxies ("
        "proxy_id, source_segment_id, camera_id, start_utc_ms, end_utc_ms, relative_path, "
        "codec_profile, container, video_codec, audio_codec, file_size_bytes, created_utc_ms"
        ") VALUES ("
        ":proxy_id, :source_segment_id, :camera_id, :start_utc_ms, :end_utc_ms, :relative_path, "
        ":codec_profile, :container, :video_codec, :audio_codec, :file_size_bytes, :created_utc_ms"
        ") "
        "ON CONFLICT(source_segment_id) DO UPDATE SET "
        "proxy_id = excluded.proxy_id, "
        "camera_id = excluded.camera_id, "
        "start_utc_ms = excluded.start_utc_ms, "
        "end_utc_ms = excluded.end_utc_ms, "
        "relative_path = excluded.relative_path, "
        "codec_profile = excluded.codec_profile, "
        "container = excluded.container, "
        "video_codec = excluded.video_codec, "
        "audio_codec = excluded.audio_codec, "
        "file_size_bytes = excluded.file_size_bytes");
    query.bindValue(":proxy_id",
        record.proxyId.trimmed().isEmpty()
            ? QUuid::createUuid().toString(QUuid::WithoutBraces)
            : record.proxyId);
    query.bindValue(":source_segment_id", record.sourceSegmentId);
    query.bindValue(":camera_id", record.cameraId);
    query.bindValue(":start_utc_ms", ToUtcMs(record.startUtc));
    query.bindValue(":end_utc_ms", ToUtcMs(record.endUtc));
    query.bindValue(":relative_path", record.relativePath);
    query.bindValue(":codec_profile", record.codecProfile);
    query.bindValue(":container", record.container);
    query.bindValue(":video_codec", record.videoCodec);
    query.bindValue(":audio_codec", record.audioCodec);
    query.bindValue(":file_size_bytes", std::max<qint64>(0, record.fileSizeBytes));
    query.bindValue(":created_utc_ms", ToUtcMs(QDateTime::currentDateTimeUtc()));
    if (!query.exec())
    {
        if (errorMessage)
            *errorMessage = SqlErrorText(query);
        return false;
    }

    return true;
}

bool ArchiveIndexStore::RecordEvent(const ArchiveEventRecord& record, QString* errorMessage)
{
    if (!IsOpen())
    {
        if (errorMessage)
            *errorMessage = "Archive database is not open.";
        return false;
    }
    if (record.cameraId.trimmed().isEmpty() || !record.occurredAtUtc.isValid())
    {
        if (errorMessage)
            *errorMessage = "Archive event record is incomplete.";
        return false;
    }

    QSqlQuery query(database_);
    query.prepare(
        "INSERT INTO events (event_id, camera_id, occurred_utc_ms, type, severity, segment_id, payload_json) "
        "VALUES (:event_id, :camera_id, :occurred_utc_ms, :type, :severity, :segment_id, :payload_json)");
    query.bindValue(":event_id",
        record.eventId.trimmed().isEmpty()
            ? QUuid::createUuid().toString(QUuid::WithoutBraces)
            : record.eventId);
    query.bindValue(":camera_id", record.cameraId);
    query.bindValue(":occurred_utc_ms", ToUtcMs(record.occurredAtUtc));
    query.bindValue(":type", record.type);
    query.bindValue(":severity", SeverityToInt(record.severity));
    query.bindValue(":segment_id", record.segmentId);
    query.bindValue(":payload_json", record.payloadJson);
    if (!query.exec())
    {
        if (errorMessage)
            *errorMessage = SqlErrorText(query);
        return false;
    }

    return RefreshDaySummary(record.cameraId, record.occurredAtUtc.date(), errorMessage);
}

bool ArchiveIndexStore::ContainsSegmentPath(const QString& relativePath) const
{
    if (!IsOpen() || relativePath.trimmed().isEmpty())
        return false;

    QSqlQuery query(database_);
    query.prepare(
        "SELECT 1 FROM segments WHERE relative_path = :relative_path LIMIT 1");
    query.bindValue(":relative_path", relativePath);
    if (!query.exec())
        return false;
    return query.next();
}
