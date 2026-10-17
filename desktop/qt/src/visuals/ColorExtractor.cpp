// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 QueMusic Contributors
// Modified by BottleMusic, 2026-10-12: request-generation/cancellation fixes,
// resource/file support, cache-first loading and a restrained fallback palette.
//
#include "ColorExtractor.h"

#include <QNetworkReply>
#include <QtConcurrent>

#include <algorithm>

namespace {

constexpr int kColorCount = 3;
constexpr int kSampleSize = 48; // 固定采样，控制遍历量
constexpr int kSimilarDistance = 120;
constexpr int kSimilarDistanceSq = kSimilarDistance * kSimilarDistance;

// 封面无有效主色（纯灰/纯白）时的兜底配色
QVector<QColor> defaultColors()
{
    return { QColor("#596b8a"), QColor("#89616f"), QColor("#857969") };
}

} // namespace

ColorExtractor::ColorExtractor(QObject *parent)
    : QObject(parent)
    , m_networkManager(new QNetworkAccessManager(this))
{
    m_cache.setMaxCost(128);
    connect(m_networkManager, &QNetworkAccessManager::finished,
            this, &ColorExtractor::onImageDownloaded);
}

QUrl ColorExtractor::imageSource() const
{
    return m_imageSource;
}

void ColorExtractor::setImageSource(const QUrl &source)
{
    if (m_imageSource == source)
        return;
    extractColorsFromUrl(source);
}

QVector<QColor> ColorExtractor::dominantColors() const
{
    return m_dominantColors;
}

bool ColorExtractor::busy() const
{
    return m_busy;
}

void ColorExtractor::extractColorsFromUrl(const QUrl &url)
{
    // Increment before cancelling or starting I/O: a slow download must never
    // become the newest task merely because it completes after a newer cover.
    const int generation = ++m_generation;
    if (m_reply) m_reply->abort();
    if (m_imageSource != url) {
        m_imageSource = url;
        emit imageSourceChanged();
    }
    if (ExtractionResult *cached = m_cache.object(url.toString())) {
        applyResult(*cached);
        setBusy(false);
        return;
    }
    if (url.isEmpty() || (!url.isLocalFile() && url.scheme() != "qrc"
                         && url.scheme() != "http" && url.scheme() != "https")) {
        applyResult({defaultColors()});
        setBusy(false);
        return;
    }
    setBusy(true);
    if (url.isLocalFile() || url.scheme() == "qrc") {
        const QString path = url.isLocalFile() ? url.toLocalFile() : ":" + url.path();
        runTask(url.toString(), [path]() {
            QImage image;
            image.load(path);
            return extract(image);
        }, generation);
    } else {
        QNetworkRequest request(url);
        request.setTransferTimeout(10000);
        m_reply = m_networkManager->get(request);
        m_reply->setProperty("generation", generation);
        m_reply->setProperty("cacheKey", url.toString());
    }
}

void ColorExtractor::onImageDownloaded(QNetworkReply *reply)
{
    reply->deleteLater();

    const int generation = reply->property("generation").toInt();
    if (generation != m_generation)
        return;
    if (reply->error() != QNetworkReply::NoError) {
        applyResult({defaultColors()});
        setBusy(false);
        return;
    }
    const QByteArray data = reply->readAll();
    const QString cacheKey = reply->property("cacheKey").toString();
    // 网络图解码也放后台
    runTask(cacheKey, [data]() {
        QImage image;
        image.loadFromData(data);
        return extract(image);
    }, generation);
}

void ColorExtractor::runTask(const QString &cacheKey, const std::function<ExtractionResult()> &task, int generation)
{
    if (cacheKey.isEmpty())
        return;

    if (ExtractionResult *cached = m_cache.object(cacheKey)) {
        applyResult(*cached);
        return;
    }

    setBusy(true);

    auto *watcher = new QFutureWatcher<ExtractionResult>(this);
    connect(watcher, &QFutureWatcher<ExtractionResult>::finished, this,
            [this, watcher, generation, cacheKey]() {
                const ExtractionResult result = watcher->result();
                watcher->deleteLater();

                // 期间又切歌，丢弃过期结果
                if (generation != m_generation)
                    return;

                if (!result.colors.isEmpty())
                    m_cache.insert(cacheKey, new ExtractionResult(result));
                applyResult(result);
                setBusy(false);
            });
    watcher->setFuture(QtConcurrent::run(task));
}

