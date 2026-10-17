#include "ColorExtractor.h"
#include <QtTest>
#include <QBuffer>
#include <QDir>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>

class VisualContracts final : public QObject {
    Q_OBJECT
private:
    static QByteArray png(const QColor& color) {
        QImage image(48, 48, QImage::Format_RGB32); image.fill(color);
        QByteArray data; QBuffer buffer(&data); buffer.open(QIODevice::WriteOnly);
        image.save(&buffer, "PNG"); return data;
    }
private slots:
    void lateDownloadCannotReplaceNewCover() {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        int requests = 0;
        connect(&server, &QTcpServer::newConnection, &server, [&] {
            auto* socket = server.nextPendingConnection();
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
                if (socket->property("scheduled").toBool()) return;
                const auto request = socket->readAll();
                if (!request.contains("\r\n\r\n")) return;
                socket->setProperty("scheduled", true); ++requests;
                const bool old = request.contains("/old");
                const QByteArray body = png(old ? QColor("#df253d") : QColor("#2868dc"));
                QTimer::singleShot(old ? 350 : 5, socket, [socket, body] {
                    if (socket->state() == QAbstractSocket::ConnectedState) {
                        socket->write("HTTP/1.1 200 OK\r\nContent-Type: image/png\r\nContent-Length: "
                            + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                        socket->disconnectFromHost();
                    }
                });
            });
        });
        ColorExtractor extractor;
        const auto base = QString("http://127.0.0.1:%1/").arg(server.serverPort());
        extractor.setImageSource(QUrl(base + "old"));
        QTRY_COMPARE(requests, 1);
        extractor.setImageSource(QUrl(base + "new"));
        QTRY_VERIFY(!extractor.busy());
        QVERIFY(!extractor.dominantColors().isEmpty());
        const auto current = extractor.dominantColors();
        QVERIFY(current.first().blue() > current.first().red());
        QTest::qWait(450);
        QCOMPARE(extractor.dominantColors(), current);
        extractor.extractColorsFromUrl(QUrl(base + "new"));
        QVERIFY(!extractor.busy());
        QCOMPARE(requests, 2); // Cached cover does not refetch.
    }
    void clearingCoverInvalidatesPendingWork() {
        QTemporaryDir folder(QDir::current().filePath("bottle-palette-XXXXXX"));
        QVERIFY(folder.isValid());
        const auto path = folder.filePath("red.png");
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(png(QColor("#df253d"))); file.close();
        ColorExtractor extractor;
        extractor.setImageSource(QUrl::fromLocalFile(path));
        extractor.setImageSource({});
        const auto fallback = extractor.dominantColors();
        QVERIFY(!extractor.busy());
        QCOMPARE(fallback.size(), 3);
        QTest::qWait(200);
        QCOMPARE(extractor.dominantColors(), fallback);
    }
    void missingAndGrayscaleCoversHaveUsableFallback() {
        ColorExtractor extractor;
        extractor.setImageSource(QUrl::fromLocalFile("C:/nonexistent-bottle-cover-test.png"));
        QTRY_VERIFY(!extractor.busy());
        QCOMPARE(extractor.dominantColors().size(), 3);
        QTemporaryDir folder(QDir::current().filePath("bottle-palette-XXXXXX"));
        QVERIFY(folder.isValid());
        const auto path = folder.filePath("gray.png");
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); file.write(png(Qt::gray)); file.close();
        extractor.setImageSource(QUrl::fromLocalFile(path));
        QTRY_VERIFY(!extractor.busy());
        QCOMPARE(extractor.dominantColors().size(), 3);
        QVERIFY(extractor.dominantColors().first().isValid());
    }
};
QTEST_GUILESS_MAIN(VisualContracts)
#include "tst_visuals.moc"
