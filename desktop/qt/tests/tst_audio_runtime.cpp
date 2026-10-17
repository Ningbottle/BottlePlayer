#define NOMINMAX
#include "music/MusicController.h"
#include "playback/EqualizerController.h"
#include "playback/PcmOutput.h"
#include <QtTest>
#include <QTemporaryDir>
#include <QAudioBuffer>
#include <QAudioDevice>
#include <QAudioBufferOutput>
#include <QMediaDevices>
#include <QFile>
#include <QSettings>
#include <QLibrary>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtEndian>
#include <cmath>
#include <cstring>
#ifdef Q_OS_WIN
#include <windows.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <endpointvolume.h>
#include <wrl/client.h>
#endif

namespace {
// Observe only this process's output meter; no microphone or audio recording.
float processAudioPeak(const QString& endpointId = {}) {
#ifdef Q_OS_WIN
    using Microsoft::WRL::ComPtr;
    ComPtr<IMMDeviceEnumerator> devices;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&devices)))) return -1;
    ComPtr<IMMDevice> device;
    if (endpointId.isEmpty()) { if (FAILED(devices->GetDefaultAudioEndpoint(eRender, eMultimedia, &device))) return -1; }
    else if (FAILED(devices->GetDevice(reinterpret_cast<const wchar_t*>(endpointId.utf16()), &device))) return -1;
    ComPtr<IAudioSessionManager2> manager;
    if (FAILED(device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(manager.GetAddressOf())))) return -1;
    ComPtr<IAudioSessionEnumerator> sessions;
    if (FAILED(manager->GetSessionEnumerator(&sessions))) return -1;
    int count = 0; sessions->GetCount(&count); float peak = 0;
    for (int i = 0; i < count; ++i) {
        ComPtr<IAudioSessionControl> session; ComPtr<IAudioSessionControl2> control;
        if (FAILED(sessions->GetSession(i, &session)) || FAILED(session.As(&control))) continue;
        DWORD pid = 0; if (FAILED(control->GetProcessId(&pid)) || pid != GetCurrentProcessId()) continue;
        ComPtr<IAudioMeterInformation> meter; float value = 0;
        if (SUCCEEDED(session.As(&meter)) && SUCCEEDED(meter->GetPeakValue(&value))) peak = std::max(peak, value);
    }
    return peak;
#else
    return -1;
#endif
}
}

