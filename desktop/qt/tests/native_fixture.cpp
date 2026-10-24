// Test-only C ABI fixture. It never contacts the network or writes a session.
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QMutex>
#include <QMutexLocker>
#include <QThread>
#include <QSet>
#include <cstdlib>
#include <cstring>

namespace {
QMutex mutex;
QJsonObject config;
QJsonArray requests;
QJsonArray playRecords;
QJsonArray playlistAlpha;
QSet<QString> cancelled;
int keyCount = 0, pollCount = 0, pollsActive = 0, maxPollsActive = 0, cancelCalls = 0;
int fmCount = 0;
bool logged = false;
QString vipTier = "none";
QString currentKey;
char* copy(const QByteArray& bytes) {
    auto* result = static_cast<char*>(std::malloc(size_t(bytes.size()) + 1));
    if (result) { std::memcpy(result, bytes.constData(), size_t(bytes.size())); result[bytes.size()] = 0; }
    return result;
}
QJsonObject track(int index) {
    return {{"FileHash", QString::number(index, 16).rightJustified(32, '0')},
        {"SongName", QStringLiteral("纸页旋律 %1").arg(index)}, {"SingerName", QStringLiteral("窗边乐队")},
        {"AlbumName", QStringLiteral("安静的街道")}, {"AlbumID", "album-42"},
        {"AlbumAudioID", QString::number(9000 + index)}, {"Duration", 60}, {"fileid", QString::number(index)}};
}
}
extern "C" {
Q_DECL_EXPORT void EchoTestConfigure(const char* json) {
    QMutexLocker lock(&mutex); config = QJsonDocument::fromJson(json).object(); requests = {}; cancelled.clear();
    fmCount = keyCount = pollCount = pollsActive = maxPollsActive = cancelCalls = 0; logged = config.value("logged").toBool(); currentKey.clear(); playRecords = {}; vipTier = config.value("vip_tier").toString("none");
    playlistAlpha = {}; for (int i = 1; i <= 51; ++i) playlistAlpha.append(track(i));
}
Q_DECL_EXPORT char* EchoTestSnapshot() {
    QMutexLocker lock(&mutex);
    return copy(QJsonDocument(QJsonObject{{"requests", requests}, {"keys", keyCount}, {"polls", pollCount},
        {"max_polls_active", maxPollsActive}, {"cancel_calls", cancelCalls}, {"logged", logged}, {"play_records", playRecords}, {"vip_tier", vipTier}, {"playlist_size", playlistAlpha.size()}}).toJson(QJsonDocument::Compact));
}
Q_DECL_EXPORT int EchoInitializeWithPathsV2(const char*) { return 0; }
Q_DECL_EXPORT char* EchoGetLastError() { return copy("fixture"); }
Q_DECL_EXPORT const char* EchoNativeBuildFingerprint() { return "qt-account-fixture"; }
Q_DECL_EXPORT void EchoFreeString(char* text) { std::free(text); }
Q_DECL_EXPORT int EchoShutdown() { return 0; }
Q_DECL_EXPORT int EchoCancelRequest(const char* id) { QMutexLocker lock(&mutex); ++cancelCalls; cancelled.insert(QString::fromUtf8(id)); return 1; }
Q_DECL_EXPORT int EchoStatsRecordPlay(const char* raw) {
    QMutexLocker lock(&mutex); const auto record = QJsonDocument::fromJson(raw).object();
    const auto id = record.value("session_id").toString();
    if (id.isEmpty()) return 2;
    for (const auto& value : playRecords) if (value.toObject().value("session_id").toString() == id) return 6;
    if (record.value("listened_seconds").toDouble() <= 60) return 1;
    playRecords.append(record); return 0;
}
Q_DECL_EXPORT const char* EchoStatsGetSummary(const char* range) {
    QMutexLocker lock(&mutex); requests.append(QJsonObject{{"method", "STATS"}, {"path", "/stats/summary"}, {"range", QString::fromUtf8(range)}});
    if (config.value("stats_degraded").toBool()) return copy("{\"degraded\":true}");
    return copy(QJsonDocument(config.value("stats_summary").isObject() ? config.value("stats_summary").toObject() :
        QJsonObject{{"total_plays", playRecords.size()}, {"total_listened_seconds", 3600}, {"unique_songs", 3}, {"unique_artists", 2}, {"completion_rate", 0.5}}).toJson(QJsonDocument::Compact));
}
Q_DECL_EXPORT const char* EchoStatsGetTop(const char* dim, const char*, int) {
    QMutexLocker lock(&mutex);
    QJsonArray rows{QJsonObject{{"song_hash", QString(32, 'a')}, {"name", QStringLiteral("纸页旋律")}, {"singer", QStringLiteral("窗边乐队")}, {"album", QStringLiteral("安静的街道")}, {"album_id", "album-42"}, {"play_count", 3}, {"total_listened_seconds", 180}}};
    return copy(QJsonDocument(QJsonObject{{"dim", QString::fromUtf8(dim)}, {"items", rows}}).toJson(QJsonDocument::Compact));
}
Q_DECL_EXPORT const char* EchoStatsGetTimeline(const char*) {
    return copy("{\"items\":[{\"date\":\"2026-10-11\",\"count\":2},{\"date\":\"2026-10-12\",\"count\":3},{\"date\":\"2026-10-13\",\"count\":1}]}");
}
Q_DECL_EXPORT const char* EchoStatsGetRecent(int, int) {
    QMutexLocker lock(&mutex); return copy(QJsonDocument(QJsonObject{{"items", playRecords}}).toJson(QJsonDocument::Compact));
}
Q_DECL_EXPORT const char* EchoStatsGetRecommendations(int) { return copy("{\"items\":[]}"); }
Q_DECL_EXPORT void EchoHandleRequest(const char* methodRaw, const char* pathRaw, const char* queryRaw,
                                    const char* headersRaw, const char* bodyRaw, char** output) {
    const auto method = QString::fromUtf8(methodRaw), path = QString::fromUtf8(pathRaw);
    const auto query = QJsonDocument::fromJson(queryRaw).object(), headers = QJsonDocument::fromJson(headersRaw).object();
    const auto requestId = headers.value("x-echo-request-id").toString();
    QJsonObject options; int sequence = 0, delay = 0;
    {
        QMutexLocker lock(&mutex); options = config;
        requests.append(QJsonObject{{"method", method}, {"path", path}, {"query", query}, {"body", QJsonDocument::fromJson(bodyRaw).object()}});
        if (path == "/login/qr/key") { sequence = ++keyCount; delay = sequence == 1 ? options.value("first_key_delay").toInt() : options.value("key_delay").toInt(); }
        if (path == "/login/qr/check") { sequence = pollCount++; delay = options.value("poll_delay").toInt(); ++pollsActive; maxPollsActive = qMax(maxPollsActive, pollsActive); }
        if (path == "/top/playlist") delay = options.value("top_playlist_delay").toInt();
        if (path == "/search") delay = options.value("search_delay").toInt();
        if (path == "/personal/fm") { sequence = fmCount++; delay = options.value("fm_delay").toInt(); }
        if (path == "/song/url") delay = options.value("song_delay").toInt();
        if (path == "/playlist/track/all") delay = options.value("tracks_delay").toInt();
        if (path == options.value("delay_path").toString()) delay = options.value("delay_ms").toInt();
    }
    if (delay > 0) QThread::msleep(static_cast<unsigned long>(delay));
    QJsonObject response;
    {
        QMutexLocker lock(&mutex);
        const bool wasCancelled = cancelled.contains(requestId) && !options.value("ignore_cancel").toBool();
        if (path == "/login/qr/check") --pollsActive;
        if (wasCancelled) response = {{"status", 0}, {"error", "cancelled"}};
        else if (path == "/login/qr/key") {
            currentKey = QStringLiteral("fixture-key-%1").arg(sequence);
            response = {{"status", 1}, {"data", QJsonObject{{"qrcode", currentKey}, {"qrcodeurl", QStringLiteral("https://example.test/login?key=%1").arg(currentKey)}}}};
        } else if (path == "/login/qr/check") {
            const auto statuses = options.value("statuses").toArray();
            const int remote = statuses.isEmpty() ? 1 : statuses[qMin(sequence, int(statuses.size()) - 1)].toInt();
            if (remote == 4) logged = true;
            response = {{"status", 1}, {"data", QJsonObject{{"status", remote}}}};
        } else if (path == "/login/qr/cancel") {
            response = method == "POST" ? QJsonObject{{"status", 1}} : QJsonObject{{"status", 0}};
        } else if (path == "/auth/logout") {
            if (method == "POST") { logged = false; currentKey.clear(); response = {{"status", 1}}; }
            else response = {{"status", 0}};
        } else if (path == "/user/detail") {
            response = logged ? QJsonObject{{"status", 1}, {"data", QJsonObject{{"userid", "42"}, {"nickname", QStringLiteral("纸页听众")}, {"pic", ""}}}}
                : QJsonObject{{"status", 0}, {"error", "not logged in"}};
        } else if (path == "/user/vip/detail") {
            const QJsonObject summary{{"tier", vipTier}, {"premium", QJsonObject{{"active", vipTier == "premium"}, {"end", "2030-12-31"}}},
                {"listen", QJsonObject{{"active", vipTier == "listen" || vipTier == "premium"}, {"end", "2030-12-31"}}}, {"listen_unlocks_playback", false}};
            response = options.value("vip_unavailable").toBool() ? QJsonObject{{"status", 0}} : QJsonObject{{"status", 1}, {"data", QJsonObject{{"entitlement_summary", summary}}}};
        } else if (path == "/youth/day/vip") {
            if (!options.value("vip_unconfirmed").toBool()) vipTier = "listen";
            response = {{"status", 1}};
        } else if (path == "/youth/vip/ad") response = {{"status", 1}};
        else if (path == "/youth/day/vip/upgrade") {
            if (options.value("vip_upgrade_rejected").toBool()) response = {{"status", 0}, {"error_code", 131001}, {"error_msg", "fixture refusal"}};
            else { if (!options.value("vip_unconfirmed").toBool()) vipTier = "premium"; response = {{"status", 1}}; }
        } else if (path == "/personal/fm") {
            const auto batches = options.value("fm_batches").toArray();
            response = options.value("fm_fail").toBool() ? QJsonObject{{"status",0}} : QJsonObject{{"status",1},{"data",QJsonObject{{"info", batches.isEmpty() ? QJsonArray{track(1),track(2),track(3)} : batches[qMin(sequence,int(batches.size())-1)].toArray()}}}};
        } else if (path == "/top/album") {
            response = {{"status",1},{"data",QJsonObject{{"albums",QJsonArray{QJsonObject{{"album_id",123},{"album_name",QStringLiteral("独立专辑推荐")},{"singer_name",QStringLiteral("另一位歌手")}}}}}}};
        } else if (path == "/album/songs") {
            response = {{"status",1},{"data",QJsonObject{{"info",QJsonArray{track(71),track(72)}},{"total",2}}}};
        } else if (path == "/top/song" && options.value("feed_count").toInt() > 0) {
            QJsonArray rows; for (int i=1;i<=options.value("feed_count").toInt();++i) rows.append(track(i));
            response = {{"status",1},{"data",QJsonObject{{"info",rows}}}};
        } else if (path == "/top/playlist") {
            response = {{"status", 1}, {"data", QJsonObject{{"info", QJsonArray{QJsonObject{{"specialid", 17}, {"specialname", QStringLiteral("纸页精选")}, {"songcount", "51"}}, QJsonObject{{"specialid", 18}, {"specialname", QStringLiteral("窗边发现")}, {"song_count", 2}}}}}}};
        if(options.value("top_playlist_empty").toBool()) response={{"status",1},{"data",QJsonObject{{"info",QJsonArray{}}}}};
        } else if (path == "/settings/device") {
            if (method == "POST") config.insert("device", query.value("clear") == "1" ? QJsonObject{} : query);
            auto device = config.value("device").toObject(); device.insert("registered", true); device.insert("appid", "1005"); device.insert("clientver", "12800");
            response = {{"status", 1}, {"updated", true}, {"data", device}};
        } else if (path == "/diagnostics/memory") response = {{"status", 1}, {"data", QJsonObject{{"working_set_bytes", 104857600}, {"private_bytes", 52428800}, {"pending_task_count", 0}, {"playback_state", "paused"}}}};
        else if (path == "/user/playlist") {
            QJsonArray lists;
            lists.append(QJsonObject{{"global_collection_id", "collection_alpha"}, {"listid", "17"}, {"name", options.value("liked").toBool() ? QStringLiteral("我喜欢的音乐") : QStringLiteral("纸页收藏")}, {"songcount", playlistAlpha.size()}});
            lists.append(QJsonObject{{"global_collection_id", "collection_beta"}, {"listid", "18"}, {"name", QStringLiteral("窗边歌单")}, {"songcount", 2}});
            response = logged ? QJsonObject{{"status", 1}, {"data", QJsonObject{{"info", lists}}}} : QJsonObject{{"status", 0}};
        } else if (path == "/playlist/track/all") {
            const int page = options.value("repeat_page").toBool() ? 1 : query.value("page").toInt(1);
            QJsonArray songs; const bool small = query.value("id").toString() == "collection_beta" || query.value("id").toString() == "18";
            const int total = small ? 2 : int(playlistAlpha.size()), first = (page - 1) * 50 + 1;
            for (int i = first; i <= qMin(total, first + 49); ++i) songs.append(small ? track(i) : playlistAlpha[i - 1].toObject());
            response = {{"status", 1}, {"data", QJsonObject{{"list", songs}, {"total", total}}}};
        } else if (path == "/playlist/tracks/add" || path == "/playlist/tracks/del") {
            if (options.value("playlist_business_error").toBool()) response = {{"status", 0}, {"error", "fixture refused"}};
            else if (options.value("playlist_transport_error").toBool()) response = {{"status", 0}, {"error_code", "network_error"}, {"error", "fixture network error"}};
            else if (method != "POST" || !logged) response = {{"status", 0}, {"error", "not logged in"}};
            else if (path.endsWith("add")) {
                const auto body = QJsonDocument::fromJson(bodyRaw).object();
                if (body.value("listid").toString() == "17") for (const auto& value : body.value("data").toArray()) {
                    const auto row = value.toObject(); bool exists = false;
                    for (const auto& current : playlistAlpha) if (current.toObject().value("FileHash") == row.value("hash")) exists = true;
                    if (!exists) playlistAlpha.append(QJsonObject{{"FileHash", row.value("hash")}, {"SongName", row.value("name")}, {"AlbumID", row.value("album_id")}, {"AlbumAudioID", row.value("mixsongid")}, {"fileid", "999"}});
                }
                response = {{"status", 1}};
            } else {
                if (query.value("listid").toString() == "17") {
                    QJsonArray remaining;
                    for (const auto& row : playlistAlpha) if (row.toObject().value("fileid").toString() != query.value("fileids").toString()) remaining.append(row);
                    playlistAlpha = remaining;
                }
                response = {{"status", 1}};
            }
        } else if (path == "/song/url") {
            auto data = options.value("song_metadata").toObject();
            data.insert("backup_url", options.value("backup_url"));
            // Real native v5 answers with exactly one quality: the delivered
            // one. A fixed three-entry list here hid the picker collapse that
            // shape causes in the UI.
            const auto audioUrl = options.value("audio_url").toString("http://127.0.0.1:1/fixture.wav");
            const auto requested = query.value("quality").toString("128");
            data.insert("quality", requested);
            data.insert("available_qualities", QJsonArray{QJsonObject{{"quality", requested}, {"url", audioUrl}}});
            response = {{"status", 1}, {"url", audioUrl}, {"quality", requested}, {"data", data}};
        } else if (path == "/privilege/lite") {
            QJsonArray goods;
            if (options.contains("privilege_goods")) goods = options.value("privilege_goods").toArray();
            else for (const auto& quality : {QStringLiteral("128"), QStringLiteral("320"), QStringLiteral("flac"), QStringLiteral("high")})
                goods.append(QJsonObject{{"quality", quality}});
            response = {{"status", 1}, {"data", QJsonArray{QJsonObject{{"hash", query.value("hash")}, {"relate_goods", goods}}}}};
        } else if (path == "/search/lyric") response = {{"status", 1}, {"candidates", QJsonArray{QJsonObject{{"id", "fixture-cue"}, {"accesskey", "fixture-key"}}}}};
        else if (path == "/lyric") response = {{"status", 1}, {"lyric", QStringLiteral("[00:00.00]<00:00.00>窗边<00:02.00>传来轻轻的旋律\n[00:04.00]把这一刻留在纸页里\n[00:08.00]风吹过安静的街道\n[00:12.00]我们听见时间的回音\n[00:16.00]一首歌陪伴漫长旅途\n[00:20.00]让心事慢慢舒展开来\n[00:24.00]故事还在继续")}};
        else if (path == "/search" && query.value("keywords").toString() == "__bottle_search_error__")
            response = {{"status",0}};
        else if (path == "/search" && query.value("keywords").toString() == "__bottle_no_match__")
            response = {{"status",1},{"data",QJsonObject{{"info",QJsonArray{}},{"total",0}}}};
        else if (path == "/search" && options.value("search_batches").isArray()) {
            const auto batches = options.value("search_batches").toArray();
            const int page = query.value("page").toInt(1) - 1;
            const auto rows = page >= 0 && page < batches.size() ? batches[page].toArray() : QJsonArray{};
            response = {{"status",1},{"data",QJsonObject{{"info",rows},{"total",options.value("search_total").toInt()}}}};
        }
        else response = {{"status", 1}, {"data", QJsonObject{{"info", QJsonArray{track(1), track(2)}}}}};
    }
    *output = copy(QJsonDocument(QJsonObject{{"status", 200}, {"body", response}}).toJson(QJsonDocument::Compact));
}
}
