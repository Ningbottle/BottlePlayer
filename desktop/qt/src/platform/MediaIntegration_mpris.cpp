#include "MediaIntegration.h"
#include "music/MusicController.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QPointer>
#include <QQuickWindow>
#include <QUrl>

namespace {
constexpr auto kMprisService = "org.mpris.MediaPlayer2.bottlemusic";
constexpr auto kMprisObjectPath = "/org/mpris/MediaPlayer2";
constexpr auto kMprisPlayerInterface = "org.mpris.MediaPlayer2.Player";
}

struct MediaIntegration::Impl {
    bool available = false;
    bool adaptorsInstalled = false;
    QPointer<QQuickWindow> quickWindow;
};

QVariantMap currentMetadata(const MusicController& music) {
    const auto track = music.current();
    if (track.isEmpty())
        return {{QStringLiteral("mpris:trackid"), QVariant::fromValue(QDBusObjectPath(QStringLiteral("/org/mpris/MediaPlayer2/TrackList/NoTrack")))}};
    const auto identity = track.value("hash").toString() + QChar(u'\0') + track.value("title").toString()
        + QChar(u'\0') + track.value("artist").toString() + QChar(u'\0') + track.value("album").toString();
    const auto digest = QCryptographicHash::hash(identity.toUtf8(), QCryptographicHash::Sha256).toHex();
    const auto trackId = QStringLiteral("/org/mpris/MediaPlayer2/track/t") + QString::fromLatin1(digest);
    QVariantMap result{
        {QStringLiteral("mpris:trackid"), QVariant::fromValue(QDBusObjectPath(trackId))},
        {QStringLiteral("xesam:title"), track.value("title").toString()},
        {QStringLiteral("xesam:artist"), QStringList{track.value("artist").toString()}},
        {QStringLiteral("xesam:album"), track.value("album").toString()}
    };
    const QUrl artwork(track.value("image").toString());
    if (artwork.isValid() && !artwork.host().isEmpty()
        && (artwork.scheme() == QStringLiteral("http") || artwork.scheme() == QStringLiteral("https")))
        result.insert(QStringLiteral("mpris:artUrl"), artwork.toString(QUrl::FullyEncoded));
    const qint64 duration = music.duration();
    if (duration > 0) result.insert(QStringLiteral("mpris:length"), QVariant::fromValue<qlonglong>(duration * 1000));
    return result;
}

class MprisRootAdaptor final : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2")
    Q_PROPERTY(QString Identity READ identity)
    Q_PROPERTY(QString DesktopEntry READ desktopEntry)
    Q_PROPERTY(bool CanRaise READ canRaise)
    Q_PROPERTY(bool CanQuit READ canQuit)
    Q_PROPERTY(bool HasTrackList READ hasTrackList)
    Q_PROPERTY(QStringList SupportedUriSchemes READ supportedUriSchemes)
    Q_PROPERTY(QStringList SupportedMimeTypes READ supportedMimeTypes)
public:
    explicit MprisRootAdaptor(MediaIntegration* owner) : QDBusAbstractAdaptor(owner), owner_(owner) {}
    QString identity() const { return QStringLiteral("BottleMusic"); }
    QString desktopEntry() const { return QStringLiteral("bottlemusic"); }
    bool canRaise() const { return true; }
    bool canQuit() const { return false; }
    bool hasTrackList() const { return false; }
    QStringList supportedUriSchemes() const { return {}; }
    QStringList supportedMimeTypes() const { return {}; }
public slots:
    void Raise() {
        if (!owner_ || !owner_->impl_->quickWindow) return;
        owner_->impl_->quickWindow->showNormal();
        owner_->impl_->quickWindow->raise();
        owner_->impl_->quickWindow->requestActivate();
    }
private:
    QPointer<MediaIntegration> owner_;
};

class MprisPlayerAdaptor final : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")
    Q_PROPERTY(QString PlaybackStatus READ playbackStatus)
    Q_PROPERTY(QVariantMap Metadata READ metadata)
    Q_PROPERTY(bool CanGoNext READ canGoNext)
    Q_PROPERTY(bool CanGoPrevious READ canGoPrevious)
    Q_PROPERTY(bool CanPlay READ canPlay)
    Q_PROPERTY(bool CanPause READ canPause)
    Q_PROPERTY(bool CanControl READ canControl)
    Q_PROPERTY(qlonglong Position READ position)
