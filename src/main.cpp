// SPDX-License-Identifier: MIT
// Lightcord - a lightweight native Discord client.

#include "core/Settings.h"
#include "ui/MainWindow.h"
#include "ui/Theme.h"

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QIcon>
#include <QMutex>
#include <QStandardPaths>
#include <QSettings>
#include <QStyleFactory>
#include <QTimer>

namespace {

QFile *g_logFile = nullptr;
QtMessageHandler g_previousHandler = nullptr;

// Writes every qDebug/qInfo/qWarning (including voice diagnostics) to
// <AppData>/Lightcord/logs/lightcord.log; the previous run is kept as lightcord.old.log.
void logToFile(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    static QMutex mutex;
    {
        QMutexLocker lock(&mutex);
        if (g_logFile && g_logFile->isOpen()) {
            static const char *levels[] = {"debug", "warning", "critical", "fatal", "info"};
            const char *level = (type >= 0 && type <= 4) ? levels[type] : "?";
            const QByteArray line = QDateTime::currentDateTime().toString(Qt::ISODateWithMs).toUtf8() + " [" +
                                    level + "] " + message.toUtf8() + "\n";
            g_logFile->write(line);
            g_logFile->flush();
        }
    }
    if (g_previousHandler)
        g_previousHandler(type, context, message);
}

void installFileLogger()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/logs");
    QDir().mkpath(dir);
    const QString path = dir + QStringLiteral("/lightcord.log");
    const QString old = dir + QStringLiteral("/lightcord.old.log");
    QFile::remove(old);
    QFile::rename(path, old);
    g_logFile = new QFile(path);
    if (g_logFile->open(QIODevice::WriteOnly | QIODevice::Text))
        g_previousHandler = qInstallMessageHandler(logToFile);
}

} // namespace

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
    installFileLogger();
    qInfo("Lightcord %s starting", LIGHTCORD_VERSION);

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
