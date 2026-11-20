// SPDX-License-Identifier: MIT
// Lightcord - a lightweight native Discord client.

#include "core/Settings.h"
#include "ui/MainWindow.h"
#include "ui/Theme.h"

#include <QApplication>
#include <QIcon>
#include <QSettings>
#include <QStyleFactory>
#include <QTimer>

int main(int argc, char *argv[])
{
#ifdef Q_OS_WIN
    // Use the native Windows Media backend for audio instead of FFmpeg: it is
    // all Lightcord needs and lets the release package skip ~20 MB of DLLs.
    if (qEnvironmentVariableIsEmpty("QT_MEDIA_BACKEND"))
        qputenv("QT_MEDIA_BACKEND", "windows");
#endif

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Lightcord"));
    QApplication::setOrganizationName(QStringLiteral("Lightcord"));
    QApplication::setApplicationVersion(QStringLiteral(LIGHTCORD_VERSION));
    QSettings::setDefaultFormat(QSettings::IniFormat);

    // Fusion gives a consistent base for the Lightcord stylesheet on every OS.
    if (QStyle *fusion = QStyleFactory::create(QStringLiteral("Fusion")))
        QApplication::setStyle(fusion);
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/lightcord.svg")));

    lightcord::Theme::apply(lightcord::Settings::instance().theme(),
                            lightcord::Settings::instance().fontPointSize());

    lightcord::MainWindow window;
    window.show();
    QTimer::singleShot(0, &window, &lightcord::MainWindow::startup);

    return app.exec();
}
