#include "core/DiagnosticWriter.h"
#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QElapsedTimer>
#include <QScopeGuard>
#include <future>
#include <vector>
#include <algorithm>
#include <stdexcept>

class DiagnosticsTests final : public QObject {
    Q_OBJECT
    QString root() const { return qEnvironmentVariable("BOTTLE_TEST_TEMP_ROOT", QDir::tempPath()) + "/bottle-diagnostics-XXXXXX"; }
private slots:
    void blockedIoDoesNotBlockEnqueueAndErrorsHaveReservedCapacity() {
        std::promise<void> entered, release; auto gate = release.get_future().share();
        std::vector<QByteArray> output;
        DiagnosticWriter writer([&](const DiagnosticRecord& record) {
            if (output.empty()) { entered.set_value(); gate.wait(); }
            output.push_back(record.bytes); return true;
        });
        const auto unblock = qScopeGuard([&] { if (gate.wait_for(std::chrono::seconds(0)) != std::future_status::ready) release.set_value(); });
        QVERIFY(writer.enqueue("fixture", "playback", {{"event","first"}}));
        QVERIFY(entered.get_future().wait_for(std::chrono::seconds(5)) == std::future_status::ready);
        for (int i=0;i<1000;++i) writer.enqueue("fixture","playback",{{"event","position"}});
        for (int i=0;i<65;++i) writer.enqueue("fixture","audio",{{"event","output_error"},{"error",1}});
        auto stats = writer.stats();
        QCOMPARE(stats.queued, DiagnosticWriter::Capacity); QCOMPARE(stats.peakQueued, DiagnosticWriter::Capacity);
        QCOMPARE(stats.accepted, quint64(513)); QCOMPARE(stats.dropped, quint64(553)); QCOMPARE(stats.criticalDropped, quint64(1));
        release.set_value(); writer.flush(); stats = writer.stats();
        QCOMPARE(stats.written, stats.accepted); QCOMPARE(stats.failed, quint64(0)); QCOMPARE(output.size(), size_t(513));
        QVERIFY(std::any_of(output.cbegin(),output.cend(),[](const QByteArray& row) { return QJsonDocument::fromJson(row).object().value("diagnosticDropped").toInt()==553; }));
    }
    void rotationStreamsAndOversizedRecordsStayBounded() {
        QTemporaryDir profile(root()); QVERIFY(profile.isValid()); DiagnosticWriter writer;
        QVERIFY(!writer.enqueue(profile.path(),"../unsafe",{{"event","bad_stream"}}));
        QVERIFY(!writer.enqueue(profile.path(),"audio",{{"padding",QString(DiagnosticWriter::MaxRecordBytes,'x')}}));
        for (int i=0;i<180;++i) QVERIFY(writer.enqueue(profile.path(),"playback",{{"i",i},{"padding",QString(4000,'x')}}));
        QVERIFY(writer.enqueue(profile.path(),"audio",{{"event","pause"}}));
        writer.flush(); const auto stats = writer.stats(); QCOMPARE(stats.written,quint64(181)); QCOMPARE(stats.dropped,quint64(2));
        QVERIFY(QFileInfo(profile.filePath("playback-diagnostics.jsonl.1")).exists());
        for (const auto& name : QDir(profile.path()).entryList(QDir::Files)) {
            QFile file(profile.filePath(name)); QVERIFY(file.size()<=DiagnosticWriter::MaxFileBytes); QVERIFY(file.open(QIODevice::ReadOnly));
            while (!file.atEnd()) { QJsonParseError error; QJsonDocument::fromJson(file.readLine(), &error); QCOMPARE(error.error,QJsonParseError::NoError); }
        }
        QCOMPARE(QDir(profile.path()).entryList(QDir::Files).size(),3);
    }
    void fileFailureRecoversAndShutdownDrainsBeforeRejectingNewWork() {
        QTemporaryDir profile(root()); QVERIFY(profile.isValid());
        QFile obstruction(profile.filePath("destination")); QVERIFY(obstruction.open(QIODevice::WriteOnly)); obstruction.write("fixture"); obstruction.close();
        DiagnosticWriter writer;
        QVERIFY(writer.enqueue(obstruction.fileName(),"requests",{{"event","request"}})); writer.flush(); QCOMPARE(writer.stats().failed,quint64(1));
        QVERIFY(obstruction.remove()); QVERIFY(QDir().mkpath(obstruction.fileName()));
        QVERIFY(writer.enqueue(obstruction.fileName(),"requests",{{"event","recovered"}})); writer.shutdown();
        QCOMPARE(writer.stats().written,quint64(1)); QVERIFY(!writer.enqueue(obstruction.fileName(),"audio",{{"event","late"}}));
        QFile recovered(QDir(obstruction.fileName()).filePath("requests-diagnostics.jsonl")); QVERIFY(recovered.open(QIODevice::ReadOnly));
        QCOMPARE(QJsonDocument::fromJson(recovered.readLine()).object().value("diagnosticWriteFailures").toInt(),1);
        writer.flush(); writer.shutdown();
    }
    void sinkExceptionsAreContained() {
        int calls=0; DiagnosticWriter writer([&](const DiagnosticRecord&) { if (++calls==1) throw std::runtime_error("fixture"); return calls>2; });
        for(int i=0;i<3;++i) QVERIFY(writer.enqueue("fixture","audio",{{"event","test"}}));
        writer.flush(); QCOMPARE(writer.stats().failed,quint64(2)); QCOMPARE(writer.stats().written,quint64(1));
    }
    void lossMetadataFitsRecordBudgetAndTornTailIsRepaired() {
        std::vector<QByteArray> rows; DiagnosticWriter capture([&](const DiagnosticRecord& row) { rows.push_back(row.bytes); return true; });
        QVERIFY(!capture.enqueue("fixture","invalid",{}));
        QVERIFY(capture.enqueue("fixture","audio",{{"padding",QString(DiagnosticWriter::MaxRecordBytes-DiagnosticWriter::MetadataReserve-100,'x')}}));
        capture.flush(); QCOMPARE(rows.size(),size_t(1)); QVERIFY(rows[0].size()<=DiagnosticWriter::MaxRecordBytes);
        QCOMPARE(QJsonDocument::fromJson(rows[0]).object().value("diagnosticDropped").toInt(),1);
        QTemporaryDir profile(root()); QVERIFY(profile.isValid()); QFile torn(profile.filePath("audio-diagnostics.jsonl"));
        QVERIFY(torn.open(QIODevice::WriteOnly)); torn.write("{\"previous\":true}\n{\"torn\":"); torn.close();
        DiagnosticWriter writer; QVERIFY(writer.enqueue(profile.path(),"audio",{{"event","recovered"}})); writer.flush();
        QVERIFY(torn.open(QIODevice::ReadOnly)); QCOMPARE(QJsonDocument::fromJson(torn.readLine()).object().value("previous").toBool(),true);
        QCOMPARE(QJsonDocument::fromJson(torn.readLine()).object().value("event").toString(),QString("recovered")); QVERIFY(torn.atEnd());
    }
    void concurrentCallerLatencyComparedWithSynchronousFileSink() {
        QTemporaryDir profile(root()); QVERIFY(profile.isValid());
        std::mutex baselineMutex; std::vector<qint64> baseline(400), queued(400);
        const auto measure = [&](auto&& sink, std::vector<qint64>& samples) {
            std::vector<std::thread> threads;
            for(int t=0;t<4;++t) threads.emplace_back([&,t] { for(int i=0;i<100;++i) { QElapsedTimer clock; clock.start(); sink(t,i); samples[size_t(t*100+i)]=clock.nsecsElapsed(); } });
            for(auto& thread:threads) thread.join();
        };
        measure([&](int t,int i) { std::lock_guard lock(baselineMutex); QFile file(profile.filePath("synchronous.jsonl")); if(file.open(QIODevice::Append)) file.write(QJsonDocument(QJsonObject{{"t",t},{"i",i}}).toJson(QJsonDocument::Compact)+'\n'); },baseline);
        DiagnosticWriter writer;
        measure([&](int t,int i) { writer.enqueue(profile.path(),"playback",{{"t",t},{"i",i}}); },queued); writer.flush();
        QCOMPARE(writer.stats().written,quint64(400)); QCOMPARE(writer.stats().dropped,quint64(0));
        std::sort(baseline.begin(),baseline.end()); std::sort(queued.begin(),queued.end());
        qInfo() << "400 concurrent calls, p95 caller us, synchronous:" << baseline[379]/1000.0 << "queued:" << queued[379]/1000.0;
    }
};
QTEST_GUILESS_MAIN(DiagnosticsTests)
#include "tst_diagnostics.moc"