class AudioRuntimeTests final : public QObject {
    Q_OBJECT
    static QString root() { return qEnvironmentVariable("BOTTLE_TEST_TEMP_ROOT", QDir::tempPath()) + "/bottle-audio-XXXXXX"; }
    static QUrl tone(const QString& path) {
        constexpr int rate = 48000, seconds = 8;
        QByteArray wav(44 + rate * seconds * 4, '\0');
        std::memcpy(wav.data(), "RIFF", 4); std::memcpy(wav.data() + 8, "WAVEfmt ", 8);
        qToLittleEndian<quint32>(wav.size() - 8, wav.data() + 4);
        qToLittleEndian<quint32>(16, wav.data() + 16); qToLittleEndian<quint16>(1, wav.data() + 20);
        qToLittleEndian<quint16>(2, wav.data() + 22); qToLittleEndian<quint32>(rate, wav.data() + 24);
        qToLittleEndian<quint32>(rate * 4, wav.data() + 28); qToLittleEndian<quint16>(4, wav.data() + 32);
        qToLittleEndian<quint16>(16, wav.data() + 34); std::memcpy(wav.data() + 36, "data", 4);
        qToLittleEndian<quint32>(wav.size() - 44, wav.data() + 40);
        for (int i = 0; i < rate * seconds; ++i) {
            const auto sample = qint16(2000 * std::sin(i * 440.0 * 6.283185307179586 / rate));
            qToLittleEndian(sample, wav.data() + 44 + i * 4); qToLittleEndian(sample, wav.data() + 46 + i * 4);
        }
        QFile file(path); if (!file.open(QIODevice::WriteOnly) || file.write(wav) != wav.size()) return {};
        return QUrl::fromLocalFile(path);
    }
private slots:
    void selectedDevicesReceiveAudioAcrossSwitches() {
        const auto devices = QMediaDevices::audioOutputs();
        if (devices.size() < 2) QSKIP("Two connected output endpoints required for routing verification");
        QTemporaryDir profile(root()); QVERIFY(profile.isValid());
        MusicController music({}, profile.path()); music.setVolume(.08);
        EqualizerController equalizer(*music.mediaPlayer(), *music.audioOutput(), profile.path());
        music.openLocal(tone(profile.filePath("switch.wav")));
        QTRY_VERIFY_WITH_TIMEOUT(music.playing() && music.position() > 200, 10000);
        for (const auto& device : devices) {
            qInfo() << "Selected endpoint" << device.description() << device.id() << device.preferredFormat();
            QVERIFY(PcmOutput::supportedFormat(device).isValid());
            equalizer.setDeviceId(QString::fromLatin1(device.id().toBase64()));
            QCOMPARE(music.audioOutput()->device().id(), device.id());
#ifdef Q_OS_WIN
            const auto endpoint = QString::fromUtf8(device.id());
            QTRY_VERIFY_WITH_TIMEOUT(processAudioPeak(endpoint) > .0001f, 4000);
            QTest::qWait(200);
            for (const auto& other : devices) if (other.id() != device.id())
                QVERIFY2(processAudioPeak(QString::fromUtf8(other.id())) <= .0001f, "Previous endpoint still receives this process's audio");
#endif
            music.setPlaying(false); QTest::qWait(150);
            equalizer.setDeviceId(QString::fromLatin1(device.id().toBase64()));
            music.setPlaying(true);
#ifdef Q_OS_WIN
            QTRY_VERIFY_WITH_TIMEOUT(processAudioPeak(endpoint) > .0001f, 3000);
#endif
        }
        music.finishSession();
    }
    void localPlaybackHasNonzeroDecodedAudio() {
        if (QMediaDevices::defaultAudioOutput().isNull()) QSKIP("No audio device available");
        QTemporaryDir profile(root()); QVERIFY(profile.isValid());
        const auto url = tone(profile.filePath("tone.wav")); QVERIFY(!url.isEmpty());
        MusicController music({}, profile.path()); music.setVolume(.15);
        EqualizerController equalizer(*music.mediaPlayer(), *music.audioOutput(), profile.path());
        auto* buffers = music.mediaPlayer()->audioBufferOutput(); QVERIFY(buffers);
        qint64 frames = 0; float peak = 0;
        connect(buffers, &QAudioBufferOutput::audioBufferReceived, this, [&](const QAudioBuffer& buffer) {
            if (!buffer.isValid() || buffer.format().sampleFormat() != QAudioFormat::Float) return;
            frames += buffer.frameCount();
            for (int i = 0; i < buffer.sampleCount(); ++i) peak = std::max(peak, std::abs(buffer.constData<float>()[i]));
        });
        music.openLocal(url);
        QTRY_VERIFY_WITH_TIMEOUT(music.playing() && music.position() > 500 && frames > 10000, 10000);
        QVERIFY(peak > .01f); QVERIFY(!music.audioOutput()->isMuted());
#ifdef Q_OS_WIN
        QTRY_VERIFY_WITH_TIMEOUT(processAudioPeak() > .0001f, 3000);
#endif
        music.setPlaying(false); const auto paused = music.position(); QTest::qWait(150);
        QVERIFY(std::abs(music.position() - paused) < 100);
#ifdef Q_OS_WIN
        QTRY_VERIFY_WITH_TIMEOUT(processAudioPeak() == 0, 1500);
#endif
        music.seek(3000); music.setPlaying(true); QTRY_VERIFY(music.position() > 3200);
        QVERIFY(!equalizer.enabled()); QVERIFY(!equalizer.allowed()); QVERIFY(!equalizer.active());
        music.finishSession();
    }
    void equalizerSwitchAndSeekKeepOutputAvailable() {
        if (QMediaDevices::defaultAudioOutput().isNull()) QSKIP("No audio device available");
        QTemporaryDir profile(root()); QVERIFY(profile.isValid());
        const auto url = tone(profile.filePath("tone.wav")); QVERIFY(!url.isEmpty());
        MusicController music({}, profile.path()); music.setVolume(.15);
        EqualizerController equalizer(*music.mediaPlayer(), *music.audioOutput(), profile.path());
        equalizer.setAllowed(true); equalizer.setEnabled(true); music.openLocal(url);
        QTRY_VERIFY_WITH_TIMEOUT(music.playing() && music.position() > 500, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(equalizer.active(), 5000);
#ifdef Q_OS_WIN
        QTRY_VERIFY_WITH_TIMEOUT(processAudioPeak() > .0001f, 3000);
#endif
        music.setPlaying(false); music.seek(4000); music.setPlaying(true);
        QTRY_VERIFY_WITH_TIMEOUT(music.position() > 4300 && equalizer.active(), 5000);
#ifdef Q_OS_WIN
        QTRY_VERIFY_WITH_TIMEOUT(processAudioPeak() > .0001f, 3000);
#endif
        equalizer.setEnabled(false); QTRY_VERIFY(!music.audioOutput()->isMuted());
        QVERIFY(!equalizer.active());
#ifdef Q_OS_WIN
        QTRY_VERIFY_WITH_TIMEOUT(processAudioPeak() > .0001f, 3000);
#endif
        music.setVolume(0);
#ifdef Q_OS_WIN
        QTRY_VERIFY_WITH_TIMEOUT(processAudioPeak() == 0, 1500);
#endif
        music.setVolume(.15);
#ifdef Q_OS_WIN
        QTRY_VERIFY_WITH_TIMEOUT(processAudioPeak() > .0001f, 3000);
#endif
        music.finishSession();
    }
    void resumeQueueAndStopDoNotRestartPlayback() {
        QTemporaryDir profile(root()); QVERIFY(profile.isValid());
        const auto url = tone(profile.filePath("tone.wav")); QVERIFY(!url.isEmpty());
        MusicController music({}, profile.path()); music.setVolume(0); music.openLocal(url);
        QTRY_VERIFY_WITH_TIMEOUT(music.playing(), 10000);
        music.setPlaying(false); music.clearQueue(); QTest::qWait(100);
        QVERIFY(!music.playing()); QVERIFY(!music.loading()); QVERIFY(music.current().isEmpty());
    }
    void backupSourcePreservesUserPause() {
        QTemporaryDir profile(root()); QVERIFY(profile.isValid());
        const auto file = tone(profile.filePath("tone.wav")); QFile wav(file.toLocalFile()); QVERIFY(wav.open(QIODevice::ReadOnly));
        const auto data = wav.readAll(); QTcpServer server; QVERIFY(server.listen(QHostAddress::LocalHost));
        connect(&server, &QTcpServer::newConnection, this, [&] {
            while (auto* socket = server.nextPendingConnection()) {
                connect(socket, &QTcpSocket::readyRead, socket, [socket, data] {
                    if (socket->property("sent").toBool()) return;
                    auto request = socket->property("request").toByteArray() + socket->readAll();
                    socket->setProperty("request", request); if (!request.contains("\r\n\r\n")) return;
                    socket->setProperty("sent", true);
                    qsizetype begin = 0, end = data.size() - 1;
                    const auto range = QRegularExpression("Range: bytes=(\\d+)-(\\d*)", QRegularExpression::CaseInsensitiveOption).match(QString::fromLatin1(request));
                    if (range.hasMatch()) { begin = range.captured(1).toLongLong(); if (!range.captured(2).isEmpty()) end = std::min(end, qsizetype(range.captured(2).toLongLong())); }
                    const auto payload = data.mid(begin, end - begin + 1);
                    QByteArray header = range.hasMatch() ? "HTTP/1.1 206 Partial Content\r\nContent-Range: bytes " + QByteArray::number(begin) + "-" + QByteArray::number(end) + "/" + QByteArray::number(data.size()) + "\r\n" : QByteArray("HTTP/1.1 200 OK\r\n");
                    socket->write(header + "Content-Type: audio/wav\r\nAccept-Ranges: bytes\r\nContent-Length: " + QByteArray::number(payload.size()) + "\r\nConnection: close\r\n\r\n" + payload);
                    socket->disconnectFromHost();
                });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
        const auto dll = qEnvironmentVariable("BOTTLE_ACCOUNT_TEST_DLL"); QVERIFY(!dll.isEmpty());
        QLibrary fixture(dll); fixture.setLoadHints(QLibrary::PreventUnloadHint); QVERIFY(fixture.load());
        const auto configure = reinterpret_cast<void(*)(const char*)>(fixture.resolve("EchoTestConfigure")); QVERIFY(configure);
        const auto base = QString("http://127.0.0.1:%1/").arg(server.serverPort());
        configure(QJsonDocument(QJsonObject{{"audio_url", base + "main.wav"}, {"backup_url", QJsonArray{base + "backup.wav"}}}).toJson(QJsonDocument::Compact).constData());
        MusicController music(dll, profile.path()); music.setVolume(0); music.initialize();
        QTRY_VERIFY(music.ready() && !music.feed().isEmpty()); music.playFrom("feed", 0);
        QTRY_VERIFY_WITH_TIMEOUT(music.playing() && music.position() > 200, 10000);
        music.setPlaying(false); QVERIFY(!music.playing());
        // Model a late transport failure after the user has paused a stream.
        music.mediaPlayer()->errorOccurred(QMediaPlayer::ResourceError, "late transport error");
        QTRY_VERIFY_WITH_TIMEOUT(music.mediaPlayer()->source().path() == "/backup.wav" && !music.loading(), 10000);
        QVERIFY(!music.playing()); music.finishSession();
    }
    void liveFangTrackUsesHeadphones() {
        const auto dll = qEnvironmentVariable("BOTTLE_LIVE_NATIVE_DLL");
        if (dll.isEmpty()) QSKIP("Explicit live Native DLL required");
        QTemporaryDir profile(root()); QVERIFY(profile.isValid());
        MusicController music(dll, profile.path()); music.setVolume(.08);
        EqualizerController equalizer(*music.mediaPlayer(), *music.audioOutput(), profile.path());
        QString endpoint;
        for (const auto& device : QMediaDevices::audioOutputs()) if (device.description().contains("ROSE", Qt::CaseInsensitive)) {
            endpoint = QString::fromUtf8(device.id()); equalizer.setDeviceId(QString::fromLatin1(device.id().toBase64())); break;
        }
        if (endpoint.isEmpty()) QSKIP("Connected headphone endpoint required");
        music.initialize(); QTRY_VERIFY_WITH_TIMEOUT(music.ready(), 15000);
        music.search(QStringLiteral("方大同")); QTRY_VERIFY_WITH_TIMEOUT(!music.searching(), 20000);
        QVERIFY(!music.searchResults().isEmpty()); music.playFrom("search", 0);
        const bool started = QTest::qWaitFor([&] { return music.playing() && music.position() > 1000 && !music.loading(); }, 25000);
        qInfo() << "Live track" << music.current().value("title") << music.current().value("artist")
                << "state" << music.mediaPlayer()->playbackState() << "status" << music.mediaPlayer()->mediaStatus() << "error" << music.mediaPlayer()->error();
        const auto evidence = qEnvironmentVariable("BOTTLE_TEST_TEMP_ROOT");
        QFile::copy(profile.filePath("playback-diagnostics.jsonl"), QDir(evidence).filePath("fang-live-playback.jsonl"));
        QFile::copy(profile.filePath("audio-diagnostics.jsonl"), QDir(evidence).filePath("fang-live-audio.jsonl"));
        QVERIFY(started);
#ifdef Q_OS_WIN
        QTRY_VERIFY_WITH_TIMEOUT(processAudioPeak(endpoint) > .0001f, 5000);
#endif
        music.finishSession();
    }
    void liveRadioConsumesItsFirstBatch() {
        const auto dll=qEnvironmentVariable("BOTTLE_LIVE_NATIVE_DLL");if(dll.isEmpty())QSKIP("Explicit live Native DLL required");
        QTemporaryDir profile(root());QVERIFY(profile.isValid());MusicController music(dll,profile.path());music.setVolume(.08);
        EqualizerController equalizer(*music.mediaPlayer(),*music.audioOutput(),profile.path());QString endpoint;
        for(const auto& device:QMediaDevices::audioOutputs())if(device.description().contains("ROSE",Qt::CaseInsensitive)){
            endpoint=QString::fromUtf8(device.id());equalizer.setDeviceId(QString::fromLatin1(device.id().toBase64()));break;
        }
        if(endpoint.isEmpty())QSKIP("Connected headphone endpoint required");
        music.initialize();QTRY_VERIFY_WITH_TIMEOUT(music.ready(),15000);music.startRadio();QTRY_VERIFY_WITH_TIMEOUT(!music.radioPending(),20000);
        QVERIFY2(music.radio()&&music.queue().size()>=2,"Live radio returned no usable first batch");
        const auto first=music.current();const auto second=music.queue()[1].toMap();
        const bool started=QTest::qWaitFor([&]{return music.playing()&&music.position()>1000&&!music.loading();},25000);
        qInfo()<<"First live radio track"<<first.value("title")<<first.value("artist")<<"started"<<started<<"error"<<music.mediaPlayer()->error();
        music.next();QCOMPARE(music.current().value("hash"),second.value("hash"));
        const bool nextStarted=QTest::qWaitFor([&]{return music.playing()&&music.position()>1000&&!music.loading();},25000);
        qInfo()<<"Second live radio track"<<music.current().value("title")<<music.current().value("artist")<<"started"<<nextStarted;
        const auto evidence=qEnvironmentVariable("BOTTLE_TEST_TEMP_ROOT");
        for(const auto& stream:{QString("playback"),QString("audio")}){
            const auto target=QDir(evidence).filePath("radio-live-"+stream+".jsonl");QFile::remove(target);QFile::copy(profile.filePath(stream+"-diagnostics.jsonl"),target);
        }
        QVERIFY(started);QVERIFY(nextStarted);
#ifdef Q_OS_WIN
        QTRY_VERIFY_WITH_TIMEOUT(processAudioPeak(endpoint)>.0001f,5000);
#endif
        music.finishSession();
    }
    void liveNativeStreamReachesAudioDevice() {
        const auto dll = qEnvironmentVariable("BOTTLE_LIVE_NATIVE_DLL");
        if (dll.isEmpty()) QSKIP("Explicit live Native DLL required");
        QVERIFY(!QMediaDevices::defaultAudioOutput().isNull());
        QTemporaryDir profile(root()); QVERIFY(profile.isValid());
        MusicController music(dll, profile.path()); music.setVolume(.15);
        EqualizerController equalizer(*music.mediaPlayer(), *music.audioOutput(), profile.path());
        music.initialize(); QTRY_VERIFY_WITH_TIMEOUT(music.ready(), 15000);
        QTRY_VERIFY_WITH_TIMEOUT(!music.feed().isEmpty(), 20000);
        music.playFrom("feed", 0);
        bool started=QTest::qWaitFor([&]{return music.playing()&&music.position()>1000;},25000);
        if(!started && !music.loading()) {
            qInfo()<<"Public source failed; testing a fresh resolution"<<music.current().value("title")<<music.mediaPlayer()->error();
            music.toggle();started=QTest::qWaitFor([&]{return music.playing()&&music.position()>1000;},25000);
        }
        const auto target=QDir(qEnvironmentVariable("BOTTLE_TEST_TEMP_ROOT")).filePath("public-live-playback.jsonl");QFile::remove(target);QFile::copy(profile.filePath("playback-diagnostics.jsonl"),target);
        QVERIFY(started);
        QVERIFY(!music.audioOutput()->isMuted());
#ifdef Q_OS_WIN
        QTRY_VERIFY_WITH_TIMEOUT(processAudioPeak() > .0001f, 5000);
#endif
        music.finishSession();
    }
};
QTEST_MAIN(AudioRuntimeTests)
#include "tst_audio_runtime.moc"