void ColorExtractor::applyResult(const ExtractionResult &result)
{
    m_dominantColors = result.colors;
    emit colorsExtracted(m_dominantColors);

    QStringList colorStrings;
    colorStrings.reserve(m_dominantColors.size());
    for (const QColor &color : m_dominantColors)
        colorStrings.append(color.name());
    emit colorsExtractedAsString(colorStrings);
}

void ColorExtractor::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    emit busyChanged();
}

// 工作线程：封面取色
ExtractionResult ColorExtractor::extract(const QImage &image)
{
    ExtractionResult result;
    if (image.isNull()) {
        result.colors = defaultColors();
        return result;
    }

    result.colors = computeDominantColors(image, kColorCount);
    return result;
}

QVector<QColor> ColorExtractor::computeDominantColors(const QImage &image, int count)
{
    if (image.isNull() || count <= 0)
        return defaultColors();

    // 先降采样再统计
    const QImage sampled = (image.width() > kSampleSize || image.height() > kSampleSize)
        ? image.scaled(kSampleSize, kSampleSize, Qt::KeepAspectRatio, Qt::FastTransformation)
        : image;
    const QImage rgb = sampled.format() == QImage::Format_RGB32
        ? sampled
        : sampled.convertToFormat(QImage::Format_RGB32);

    struct Bucket
    {
        quint64 r = 0;
        quint64 g = 0;
        quint64 b = 0;
        quint32 n = 0;
    };

    // 量化打包成 15 位整型键，比拼接 QString 快一个数量级
    QHash<quint32, Bucket> buckets;
    buckets.reserve(512);

    for (int y = 0; y < rgb.height(); ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(rgb.constScanLine(y));
        for (int x = 0; x < rgb.width(); ++x) {
            const QRgb pixel = line[x];
            const int r = qRed(pixel);
            const int g = qGreen(pixel);
            const int b = qBlue(pixel);

            if (r + g + b > 690) // 平均亮度 > 230，近白
                continue;

            const int maxVal = qMax(r, qMax(g, b));
            const int minVal = qMin(r, qMin(g, b));
            if (maxVal - minVal < 5) // 近灰色，对主题色无贡献
                continue;

            const quint32 key = (quint32(r >> 3) << 10) | (quint32(g >> 3) << 5)
                              | quint32(b >> 3);
            Bucket &bucket = buckets[key];
            bucket.r += r;
            bucket.g += g;
            bucket.b += b;
            ++bucket.n;
        }
    }

    if (buckets.isEmpty())
        return defaultColors();

    QVector<QPair<quint32, QColor>> ranked;
    ranked.reserve(buckets.size());
    for (auto it = buckets.constBegin(); it != buckets.constEnd(); ++it) {
        const Bucket &bucket = it.value();
        ranked.append({ bucket.n, QColor(int(bucket.r / bucket.n),
                                        int(bucket.g / bucket.n),
                                        int(bucket.b / bucket.n)) });
    }
    std::sort(ranked.begin(), ranked.end(),
              [](const QPair<quint32, QColor> &a, const QPair<quint32, QColor> &b) {
                  return a.first > b.first;
              });

    // 依次挑出互不相似的颜色并提亮
    QVector<QColor> result;
    result.reserve(count);
    for (const QPair<quint32, QColor> &entry : ranked) {
        if (result.size() >= count)
            break;

        const QColor &color = entry.second;
        bool similar = false;
        for (const QColor &selected : result) {
            const int dr = color.red() - selected.red();
            const int dg = color.green() - selected.green();
            const int db = color.blue() - selected.blue();
            if (dr * dr + dg * dg + db * db < kSimilarDistanceSq) { // 平方距离，省开方
                similar = true;
                break;
            }
        }
        if (similar)
            continue;

        const QColor hsv = color.toHsv();
        result.append(QColor::fromHsv(hsv.hue(),
                                      qMin(255, hsv.saturation() + 42),
                                      qBound(30, hsv.value() - 32, 200)));
    }

    return result.isEmpty() ? defaultColors() : result;
}
