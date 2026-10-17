#pragma once
#include <QObject>
#include <QAbstractNativeEventFilter>
#include <memory>

class MusicController;
class QQuickWindow;
class MediaIntegration final : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY changed)
public:
    explicit MediaIntegration(MusicController& music, QObject* parent = nullptr);
    ~MediaIntegration() override;
    void attach(QQuickWindow* window);
    bool available() const;
    bool nativeEventFilter(const QByteArray& type, void* message, qintptr* result) override;
signals:
    void changed();
private:
    void update();
    void button(int command);
    struct Impl;
    std::unique_ptr<Impl> impl_;
    MusicController& music_;
};
