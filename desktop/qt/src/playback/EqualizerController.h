#pragma once
#include <QObject>
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QAudioBufferOutput>
#include <QMediaDevices>
#include <QAudioDevice>
#include <QSettings>
#include <QThread>
#include <QVariantList>
#include <memory>

class AudioWorker;
struct AudioDispatch;
class EqualizerController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY changed)
    Q_PROPERTY(bool allowed READ allowed NOTIFY changed)
    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(QString reason READ reason NOTIFY changed)
    Q_PROPERTY(QVariantList bands READ bands NOTIFY changed)
    Q_PROPERTY(QVariantList devices READ devices NOTIFY changed)
    Q_PROPERTY(QString deviceId READ deviceId WRITE setDeviceId NOTIFY changed)
    Q_PROPERTY(QString activeDeviceName READ activeDeviceName NOTIFY changed)
    Q_PROPERTY(QString preset READ preset NOTIFY changed)
public:
    EqualizerController(QMediaPlayer& player, QAudioOutput& output, const QString& profile, QObject* parent = nullptr);
    ~EqualizerController() override;
    bool enabled() const { return enabled_; }
    bool allowed() const { return allowed_; }
    bool active() const { return active_; }
    QString reason() const { return reason_; }
    QVariantList bands() const { return bands_; }
    QVariantList devices() const;
    QString deviceId() const { return deviceId_; }
    QString activeDeviceName() const { return output_.device().description(); }
    QString preset() const { return preset_; }
    void setEnabled(bool enabled);
    void setAllowed(bool allowed);
    void setDeviceId(const QString& id);
    Q_INVOKABLE void setBand(int index, double gain);
    Q_INVOKABLE void applyPreset(const QString& preset);
signals:
    void changed();
private:
    void updateWorker();
    void traceOutput(const QString& event, const QString& reason = {});
    QMediaPlayer& player_;
    QAudioOutput& output_;
    QSettings settings_;
    QMediaDevices mediaDevices_;
    std::unique_ptr<QAudioBufferOutput> buffers_;
    QThread workerThread_;
    AudioWorker* worker_ = nullptr;
    std::shared_ptr<AudioDispatch> dispatch_;
    QVariantList bands_;
    QString deviceId_, preset_ = "Flat", reason_ = QStringLiteral("实验室模式关闭，使用原始音频");
    bool enabled_ = false, allowed_ = false, active_ = false, routed_ = false;
    qint64 previousPosition_ = -1;
};
