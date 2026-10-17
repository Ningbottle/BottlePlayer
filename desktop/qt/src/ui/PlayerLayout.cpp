#include "PlayerLayout.h"
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <cmath>
#include <limits>

namespace {
const QStringList ids{QStringLiteral("lyrics"), QStringLiteral("cover"), QStringLiteral("info"),
                      QStringLiteral("progress"), QStringLiteral("quality")};
QSize minimum(const QString& id) {
    if (id == QLatin1String("lyrics")) return {4, 3};
    if (id == QLatin1String("cover")) return {2, 2};
    if (id == QLatin1String("quality")) return {2, 1};
    return {3, 1};
}
QSize preferred(const QString& id) {
    if (id == QLatin1String("lyrics")) return {6, 6};
    if (id == QLatin1String("cover")) return {6, 5};
    if (id == QLatin1String("info")) return {6, 1};
    if (id == QLatin1String("quality")) return {3, 1};
    return {5, 1};
}
int area(const QRect& rect) { return rect.width() * rect.height(); }
}

PlayerLayout::PlayerLayout(const QString& profile, QObject* parent)
    : QObject(parent), cards_(defaults()) {
    // An invalid test/profile directory must never fall back to the CWD.
    if (profile.isEmpty()) return;
    settings_ = std::make_unique<QSettings>(QDir(profile).filePath("player-layout.ini"), QSettings::IniFormat);
    const auto payload = QJsonDocument::fromJson(settings_->value("layout").toByteArray()).object();
    Layout loaded;
    if (payload.value("version").toInt() == 1 && payload.value("cards").isObject()
        && parse(payload.value("cards").toObject().toVariantMap(), loaded)) {
        // Adopt the refined default only for the previous untouched layout.
        // User-created arrangements and hidden cards retain their geometry.
        const Layout previousDefault{{QStringLiteral("lyrics"), {5, 0, 7, 6}},
            {QStringLiteral("cover"), {0, 0, 5, 4}}, {QStringLiteral("info"), {0, 4, 5, 2}}};
        if (loaded != previousDefault) cards_ = std::move(loaded);
    }
}
PlayerLayout::Layout PlayerLayout::defaults() {
    return {{QStringLiteral("lyrics"), {6, 0, 6, 6}}, {QStringLiteral("cover"), {0, 0, 6, 5}},
            {QStringLiteral("info"), {0, 5, 6, 1}}};
}
QVariantMap PlayerLayout::toVariants(const Layout& layout) {
    QVariantMap value;
    for (auto it = layout.cbegin(); it != layout.cend(); ++it) {
        const QRect rect = it.value();
        value.insert(it.key(), QVariantMap{{"x", rect.x()}, {"y", rect.y()}, {"w", rect.width()}, {"h", rect.height()}});
    }
    return value;
}
QVariantMap PlayerLayout::cards() const { return toVariants(cards_); }
bool PlayerLayout::canPlace(const Layout& layout, const QString& id, const QRect& rect) {
    const QSize min = minimum(id);
    if (!ids.contains(id) || rect.x() < 0 || rect.y() < 0 || rect.x() + rect.width() > 12
        || rect.y() + rect.height() > 6 || rect.width() < min.width() || rect.height() < min.height()) return false;
    for (auto it = layout.cbegin(); it != layout.cend(); ++it)
        if (it.key() != id && rect.intersects(it.value())) return false;
    return true;
}
bool PlayerLayout::parse(const QVariantMap& value, Layout& layout) {
    Layout parsed;
    const auto json = QJsonObject::fromVariantMap(value);
    for (auto it = json.constBegin(); it != json.constEnd(); ++it) {
        if (!ids.contains(it.key()) || !it.value().isObject()) return false;
        const auto raw = it.value().toObject();
        for (const auto* field : {"x", "y", "w", "h"}) {
            const auto number = raw.value(QLatin1String(field));
            if (!number.isDouble() || !std::isfinite(number.toDouble())
                || std::floor(number.toDouble()) != number.toDouble()
                || number.toDouble() < 0 || number.toDouble() > 12) return false;
        }
        const QRect rect(raw.value("x").toInt(), raw.value("y").toInt(), raw.value("w").toInt(), raw.value("h").toInt());
        if (!canPlace(parsed, it.key(), rect)) return false;
        parsed.insert(it.key(), rect);
    }
    layout = std::move(parsed); return true;
}
QVariantMap PlayerLayout::result(bool ok, const Layout& layout) { return {{"ok", ok}, {"cards", toVariants(layout)}}; }
QRect PlayerLayout::nearestFree(const Layout& layout, const QString& id, QSize size, QPoint near) {
    QRect best; double bestDistance = std::numeric_limits<double>::infinity();
    for (int y = 0; y + size.height() <= 6; ++y) for (int x = 0; x + size.width() <= 12; ++x) {
        const QRect rect({x, y}, size);
        if (!canPlace(layout, id, rect)) continue;
        const double distance = std::hypot(x - near.x(), y - near.y());
        if (distance < bestDistance) { best = rect; bestDistance = distance; }
    }
    return best;
}
QVariantMap PlayerLayout::previewMove(const QString& id, int x, int y) const {
    if (!cards_.contains(id)) return result(false, cards_);
    const QRect current = cards_.value(id);
    const QRect target(std::clamp(x, 0, 12 - current.width()), std::clamp(y, 0, 6 - current.height()), current.width(), current.height());
    if (target == current) return result(true, cards_);
    Layout next = cards_; next[id] = target;
    QStringList covered;
    for (const QString& other : ids)
        if (other != id && next.contains(other) && target.intersects(next[other])) covered.append(other);
    std::stable_sort(covered.begin(), covered.end(), [&next](const QString& a, const QString& b) { return area(next[a]) > area(next[b]); });
    for (const QString& other : covered) next.remove(other);
    if (covered.size() == 1) {
        const QString other = covered.first();
        const QRect swapped(current.topLeft(), cards_[other].size());
        if (canPlace(next, other, swapped)) { next[other] = swapped; return result(true, next); }
    }
    for (const QString& other : covered) {
        const QRect previous = cards_[other];
        const QRect spot = nearestFree(next, other, previous.size(), previous.topLeft());
        if (!spot.isValid()) return result(false, cards_);
        next[other] = spot;
    }
    return result(true, next);
}
QVariantMap PlayerLayout::previewResize(const QString& id, int width, int height) const {
    if (!cards_.contains(id)) return result(false, cards_);
    const QRect current = cards_[id]; const QSize min = minimum(id);
    const int wantW = std::clamp(width, min.width(), 12 - current.x());
    const int wantH = std::clamp(height, min.height(), 6 - current.y());
    QRect best;
    for (int w = wantW; w >= min.width(); --w) for (int h = wantH; h >= min.height(); --h) {
        const QRect rect(current.topLeft(), QSize(w, h));
        if (!canPlace(cards_, id, rect)) continue;
        if (!best.isValid() || area(rect) > area(best)) best = rect;
        break;
    }
    if (!best.isValid()) return result(false, cards_);
    Layout next = cards_; next[id] = best; return result(true, next);
}
bool PlayerLayout::apply(Layout next) {
    if (next == cards_) return true;
    cards_ = std::move(next);
    const QJsonObject payload{{"version", 1}, {"cards", QJsonObject::fromVariantMap(cards())}};
    if (settings_) settings_->setValue("layout", QJsonDocument(payload).toJson(QJsonDocument::Compact));
    emit cardsChanged(); return true;
}
bool PlayerLayout::commit(const QVariantMap& value) {
    Layout next; return parse(value, next) && apply(std::move(next));
}
bool PlayerLayout::move(const QString& id, int x, int y) {
    const auto preview = previewMove(id, x, y);
    return preview.value("ok").toBool() && commit(preview.value("cards").toMap());
}
bool PlayerLayout::resize(const QString& id, int width, int height) {
    const auto preview = previewResize(id, width, height);
    return preview.value("ok").toBool() && commit(preview.value("cards").toMap());
}
bool PlayerLayout::hide(const QString& id) {
    if (!cards_.contains(id)) return false;
    Layout next = cards_; next.remove(id); return apply(std::move(next));
}
bool PlayerLayout::add(const QString& id) {
    if (!ids.contains(id) || cards_.contains(id)) return false;
    Layout next = cards_; const QSize pref = preferred(id), min = minimum(id);
    for (int attempt = 0; attempt < 72; ++attempt) {
        for (int h = pref.height(); h >= min.height(); --h) for (int w = pref.width(); w >= min.width(); --w)
            for (int y = 0; y + h <= 6; ++y) for (int x = 0; x + w <= 12; ++x) {
                const QRect spot(x, y, w, h);
                if (canPlace(next, id, spot)) { next[id] = spot; return apply(std::move(next)); }
            }
        QString largest; int largestArea = 0;
        for (const QString& other : ids) {
            if (!next.contains(other)) continue;
            const QRect rect = next[other]; const QSize limit = minimum(other);
            if ((rect.width() > limit.width() || rect.height() > limit.height()) && area(rect) > largestArea) {
                largest = other; largestArea = area(rect);
            }
        }
        if (largest.isEmpty()) return false;
        QRect rect = next[largest]; const QSize limit = minimum(largest);
        if (rect.width() - limit.width() >= rect.height() - limit.height()) rect.setWidth(rect.width() - 1);
        else rect.setHeight(rect.height() - 1);
        next[largest] = rect;
    }
    return false;
}
void PlayerLayout::reset() { apply(defaults()); }
