#pragma once
#include <QObject>
#include <QRect>
#include <QMap>
#include <QSettings>
#include <QVariantMap>
#include <memory>

// Window-contained player layout contract. Layout state is independent of audio
// and persists only on a committed gesture, never during pointer movement.
class PlayerLayout final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap cards READ cards NOTIFY cardsChanged)
public:
    explicit PlayerLayout(const QString& profile, QObject* parent = nullptr);
    QVariantMap cards() const;
    Q_INVOKABLE QVariantMap previewMove(const QString& id, int x, int y) const;
    Q_INVOKABLE QVariantMap previewResize(const QString& id, int width, int height) const;
    Q_INVOKABLE bool commit(const QVariantMap& cards);
    Q_INVOKABLE bool move(const QString& id, int x, int y);
    Q_INVOKABLE bool resize(const QString& id, int width, int height);
    Q_INVOKABLE bool hide(const QString& id);
    Q_INVOKABLE bool add(const QString& id);
    Q_INVOKABLE void reset();
signals:
    void cardsChanged();
private:
    using Layout = QMap<QString, QRect>;
    static Layout defaults();
    static QVariantMap toVariants(const Layout& layout);
    static bool parse(const QVariantMap& value, Layout& layout);
    static bool canPlace(const Layout& layout, const QString& id, const QRect& rect);
    static QRect nearestFree(const Layout& layout, const QString& id, QSize size, QPoint near);
    static QVariantMap result(bool ok, const Layout& layout);
    bool apply(Layout next);
    Layout cards_;
    std::unique_ptr<QSettings> settings_;
};
