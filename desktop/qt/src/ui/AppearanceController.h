#pragma once
#include <QObject>
#include <QSettings>

class AppearanceController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString mode READ mode WRITE setMode NOTIFY changed)
    Q_PROPERTY(QString presentation READ presentation WRITE setPresentation NOTIFY changed)
    Q_PROPERTY(bool dark READ dark NOTIFY changed)
    Q_PROPERTY(QString accent READ accent WRITE setAccent NOTIFY changed)
    Q_PROPERTY(bool compact READ compact WRITE setCompact NOTIFY changed)
    Q_PROPERTY(QString lyricFont READ lyricFont WRITE setLyricFont NOTIFY changed)
    Q_PROPERTY(QString lyricFocus READ lyricFocus WRITE setLyricFocus NOTIFY changed)
    Q_PROPERTY(bool labMode READ labMode WRITE setLabMode NOTIFY changed)
    Q_PROPERTY(int sidebarWidth READ sidebarWidth WRITE setSidebarWidth NOTIFY changed)
    Q_PROPERTY(QString storageError READ storageError NOTIFY changed)
public:
    explicit AppearanceController(const QString& profile, QObject* parent = nullptr);
    QString mode() const { return mode_; }
    QString presentation() const { return presentation_; }
    void setPresentation(const QString& value);
    bool dark() const;
    QString accent() const { return accent_; }
    bool compact() const { return compact_; }
    QString lyricFont() const { return font_; }
    QString lyricFocus() const { return focus_; }
    bool labMode() const { return lab_; }
    int sidebarWidth() const { return sidebarWidth_; }
    void setSidebarWidth(int width);
    QString storageError() const { return settings_.status() == QSettings::NoError ? QString{} : QStringLiteral("外观设置暂时无法保存"); }
    void setMode(const QString& value);
    void setAccent(const QString& value);
    void setCompact(bool value);
    void setLyricFont(const QString& value);
    void setLyricFocus(const QString& value);
    void setLabMode(bool value);
    Q_INVOKABLE void retryStorage();
signals:
    void changed();
private:
    void save(const QString& key, const QVariant& value);
    QSettings settings_;
    QString mode_ = "light", presentation_ = "paper", accent_, font_ = "skin", focus_ = "readable";
    bool compact_ = false, lab_ = false;
    int sidebarWidth_ = 232;
};
