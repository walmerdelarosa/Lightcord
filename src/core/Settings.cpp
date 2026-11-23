// SPDX-License-Identifier: MIT
#include "Settings.h"

#include <QSettings>

namespace lightcord {

Settings &Settings::instance()
{
    static Settings s;
    return s;
}

QString Settings::theme() const
{
    return QSettings().value("ui/theme", "dark").toString();
}

void Settings::setTheme(const QString &theme)
{
    QSettings().setValue("ui/theme", theme);
    emit themeChanged();
}

int Settings::fontPointSize() const
{
    return QSettings().value("ui/fontPointSize", 10).toInt();
}

void Settings::setFontPointSize(int pt)
{
    QSettings().setValue("ui/fontPointSize", pt);
    emit themeChanged();
}

QByteArray Settings::inputDeviceId() const
{
    return QSettings().value("audio/inputDevice").toByteArray();
}

void Settings::setInputDeviceId(const QByteArray &id)
{
    QSettings().setValue("audio/inputDevice", id);
    emit audioDevicesChanged();
}

QByteArray Settings::outputDeviceId() const
{
    return QSettings().value("audio/outputDevice").toByteArray();
}

void Settings::setOutputDeviceId(const QByteArray &id)
{
    QSettings().setValue("audio/outputDevice", id);
    emit audioDevicesChanged();
}

audio::NoiseSuppressor::Settings Settings::noiseSuppression() const
{
    QSettings q;
    audio::NoiseSuppressor::Settings s;
    s.enabled = q.value("audio/ns/enabled", s.enabled).toBool();
    s.strength = q.value("audio/ns/strength", s.strength).toFloat();
    s.voiceGate = q.value("audio/ns/voiceGate", s.voiceGate).toBool();
    s.gateThreshold = q.value("audio/ns/gateThreshold", s.gateThreshold).toFloat();
    s.gateHoldMs = q.value("audio/ns/gateHoldMs", s.gateHoldMs).toInt();

    // 0.2.0 saved its default hold time (200 ms) as soon as the settings dialog was
    // used. It closed the gate between syllables, so a hold that still has exactly
    // that value is moved to the current default once.
    if (q.value("audio/ns/revision", 1).toInt() < 2) {
        const audio::NoiseSuppressor::Settings defaults;
        if (q.contains("audio/ns/gateHoldMs") && s.gateHoldMs == 200) {
            s.gateHoldMs = defaults.gateHoldMs;
            q.setValue("audio/ns/gateHoldMs", s.gateHoldMs);
        }
        q.setValue("audio/ns/revision", 2);
    }
    return s;
}

void Settings::setNoiseSuppression(const audio::NoiseSuppressor::Settings &s)
{
    QSettings q;
    q.setValue("audio/ns/enabled", s.enabled);
    q.setValue("audio/ns/strength", s.strength);
    q.setValue("audio/ns/voiceGate", s.voiceGate);
    q.setValue("audio/ns/gateThreshold", s.gateThreshold);
    q.setValue("audio/ns/gateHoldMs", s.gateHoldMs);
    q.setValue("audio/ns/revision", 2);
    emit noiseSuppressionChanged();
}

int Settings::screenShareFps() const
{
    return QSettings().value("screenshare/fps", 15).toInt();
}

void Settings::setScreenShareFps(int fps)
{
    QSettings().setValue("screenshare/fps", fps);
}

int Settings::screenShareMaxHeight() const
{
    return QSettings().value("screenshare/maxHeight", 720).toInt();
}

void Settings::setScreenShareMaxHeight(int px)
{
    QSettings().setValue("screenshare/maxHeight", px);
}

QByteArray Settings::windowGeometry() const
{
    return QSettings().value("ui/geometry").toByteArray();
}

void Settings::setWindowGeometry(const QByteArray &g)
{
    QSettings().setValue("ui/geometry", g);
}

QByteArray Settings::splitterState() const
{
    return QSettings().value("ui/splitter").toByteArray();
}

void Settings::setSplitterState(const QByteArray &s)
{
    QSettings().setValue("ui/splitter", s);
}

QString Settings::lastChannelId() const
{
    return QSettings().value("ui/lastChannel").toString();
}

void Settings::setLastChannelId(const QString &id)
{
    QSettings().setValue("ui/lastChannel", id);
}

} // namespace lightcord
