// SPDX-License-Identifier: MIT
#pragma once

#include <QElapsedTimer>
#include <QImage>
#include <QObject>
#include <QPointer>
#include <QScreen>
#include <QString>
#include <QTimer>
#include <QVector>

namespace lightcord::screenshare {

struct CaptureSource {
    enum class Kind { Screen, Window };
    Kind kind = Kind::Screen;
    QString title;
    QPointer<QScreen> screen;   // Kind::Screen
    quintptr windowHandle = 0;  // Kind::Window (HWND on Windows)
    QImage thumbnail;

    bool isValid() const { return kind == Kind::Screen ? !screen.isNull() : windowHandle != 0; }
};

// Simple, dependency-free screen/window capturer.
//
// Captures a whole screen (all platforms, via QScreen) or a single window
// (Windows, via PrintWindow with PW_RENDERFULLCONTENT so that GPU-composited
// apps are captured too) at a fixed frame rate, scaled down to a maximum
// height. Frames are delivered as QImage; the video encoder and the Discord
// "Go Live" transport consume them (see docs/ROADMAP.md).
class ScreenCapturer : public QObject {
    Q_OBJECT
public:
    explicit ScreenCapturer(QObject *parent = nullptr);

    static QVector<CaptureSource> availableSources(int thumbnailWidth = 240);
    static QImage grab(const CaptureSource &source);

    void setSource(const CaptureSource &source);
    const CaptureSource &source() const { return m_source; }
    void setFrameRate(int fps);
    void setMaxHeight(int pixels); // 0 = native resolution

    bool start();
    void stop();
    bool isActive() const { return m_timer.isActive(); }
    double measuredFps() const { return m_measuredFps; }

signals:
    void frameReady(const QImage &frame);
    void started();
    void stopped(const QString &reason);

private:
    void captureFrame();

    CaptureSource m_source;
    QTimer m_timer;
    int m_fps = 15;
    int m_maxHeight = 720;

    QElapsedTimer m_fpsClock;
    int m_framesInWindow = 0;
    double m_measuredFps = 0.0;
    int m_consecutiveFailures = 0;
};

} // namespace lightcord::screenshare
