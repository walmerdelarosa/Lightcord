// SPDX-License-Identifier: MIT
#pragma once

#include "audio/NoiseSuppressor.h"

#include <QByteArray>
#include <QObject>
#include <QString>

namespace lightcord {

// Typed access to the persistent user settings (QSettings, INI on disk).
class Settings : public QObject {
    Q_OBJECT
public:
    static Settings &instance();

    QString theme() const;            // "dark" | "light"
    void setTheme(const QString &theme);

    int fontPointSize() const;
    void setFontPointSize(int pt);

    QByteArray inputDeviceId() const;  // empty = system default
    void setInputDeviceId(const QByteArray &id);
    QByteArray outputDeviceId() const;
    void setOutputDeviceId(const QByteArray &id);

    audio::NoiseSuppressor::Settings noiseSuppression() const;
    void setNoiseSuppression(const audio::NoiseSuppressor::Settings &s);

    int screenShareFps() const;        // 5 | 15 | 30
    void setScreenShareFps(int fps);
    int screenShareMaxHeight() const;  // 720 | 1080 | 0 (native)
    void setScreenShareMaxHeight(int px);

    bool experimentalVoiceSignaling() const;
    void setExperimentalVoiceSignaling(bool on);

    QByteArray windowGeometry() const;
    void setWindowGeometry(const QByteArray &g);
    QByteArray splitterState() const;
    void setSplitterState(const QByteArray &s);
    QString lastChannelId() const;
    void setLastChannelId(const QString &id);

signals:
    void themeChanged();
    void noiseSuppressionChanged();

private:
    Settings() = default;
};

} // namespace lightcord
