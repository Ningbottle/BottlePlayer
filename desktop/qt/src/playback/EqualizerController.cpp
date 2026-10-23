#include "EqualizerController.h"
#include "EqProcessor.h"
#include "PcmOutput.h"
#include "core/Diagnostics.h"
#include <QFileInfo>
#include <QAudioSink>
#include <QAudioBuffer>
#include <QTimer>
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <cmath>
#include <algorithm>
#include <atomic>
#include <QMutex>
#include <QMutexLocker>
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <objbase.h>
#endif

struct AudioDispatch {
    QMutex mutex;
    AudioWorker* worker = nullptr;
    std::atomic<qsizetype> queuedBytes = 0;
    std::atomic_bool overflow = false;
    std::atomic<qsizetype> byteLimit = 96000;
    std::atomic<quint64> generation = 0;
};
class AudioWorker final : public QObject {
    Q_OBJECT
public:
    void initialize() {
#ifdef Q_OS_WIN
        comInitialized_ = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
#endif
    }
    ~AudioWorker() override {
        reset();
#ifdef Q_OS_WIN
        if (comInitialized_) CoUninitialize();
#endif
    }
    void configure(QAudioDevice device, QAudioFormat sinkFormat, bool enabled, const QVariantList& gains, double volume) {
        const bool reconfigure = device_ != device || sinkFormat_ != sinkFormat;
        if (reconfigure) reset();
        device_ = device; sinkFormat_ = sinkFormat; enabled_ = true; volume_ = volume;
        if (reconfigure && sinkFormat_.isValid()) processor_.configure(sinkFormat_.sampleRate(), sinkFormat_.channelCount());
        // The PCM sink also owns ordinary playback. Disabling the laboratory
        // controls bypasses the filters, never the audio output itself.
        std::array<double, 10> values{}; if (enabled) for (size_t i = 0; i < values.size() && i < size_t(gains.size()); ++i) values[i] = gains[int(i)].toDouble();
        processor_.setGains(values); if (sink_) sink_->setVolume(float(volume_));
    }
    void reset() {
        if (pump_) pump_->stop(); pending_.clear(); io_ = nullptr;
        if (sink_) { sink_->reset(); delete sink_; sink_ = nullptr; }
        processor_.configure(sinkFormat_.isValid() ? sinkFormat_.sampleRate() : 48000, sinkFormat_.isValid() ? sinkFormat_.channelCount() : 2); emit ready(false);
    }
    void overflow() { fail(QStringLiteral("EQ 接入队列超限，已回退原始音频")); }
    void playing(bool playing) { playing_ = playing; if (!playing) { if (sink_) sink_->suspend(); if (pump_) pump_->stop(); } else { if (sink_) sink_->resume(); if (pump_ && !pending_.isEmpty()) pump_->start(); } }
    void buffer(const QAudioBuffer& buffer) {
        if (!enabled_ || !playing_ || !buffer.isValid() || buffer.byteCount() == 0) return;
        if (buffer.format().sampleRate() != sinkFormat_.sampleRate() || buffer.format().channelCount() != sinkFormat_.channelCount() || buffer.format().sampleFormat() != QAudioFormat::Float || device_.isNull()) { fail(QStringLiteral("当前设备无法接入 EQ，已回退原始音频")); return; }
        if (!sink_) {
            if (!sinkFormat_.isValid()) { fail(QStringLiteral("当前设备无法接入 EQ，已回退原始音频")); return; }
            sink_ = new QAudioSink(device_, sinkFormat_, this); sink_->setBufferSize(sinkFormat_.bytesForDuration(100000)); sink_->setVolume(float(volume_)); io_ = sink_->start();
            if (!io_ || sink_->error() != QtAudio::NoError) { fail(QStringLiteral("音频处理输出初始化失败，已回退原始音频")); return; }
            if (!pump_) { pump_ = new QTimer(this); pump_->setInterval(5); connect(pump_, &QTimer::timeout, this, &AudioWorker::pump); }
        }
        QByteArray data(buffer.constData<char>(), buffer.byteCount());
        processor_.process(reinterpret_cast<float*>(data.data()), buffer.frameCount());
        if (sinkFormat_.sampleFormat() != QAudioFormat::Float) data = PcmOutput::encode(reinterpret_cast<const float*>(data.constData()), buffer.frameCount() * sinkFormat_.channelCount(), sinkFormat_.sampleFormat());
        if (data.isEmpty() || pending_.size() + data.size() > sinkFormat_.bytesForDuration(250000)) { fail(QStringLiteral("EQ 输出缓冲超限，已回退原始音频")); return; }
        pending_.append(data); pump(); if (!pending_.isEmpty()) pump_->start(); emit ready(true);
    }
signals:
    void ready(bool active);
    void failure(const QString& reason);
private:
    void pump() {
        if (!sink_ || !io_ || !playing_) return;
        if (sink_->error() != QtAudio::NoError && sink_->error() != QtAudio::UnderrunError) { fail(QStringLiteral("EQ 输出中断，已回退原始音频")); return; }
        const auto bytes = std::min(pending_.size(), sink_->bytesFree());
        if (bytes > 0) { const auto written = io_->write(pending_.constData(), bytes); if (written < 0) { fail(QStringLiteral("EQ 输出写入失败")); return; } pending_.remove(0, written); }
        if (pending_.isEmpty() && pump_) pump_->stop();
    }
    void fail(const QString& reason) { enabled_ = false; reset(); emit failure(reason); }
    QAudioDevice device_;
    QAudioFormat sinkFormat_;
    QAudioSink* sink_ = nullptr; QIODevice* io_ = nullptr; QTimer* pump_ = nullptr;
    EqProcessor processor_;
    QByteArray pending_;
    double volume_ = .5;
    bool enabled_ = false, playing_ = false;
#ifdef Q_OS_WIN
    bool comInitialized_ = false;
#endif
};
EqualizerController::EqualizerController(QMediaPlayer& player, QAudioOutput& output, const QString& profile, QObject* parent)
    : QObject(parent), player_(player), output_(output), settings_(QDir(profile).filePath("equalizer.ini"), QSettings::IniFormat) {
    enabled_ = settings_.value("enabled", false).toBool(); preset_ = settings_.value("preset", "Flat").toString();
    const auto gains = QJsonDocument::fromJson(settings_.value("bands").toByteArray()).array();
    for (int i = 0; i < 10; ++i) { const auto gain = i < gains.size() ? gains[i].toDouble() : 0; bands_.append(std::isfinite(gain) ? std::clamp(gain, -6.0, 6.0) : 0); }
    worker_ = new AudioWorker; worker_->moveToThread(&workerThread_); connect(&workerThread_, &QThread::finished, worker_, &QObject::deleteLater);
    connect(&workerThread_, &QThread::started, worker_, &AudioWorker::initialize);
    connect(worker_, &AudioWorker::ready, this, [this](bool ready) { const bool changedRoute = routed_ != ready; routed_ = ready; if (changedRoute && ready) traceOutput("sink_ready"); const bool active = ready && enabled_ && allowed_; if (active_ == active) return; active_ = active; if (active) reason_ = QStringLiteral("本地 PCM 音频处理已接入"); emit changed(); });
    connect(worker_, &AudioWorker::failure, this, [this](const QString& reason) { routed_ = active_ = false; player_.setAudioBufferOutput(nullptr); player_.setAudioOutput(&output_); output_.setMuted(false); reason_ = reason; traceOutput("sink_failure", reason); emit changed(); });
    workerThread_.start();
    dispatch_ = std::make_shared<AudioDispatch>(); dispatch_->worker = worker_;
    connect(&player_, &QMediaPlayer::playbackStateChanged, this, [this] { const bool playing = player_.playbackState() == QMediaPlayer::PlayingState; QMetaObject::invokeMethod(worker_, [this, playing] { worker_->playing(playing); }); });
    connect(&player_, &QMediaPlayer::sourceChanged, this, [this] { previousPosition_ = -1; ++dispatch_->generation; QMetaObject::invokeMethod(worker_, &AudioWorker::reset); });
    connect(&player_, &QMediaPlayer::positionChanged, this, [this](qint64 position) { if (previousPosition_ >= 0 && (position < previousPosition_ || position - previousPosition_ > 1500)) QMetaObject::invokeMethod(worker_, &AudioWorker::reset); previousPosition_ = position; });
    connect(&output_, &QAudioOutput::volumeChanged, this, &EqualizerController::updateWorker);
    connect(&mediaDevices_, &QMediaDevices::audioOutputsChanged, this, [this] { setDeviceId(deviceId_); emit changed(); });
    setDeviceId(settings_.value("device").toString()); updateWorker();
}
EqualizerController::~EqualizerController() {
    { QMutexLocker lock(&dispatch_->mutex); dispatch_->worker = nullptr; }
    if (buffers_) disconnect(buffers_.get(), nullptr, buffers_.get(), nullptr);
    player_.setAudioBufferOutput(nullptr); disconnect(&player_, nullptr, this, nullptr); disconnect(&output_, nullptr, this, nullptr);
    QMetaObject::invokeMethod(worker_, &AudioWorker::reset, Qt::BlockingQueuedConnection); workerThread_.quit(); workerThread_.wait(); player_.setAudioOutput(&output_); output_.setMuted(false);
}
QVariantList EqualizerController::devices() const {
    QVariantList rows{QVariantMap{{"id", ""}, {"name", QStringLiteral("跟随系统默认")}}}; bool found = deviceId_.isEmpty();
    for (const auto& device : QMediaDevices::audioOutputs()) {
        const auto id = QString::fromLatin1(device.id().toBase64()); found = found || id == deviceId_;
        rows.append(QVariantMap{{"id", id}, {"name", device.description()}});
    }
    if (!found) rows.append(QVariantMap{{"id", deviceId_}, {"name", QStringLiteral("所选设备已断开，暂用系统默认")}});
    return rows;
}
void EqualizerController::setDeviceId(const QString& id) {
    auto selected = QMediaDevices::defaultAudioOutput();
    for (const auto& device : QMediaDevices::audioOutputs()) if (QString::fromLatin1(device.id().toBase64()) == id) selected = device;
    const bool changedDevice = output_.device() != selected;
    deviceId_ = id; output_.setDevice(selected); settings_.setValue("device", deviceId_);
    if (changedDevice) { ++dispatch_->generation; routed_ = active_ = false; }
    updateWorker(); traceOutput("device_selected"); emit changed();
}
void EqualizerController::updateWorker() {
    const auto device = output_.device(); const bool enabled = enabled_ && allowed_; const auto gains = bands_; const auto volume = output_.volume();
    // Windows device capability discovery requires COM; prepare the immutable
    // format on the GUI apartment, and keep WASAPI on an initialized worker.
    const auto sinkFormat = PcmOutput::supportedFormat(device);
    auto inputFormat = sinkFormat; inputFormat.setSampleFormat(QAudioFormat::Float);
    if (sinkFormat.isValid() && (!buffers_ || buffers_->format() != inputFormat)) {
        player_.setAudioBufferOutput(nullptr); ++dispatch_->generation;
        buffers_ = std::make_unique<QAudioBufferOutput>(inputFormat);
        dispatch_->byteLimit.store(inputFormat.bytesForDuration(250000)); const auto dispatch = dispatch_;
        connect(buffers_.get(), &QAudioBufferOutput::audioBufferReceived, buffers_.get(), [dispatch](const QAudioBuffer& buffer) {
            if (!buffer.isValid() || buffer.byteCount() <= 0) return;
            QMutexLocker lock(&dispatch->mutex); auto* worker = dispatch->worker; if (!worker) return;
            const auto bytes = buffer.byteCount(); const auto generation = dispatch->generation.load();
            if (dispatch->queuedBytes.fetch_add(bytes) + bytes > dispatch->byteLimit.load()) {
                dispatch->queuedBytes.fetch_sub(bytes);
                if (!dispatch->overflow.exchange(true)) QMetaObject::invokeMethod(worker, [worker, dispatch, generation] { if (generation == dispatch->generation.load()) worker->overflow(); dispatch->overflow.store(false); }, Qt::QueuedConnection);
                return;
            }
            QMetaObject::invokeMethod(worker, [worker, dispatch, buffer, bytes, generation] { dispatch->queuedBytes.fetch_sub(bytes); if (generation == dispatch->generation.load()) worker->buffer(buffer); }, Qt::QueuedConnection);
        }, Qt::DirectConnection);
    }
    // QMediaPlayer's default Windows renderer can select an empty channel
    // layout in Qt 6.8 and silently discard audio while its clock advances.
    // Use one explicitly formatted sink for both bypass and filtered playback.
    // Detach the default renderer so it cannot duplicate or reject this output.
    player_.setAudioBufferOutput(sinkFormat.isValid() ? buffers_.get() : nullptr);
    player_.setAudioOutput(sinkFormat.isValid() ? nullptr : &output_);
    active_ = routed_ && enabled; output_.setMuted(false);
    if (!sinkFormat.isValid()) { routed_ = active_ = false; reason_ = QStringLiteral("音频输出设备不可用，请检查系统输出设备"); }
    QMetaObject::invokeMethod(worker_, [this, device, sinkFormat, enabled, gains, volume] { worker_->configure(device, sinkFormat, enabled, gains, volume); });
}
void EqualizerController::traceOutput(const QString& event, const QString& reason) {
    const auto device = output_.device(); const auto format = PcmOutput::supportedFormat(device);
    writeDiagnostic(QFileInfo(settings_.fileName()).absolutePath(), "audio", {{"event", event}, {"requestedDevice", deviceId_},
        {"activeDevice", QString::fromLatin1(device.id().toBase64())}, {"deviceName", device.description()},
        {"sampleRate", format.sampleRate()}, {"channels", format.channelCount()}, {"format", int(format.sampleFormat())}, {"reason", reason}});
}
void EqualizerController::setAllowed(bool value) { if (allowed_ == value) return; allowed_ = value; if (!value) { active_ = false; output_.setMuted(false); reason_ = QStringLiteral("实验室模式关闭，使用原始音频"); } else reason_ = enabled_ ? QStringLiteral("等待解码音频缓冲…") : QStringLiteral("EQ 已关闭，使用原始音频"); updateWorker(); emit changed(); }
void EqualizerController::setEnabled(bool value) { enabled_ = value; settings_.setValue("enabled", value); if (!value) { active_ = false; output_.setMuted(false); } reason_ = value && allowed_ ? QStringLiteral("等待解码音频缓冲…") : QStringLiteral("使用原始音频"); updateWorker(); emit changed(); }
void EqualizerController::setBand(int index, double gain) { if (index < 0 || index >= 10 || !std::isfinite(gain)) return; bands_[index] = std::clamp(gain, -6.0, 6.0); preset_ = "Custom"; settings_.setValue("bands", QJsonDocument(QJsonArray::fromVariantList(bands_)).toJson(QJsonDocument::Compact)); settings_.setValue("preset", preset_); updateWorker(); emit changed(); }
void EqualizerController::applyPreset(const QString& name) {
    static const QHash<QString, QVariantList> presets{{"Flat", {0,0,0,0,0,0,0,0,0,0}}, {"Bass Boost", {4,5,4,1,0,0,0,1,1,0}}, {"Vocal", {-1,-1,0,1,2,3,3,2,1,0}}, {"Rock", {3,4,3,1,-1,-1,1,3,4,3}}, {"Harman Kardon", {2,3,2,0,-1,0,1,2,2,1}}, {"125Hz Test", {0,0,6,0,0,0,0,0,0,0}}};
    if (!presets.contains(name)) return; bands_ = presets.value(name); preset_ = name;
    settings_.setValue("preset", name); settings_.setValue("bands", QJsonDocument(QJsonArray::fromVariantList(bands_)).toJson(QJsonDocument::Compact)); updateWorker(); emit changed();
}
#include "EqualizerController.moc"
