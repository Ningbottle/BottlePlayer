#include "echo/execution/OperationPolicy.h"
#include "NativeGateway.h"
#include "Diagnostics.h"
#include <QElapsedTimer>
#include "settings/StatsArchive.h"
#include <QDir>
#include <QLibrary>
#include <QJsonDocument>
#include <QFutureWatcher>
#include <QPointer>
#include <QThreadPool>
#include <QSet>
#include <QHash>
#include <QUuid>
#include <QtConcurrent>
#include <atomic>
#include <algorithm>

struct NativeGateway::Runtime {
    QLibrary library;
    QString profile;
    std::atomic_bool ready = false;
    using Init = int(*)(const char*);
    using Error = char*(*)();
    using Handle = void(*)(const char*, const char*, const char*, const char*, const char*, char**);
    using Free = void(*)(char*);
    using Cancel = int(*)(const char*);
    using Shutdown = int(*)();
    using Fingerprint = const char*(*)();
    using StatsRead = const char*(*)(const char*);
    using StatsTop = const char*(*)(const char*, const char*, int);
    using StatsRecent = const char*(*)(int, int);
    using StatsRecommend = const char*(*)(int);
    using StatsRecord = int(*)(const char*);
    Init init = nullptr; Error error = nullptr; Handle handle = nullptr;
    Free freeString = nullptr; Cancel cancel = nullptr; Shutdown shutdown = nullptr;
    Fingerprint build = nullptr;
    StatsRead summary = nullptr, timeline = nullptr;
    StatsTop top = nullptr; StatsRecent recent = nullptr;
    StatsRecommend recommend = nullptr; StatsRecord record = nullptr;
    Runtime(QString path, QString data) : library(std::move(path)), profile(std::move(data)) {
        library.setLoadHints(QLibrary::ResolveAllSymbolsHint | QLibrary::PreventUnloadHint);
    }
};
struct NativeGateway::WorkState {
    struct RequestControl {
        std::shared_ptr<std::atomic_bool> flag;
        bool nativeRequest = false;
    };
    QThreadPool pool;
    QSet<QString> pending;
    QHash<QString, RequestControl> cancellation;
};
NativeGateway::NativeGateway(QString path, QString profile, QObject* parent)
    : QObject(parent), runtime_(std::make_shared<Runtime>(std::move(path), std::move(profile))),
      work_(std::make_unique<WorkState>()) {
    work_->pool.setMaxThreadCount(3);
    work_->pool.setExpiryTimeout(5000);
}
NativeGateway::~NativeGateway() {
    for (const auto& id : std::as_const(work_->pending)) cancel(id);
    work_->pool.waitForDone(); // Native deadlines bound requests; never unload DLL workers still execute.
    if (runtime_->ready && runtime_->shutdown) runtime_->shutdown();
}
void NativeGateway::initialize(Callback callback) {
    auto state = runtime_;
    auto* watcher = new QFutureWatcher<QJsonObject>(this);
    connect(watcher, &QFutureWatcher<QJsonObject>::finished, this, [this, watcher, callback] {
        const auto value = watcher->result(); watcher->deleteLater();
        writeDiagnostic(runtime_->profile, "requests", {{"event", "initialized"}, {"ready", runtime_->ready.load()}, {"fingerprint", value.value("fingerprint").toString()}});
        emit readyChanged(); callback(value);
    });
    watcher->setFuture(QtConcurrent::run(&work_->pool, [state] {
        if (!state->library.load()) return QJsonObject{{"error", QStringLiteral("原生音乐核心加载失败")}};
        state->init = reinterpret_cast<Runtime::Init>(state->library.resolve("EchoInitializeWithPathsV2"));
        state->error = reinterpret_cast<Runtime::Error>(state->library.resolve("EchoGetLastError"));
        state->handle = reinterpret_cast<Runtime::Handle>(state->library.resolve("EchoHandleRequest"));
        state->freeString = reinterpret_cast<Runtime::Free>(state->library.resolve("EchoFreeString"));
        state->cancel = reinterpret_cast<Runtime::Cancel>(state->library.resolve("EchoCancelRequest"));
        state->shutdown = reinterpret_cast<Runtime::Shutdown>(state->library.resolve("EchoShutdown"));
        state->build = reinterpret_cast<Runtime::Fingerprint>(state->library.resolve("EchoNativeBuildFingerprint"));
        state->summary = reinterpret_cast<Runtime::StatsRead>(state->library.resolve("EchoStatsGetSummary"));
        state->timeline = reinterpret_cast<Runtime::StatsRead>(state->library.resolve("EchoStatsGetTimeline"));
        state->top = reinterpret_cast<Runtime::StatsTop>(state->library.resolve("EchoStatsGetTop"));
        state->recent = reinterpret_cast<Runtime::StatsRecent>(state->library.resolve("EchoStatsGetRecent"));
        state->recommend = reinterpret_cast<Runtime::StatsRecommend>(state->library.resolve("EchoStatsGetRecommendations"));
        state->record = reinterpret_cast<Runtime::StatsRecord>(state->library.resolve("EchoStatsRecordPlay"));
        if (!state->init || !state->handle || !state->freeString || !state->cancel || !state->shutdown || !state->build)
            return QJsonObject{{"error", QStringLiteral("原生核心 ABI 不完整")}};
        if (state->init(state->profile.toUtf8().constData()) != 0) {
            if (state->error) { auto* text = state->error(); if (text) state->freeString(text); }
            return QJsonObject{{"error", QStringLiteral("独立数据目录初始化失败")}};
        }
        state->ready = true;
        return QJsonObject{{"status", 1}, {"fingerprint", QString::fromUtf8(state->build())}};
    }));
}
QString NativeGateway::get(const QString& path, const QJsonObject& query, QObject* receiver, Callback callback) {
    return request(QStringLiteral("GET"), path, query, {}, receiver, std::move(callback));
}
bool NativeGateway::ready() const { return runtime_->ready; }
QString NativeGateway::request(const QString& method, const QString& path, const QJsonObject& query,
                               const QJsonObject& body, QObject* receiver, Callback callback) {
    if (!runtime_->ready) { callback({{"status", 0}, {"error", QStringLiteral("核心尚未就绪")}}); return {}; }
    const auto state = runtime_;
    return dispatch([state, method, path, query, body](const QString& id) {
        QElapsedTimer elapsed; elapsed.start();
        writeDiagnostic(state->profile, "requests", {{"event", "request"}, {"requestId", id}, {"method", method}, {"path", path}});
        const auto queryBytes = QJsonDocument(query).toJson(QJsonDocument::Compact);
        const auto headers = QJsonDocument(QJsonObject{{"x-echo-request-id", id}, {"x-echo-budget-ms", static_cast<int>(echo::execution::PolicyForPath(path.toStdString()).deadlineMs)}}).toJson(QJsonDocument::Compact);
        char* raw = nullptr;
        const auto bodyBytes = body.isEmpty() ? QByteArray{} : QJsonDocument(body).toJson(QJsonDocument::Compact);
        state->handle(method.toUtf8().constData(), path.toUtf8().constData(), queryBytes.constData(), headers.constData(), bodyBytes.constData(), &raw);
        if (!raw) return QJsonObject{{"status", 0}, {"error", QStringLiteral("核心返回为空")}};
        std::unique_ptr<char, Runtime::Free> nativeString(raw, state->freeString);
        const auto envelope = QJsonDocument::fromJson(QByteArray(nativeString.get())).object();
        nativeString.reset();
        auto responseBody = envelope.value("body");
        if (responseBody.isString()) responseBody = QJsonDocument::fromJson(responseBody.toString().toUtf8()).object();
        if (!responseBody.isObject()) return QJsonObject{{"status", 0}, {"error", QStringLiteral("响应格式无效")}};
        auto response = responseBody.toObject();
        const int httpStatus = envelope.value("status").toInt(200);
        writeDiagnostic(state->profile, "requests", {{"event", "response"}, {"requestId", id}, {"method", method}, {"path", path},
            {"http", httpStatus}, {"status", response.value("status").toVariant().toInt()}, {"elapsedMs", elapsed.elapsed()}});
        response.insert("_httpStatus", httpStatus);
        if (httpStatus >= 500 || httpStatus == 408 || httpStatus == 429) response.insert("_transportError", true);
        return response;
    }, receiver, std::move(callback), path != QLatin1String("/login/qr/cancel"), true);
}
QString NativeGateway::dispatch(std::function<QJsonObject(const QString&)> work, QObject* receiver, Callback callback,
                                bool cancelable, bool nativeRequest) {
    const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    work_->pending.insert(id);
    auto cancelled = std::make_shared<std::atomic_bool>(false);
    if (cancelable) work_->cancellation.insert(id, WorkState::RequestControl{cancelled, nativeRequest});
    const auto weakReceiver = QPointer<QObject>(receiver);
    auto* watcher = new QFutureWatcher<QJsonObject>(this);
    connect(watcher, &QFutureWatcher<QJsonObject>::finished, this, [this, watcher, id, weakReceiver, callback] {
        work_->pending.remove(id); work_->cancellation.remove(id);
        const auto value = watcher->result(); watcher->deleteLater();
        if (weakReceiver) callback(value);
    });
    watcher->setFuture(QtConcurrent::run(&work_->pool, [work = std::move(work), id, cancelled] {
        if (cancelled->load()) return QJsonObject{{"status", 0}, {"degraded", true}, {"error", "request_cancelled"}};
        return work(id);
    }));
    return id;
}
QString NativeGateway::stats(const QString& operation, const QJsonObject& arguments, QObject* receiver, Callback callback) {
    if (!ready()) { callback({{"degraded", true}, {"error", QStringLiteral("核心尚未就绪")}}); return {}; }
    const auto state = runtime_;
    return dispatch([state, operation, arguments](const QString&) {
        const auto range = arguments.value("range").toString("30d").toUtf8();
        const int limit = std::clamp(arguments.value("limit").toInt(20), 1, 500);
        const char* raw = nullptr;
        if (operation == "summary" && state->summary) raw = state->summary(range.constData());
        else if (operation == "timeline" && state->timeline) raw = state->timeline(range.constData());
        else if (operation == "top" && state->top) raw = state->top(arguments.value("kind").toString("song").toUtf8().constData(), range.constData(), limit);
        else if (operation == "recent" && state->recent) raw = state->recent(limit, std::max(0, arguments.value("offset").toInt()));
        else if (operation == "recommendations" && state->recommend) raw = state->recommend(limit);
        if (!raw) return QJsonObject{{"degraded", true}, {"error", QStringLiteral("统计服务不可用")}};
        QJsonParseError error;
        std::unique_ptr<char, Runtime::Free> nativeString(const_cast<char*>(raw), state->freeString);
        const auto parsed = QJsonDocument::fromJson(QByteArray(nativeString.get()), &error);
        nativeString.reset();
        if (error.error != QJsonParseError::NoError || !parsed.isObject())
            return QJsonObject{{"degraded", true}, {"error", QStringLiteral("统计响应无效")}};
        return parsed.object();
    }, receiver, std::move(callback));
}
QString NativeGateway::recordPlay(const QJsonObject& record, QObject* receiver, Callback callback) {
    if (!ready() || !runtime_->record) { callback({{"status", 0}, {"error", QStringLiteral("统计记录服务不可用")}}); return {}; }
    const auto state = runtime_;
    return dispatch([state, record](const QString&) {
        const int result = state->record(QJsonDocument(record).toJson(QJsonDocument::Compact).constData());
        return QJsonObject{{"status", result == 0 || result == 1 || result == 6 ? 1 : 0}, {"record_status", result}};
    }, receiver, std::move(callback), false);
}
void NativeGateway::cancel(const QString& id) {
    // Only active, cancelable work owns an entry. Completed IDs and durable
    // writes must not create orphan native cancellation records.
    const auto control = work_->cancellation.constFind(id);
    if (control == work_->cancellation.cend() || control->flag->exchange(true)) return;
    writeDiagnostic(runtime_->profile, "requests", {{"event", "cancel"}, {"requestId", id}, {"nativeRequest", control->nativeRequest}});
    if (control->nativeRequest && runtime_->cancel) runtime_->cancel(id.toUtf8().constData());
}
QString NativeGateway::archiveStats(const QString& source, const QString& destination, bool importing,
                                    std::shared_ptr<std::atomic_bool> stopped, QObject* receiver, Callback callback) {
    if (!ready() || !runtime_->record) { callback({{"status", 0}, {"error", QStringLiteral("统计核心尚未就绪")}}); return {}; }
    const auto state = runtime_;
    return dispatch([state, source, destination, importing, stopped](const QString&) {
        const auto database = QDir(state->profile).filePath("bottlemusic.db");
        if (importing) return StatsArchive::restore(source, database, destination, [state](const QJsonObject& row) { return state->record(QJsonDocument(row).toJson(QJsonDocument::Compact).constData()); }, *stopped);
        QJsonArray records; const auto read = StatsArchive::read(database, records);
        return read.value("status").toInt() == 1 ? StatsArchive::write(destination, records) : read;
    }, receiver, std::move(callback), false);
}
QString NativeGateway::fingerprint() const {
    return runtime_->ready ? QString::fromUtf8(runtime_->build()) : QString{};
}
