// SPDX-License-Identifier: MIT
#include "ScreenCapturer.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QPixmap>

#include <string>

#ifdef Q_OS_WIN
#include <windows.h>
#include <dwmapi.h>
#endif

namespace lightcord::screenshare {

namespace {

#ifdef Q_OS_WIN
#ifndef PW_RENDERFULLCONTENT
#define PW_RENDERFULLCONTENT 0x00000002
#endif

struct EnumContext {
    QVector<CaptureSource> *out;
    DWORD ownPid;
};

BOOL CALLBACK enumWindowProc(HWND hwnd, LPARAM lParam)
{
    auto *ctx = reinterpret_cast<EnumContext *>(lParam);
    if (!IsWindowVisible(hwnd) || IsIconic(hwnd) || GetWindow(hwnd, GW_OWNER) != nullptr)
        return TRUE;
    if (GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW)
        return TRUE;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == ctx->ownPid)
        return TRUE;

    // Skip windows hidden by DWM (e.g. suspended UWP apps on other desktops).
    BOOL cloaked = FALSE;
    if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof cloaked)) && cloaked)
        return TRUE;

    const int len = GetWindowTextLengthW(hwnd);
    if (len <= 0)
        return TRUE;
    std::wstring title(static_cast<std::size_t>(len) + 1, L'\0');
    GetWindowTextW(hwnd, title.data(), len + 1);
    title.resize(static_cast<std::size_t>(len));

    RECT r{};
    GetWindowRect(hwnd, &r);
    if (r.right - r.left < 50 || r.bottom - r.top < 50)
        return TRUE;

    CaptureSource s;
    s.kind = CaptureSource::Kind::Window;
    s.title = QString::fromStdWString(title);
    s.windowHandle = reinterpret_cast<quintptr>(hwnd);
    ctx->out->push_back(s);
    return TRUE;
}

QImage grabWindowWin32(HWND hwnd)
{
    RECT r{};
    if (!IsWindow(hwnd) || !GetWindowRect(hwnd, &r))
        return {};
    const int w = r.right - r.left;
    const int h = r.bottom - r.top;
    if (w <= 0 || h <= 0)
        return {};

    HDC screenDc = GetDC(nullptr);
    HDC memDc = CreateCompatibleDC(screenDc);
    HBITMAP bmp = CreateCompatibleBitmap(screenDc, w, h);
    HGDIOBJ old = SelectObject(memDc, bmp);

    QImage image;
    if (PrintWindow(hwnd, memDc, PW_RENDERFULLCONTENT)) {
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = w;
        bi.bmiHeader.biHeight = -h; // top-down rows
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;

        image = QImage(w, h, QImage::Format_RGB32);
        SelectObject(memDc, old);
        if (!GetDIBits(memDc, bmp, 0, static_cast<UINT>(h), image.bits(), &bi, DIB_RGB_COLORS))
            image = QImage();
        old = nullptr;
    }

    if (old)
        SelectObject(memDc, old);
    DeleteObject(bmp);
    DeleteDC(memDc);
    ReleaseDC(nullptr, screenDc);
    return image;
}
#endif

QImage scaledToHeight(const QImage &img, int maxHeight)
{
    if (img.isNull() || maxHeight <= 0 || img.height() <= maxHeight)
        return img;
    return img.scaledToHeight(maxHeight, Qt::SmoothTransformation);
}

} // namespace

ScreenCapturer::ScreenCapturer(QObject *parent)
    : QObject(parent)
{
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, &ScreenCapturer::captureFrame);
}

QVector<CaptureSource> ScreenCapturer::availableSources(int thumbnailWidth)
{
    QVector<CaptureSource> sources;
    const auto screens = QGuiApplication::screens();
    for (int i = 0; i < screens.size(); ++i) {
        CaptureSource s;
        s.kind = CaptureSource::Kind::Screen;
        s.screen = screens.at(i);
        const QSize sz = screens.at(i)->size() * screens.at(i)->devicePixelRatio();
        s.title = QCoreApplication::translate("ScreenCapturer", "Screen %1 (%2×%3)")
                      .arg(i + 1)
                      .arg(sz.width())
                      .arg(sz.height());
        sources.push_back(s);
    }

#ifdef Q_OS_WIN
    EnumContext ctx{&sources, GetCurrentProcessId()};
    EnumWindows(enumWindowProc, reinterpret_cast<LPARAM>(&ctx));
#endif

    if (thumbnailWidth > 0) {
        for (CaptureSource &s : sources) {
            const QImage full = grab(s);
            if (!full.isNull())
                s.thumbnail = full.scaledToWidth(thumbnailWidth, Qt::SmoothTransformation);
        }
    }
    return sources;
}

QImage ScreenCapturer::grab(const CaptureSource &source)
{
    if (source.kind == CaptureSource::Kind::Screen) {
        if (source.screen.isNull())
            return {};
        return source.screen->grabWindow(0).toImage();
    }
#ifdef Q_OS_WIN
    return grabWindowWin32(reinterpret_cast<HWND>(source.windowHandle));
#else
    return {};
#endif
}

void ScreenCapturer::setSource(const CaptureSource &source)
{
    m_source = source;
}

void ScreenCapturer::setFrameRate(int fps)
{
    m_fps = qBound(1, fps, 60);
    if (m_timer.isActive())
        m_timer.start(1000 / m_fps);
}

void ScreenCapturer::setMaxHeight(int pixels)
{
    m_maxHeight = qMax(0, pixels);
}

bool ScreenCapturer::start()
{
    if (!m_source.isValid())
        return false;
    m_consecutiveFailures = 0;
    m_framesInWindow = 0;
    m_measuredFps = 0.0;
    m_fpsClock.start();
    m_timer.start(1000 / m_fps);
    emit started();
    captureFrame();
    return true;
}

void ScreenCapturer::stop()
{
    if (!m_timer.isActive())
        return;
    m_timer.stop();
    emit stopped(QString());
}

void ScreenCapturer::captureFrame()
{
    const QImage frame = scaledToHeight(grab(m_source), m_maxHeight);
    if (frame.isNull()) {
        if (++m_consecutiveFailures >= m_fps * 2) {
            m_timer.stop();
            emit stopped(tr("The shared window or screen is no longer available."));
        }
        return;
    }
    m_consecutiveFailures = 0;

    ++m_framesInWindow;
    const qint64 elapsed = m_fpsClock.elapsed();
    if (elapsed >= 1000) {
        m_measuredFps = m_framesInWindow * 1000.0 / static_cast<double>(elapsed);
        m_framesInWindow = 0;
        m_fpsClock.restart();
    }
    emit frameReady(frame);
}

} // namespace lightcord::screenshare
