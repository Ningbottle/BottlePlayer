#include "AppearanceController.h"
#include <QGuiApplication>
#include <QStyleHints>
#include <QDir>
#include <QRegularExpression>
#include <algorithm>

AppearanceController::AppearanceController(const QString& profile, QObject* parent)
    : QObject(parent), settings_(QDir(profile).filePath("appearance.ini"), QSettings::IniFormat) {
    const auto mode = settings_.value("mode", mode_).toString();
    if (mode == "light" || mode == "dark" || mode == "system") mode_ = mode;
    // Launch always opens the paper home; curtain is an in-session mode and must
    // not survive restarts as an implicit home choice.
    const auto accent = settings_.value("accent").toString();
    if (QRegularExpression("^#[0-9a-fA-F]{6}$").match(accent).hasMatch()) accent_ = accent;
    const auto font = settings_.value("lyricFont", font_).toString();
    if (font == "skin" || font == "kai" || font == "song" || font == "hei") font_ = font;
    const auto focus = settings_.value("lyricFocus", focus_).toString();
    if (focus == "readable" || focus == "stage") focus_ = focus;
    compact_ = settings_.value("compact", false).toBool(); lab_ = settings_.value("labMode", false).toBool();
    sidebarWidth_ = std::clamp(settings_.value("sidebarWidth", sidebarWidth_).toInt(), 200, 360);
    storageStatus_ = settings_.status();
    saveTimer_.setSingleShot(true); saveTimer_.setInterval(250);
    connect(&saveTimer_, &QTimer::timeout, this, [this] { flushStorage(); });
    if (qobject_cast<QGuiApplication*>(QCoreApplication::instance()))
        connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, [this] { if (mode_ == "system") emit changed(); });
}
AppearanceController::~AppearanceController() { flushStorage(); }
bool AppearanceController::dark() const {
    return mode_ == "dark" || (mode_ == "system" && qobject_cast<QGuiApplication*>(QCoreApplication::instance())
        && QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark);
}
void AppearanceController::scheduleSave(const QString& key, const QVariant& value) {
    // Do not call QSettings::setValue here: it can schedule its own disk write.
    pending_.insert(key, value); saveTimer_.start();
}
void AppearanceController::save(const QString& key, const QVariant& value) { scheduleSave(key, value); emit changed(); }
bool AppearanceController::flushStorage() {
    saveTimer_.stop();
    if (pending_.isEmpty() && storageStatus_ == QSettings::NoError) return true;
    // QSettings remembers its first error. A fresh writer lets a repaired
    // path recover, rather than permanently blocking imports and exports.
    QSettings writer(settings_.fileName(), QSettings::IniFormat);
    for (auto it = pending_.cbegin(); it != pending_.cend(); ++it) writer.setValue(it.key(), it.value());
    writer.sync();
    const auto status = writer.status();
    if (status == QSettings::NoError) pending_.clear();
    if (status != storageStatus_) { storageStatus_ = status; emit storageErrorChanged(); }
    return status == QSettings::NoError;
}
void AppearanceController::setMode(const QString& value) {
    if ((value != "light" && value != "dark" && value != "system") || mode_ == value) return;
    mode_ = value; save("mode", value);
}
void AppearanceController::setPresentation(const QString& value) {
    if ((value != "paper" && value != "curtain") || value == presentation_) return;
    presentation_ = value; save("presentation", value);
}
void AppearanceController::setAccent(const QString& value) {
    if ((!value.isEmpty() && !QRegularExpression("^#[0-9a-fA-F]{6}$").match(value).hasMatch()) || accent_ == value) return;
    accent_ = value; save("accent", value);
}
void AppearanceController::setCompact(bool value) { if (compact_ == value) return; compact_ = value; save("compact", value); }
void AppearanceController::setLyricFont(const QString& value) {
    if ((value != "skin" && value != "kai" && value != "song" && value != "hei") || font_ == value) return;
    font_ = value; save("lyricFont", value);
}
void AppearanceController::setLyricFocus(const QString& value) {
    if ((value != "readable" && value != "stage") || focus_ == value) return;
    focus_ = value; save("lyricFocus", value);
}
void AppearanceController::setLabMode(bool value) { if (lab_ == value) return; lab_ = value; save("labMode", value); }
void AppearanceController::setSidebarWidth(int width) {
    width = std::clamp(width, 200, 360); if (sidebarWidth_ == width) return;
    sidebarWidth_ = width; scheduleSave("sidebarWidth", width); emit sidebarWidthChanged();
}
void AppearanceController::retryStorage() { flushStorage(); }