public:
    explicit MprisPlayerAdaptor(MediaIntegration* owner) : QDBusAbstractAdaptor(owner), owner_(owner) {}
    QString playbackStatus() const {
        if (!owner_ || owner_->music_.queueIndex() < 0) return QStringLiteral("Stopped");
        return owner_->music_.playing() ? QStringLiteral("Playing") : QStringLiteral("Paused");
    }
    QVariantMap metadata() const { return owner_ ? currentMetadata(owner_->music_) : QVariantMap{}; }
    bool canGoNext() const { return owner_ && owner_->music_.queueIndex() >= 0; }
    bool canGoPrevious() const { return owner_ && owner_->music_.queueIndex() >= 0 && !owner_->music_.radio(); }
    bool canPlay() const { return owner_ && owner_->music_.queueIndex() >= 0; }
    bool canPause() const { return owner_ && owner_->music_.queueIndex() >= 0; }
    bool canControl() const { return bool(owner_); }
    qlonglong position() const { return owner_ ? qlonglong(owner_->music_.position()) * 1000 : 0; }
public slots:
    void PlayPause() { if (owner_) owner_->button(0); }
    void Play() { if (owner_) owner_->button(1); }
    void Pause() { if (owner_) owner_->button(2); }
    void Next() { if (owner_) owner_->button(3); }
    void Previous() { if (owner_) owner_->button(4); }
    void Stop() { if (owner_) owner_->button(2); }
private:
    QPointer<MediaIntegration> owner_;
};

MediaIntegration::MediaIntegration(MusicController& music, QObject* parent)
    : QObject(parent), impl_(std::make_unique<Impl>()), music_(music) {
    QCoreApplication::instance()->installNativeEventFilter(this);
    connect(&music_, &MusicController::playbackChanged, this, &MediaIntegration::update);
    connect(&music_, &MusicController::queueChanged, this, &MediaIntegration::update);
    connect(&music_, &MusicController::queueIndexChanged, this, &MediaIntegration::update);
    connect(&music_, &MusicController::radioChanged, this, &MediaIntegration::update);
}

MediaIntegration::~MediaIntegration() {
    QCoreApplication::instance()->removeNativeEventFilter(this);
    if (impl_->available) {
        auto bus = QDBusConnection::sessionBus();
        bus.unregisterService(QString::fromLatin1(kMprisService));
        bus.unregisterObject(QString::fromLatin1(kMprisObjectPath));
    }
}

bool MediaIntegration::available() const { return impl_->available; }

void MediaIntegration::attach(QQuickWindow* window) {
    if (!window || impl_->available) return;
    impl_->quickWindow = window;
    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) { emit changed(); return; }
    if (!impl_->adaptorsInstalled) {
        new MprisRootAdaptor(this);
        new MprisPlayerAdaptor(this);
        impl_->adaptorsInstalled = true;
    }
    const auto objectPath = QString::fromLatin1(kMprisObjectPath);
    if (!bus.registerObject(objectPath, this, QDBusConnection::ExportAdaptors)) { emit changed(); return; }
    if (!bus.registerService(QString::fromLatin1(kMprisService))) {
        bus.unregisterObject(objectPath);
        emit changed();
        return;
    }
    impl_->available = true;
    update();
    emit changed();
}

void MediaIntegration::button(int command) {
    if (command == 1) music_.setPlaying(true);
    else if (command == 2) music_.setPlaying(false);
    else if (command == 3) music_.next();
    else if (command == 4) music_.previous();
    else music_.toggle();
}

void MediaIntegration::update() {
    if (!impl_->available) return;
    const auto selected = music_.queueIndex() >= 0;
    const QVariantMap properties{
        {QStringLiteral("PlaybackStatus"), music_.playing() ? QStringLiteral("Playing") : selected ? QStringLiteral("Paused") : QStringLiteral("Stopped")},
        {QStringLiteral("Metadata"), currentMetadata(music_)},
        {QStringLiteral("CanGoNext"), selected},
        {QStringLiteral("CanGoPrevious"), selected && !music_.radio()},
        {QStringLiteral("CanPlay"), selected},
        {QStringLiteral("CanPause"), selected},
        {QStringLiteral("CanControl"), true},
        {QStringLiteral("Position"), QVariant::fromValue<qlonglong>(qlonglong(music_.position()) * 1000)}
    };
    auto signal = QDBusMessage::createSignal(QString::fromLatin1(kMprisObjectPath),
        QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("PropertiesChanged"));
    signal << QString::fromLatin1(kMprisPlayerInterface) << properties << QStringList{};
    QDBusConnection::sessionBus().send(signal);
}

bool MediaIntegration::nativeEventFilter(const QByteArray& type, void* raw, qintptr* result) {
    Q_UNUSED(type);
    Q_UNUSED(raw);
    Q_UNUSED(result);
    return false;
}

#include "MediaIntegration_mpris.moc"
