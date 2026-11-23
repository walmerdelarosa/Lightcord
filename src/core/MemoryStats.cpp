// SPDX-License-Identifier: MIT
#include "MemoryStats.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#else
#include <QFile>
#endif

namespace lightcord {

namespace {
QString mb(double bytes)
{
    return QString::number(bytes / (1024.0 * 1024.0), 'f', 1) + QStringLiteral(" MB");
}
} // namespace

QString memoryUsageSummary()
{
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    pmc.cb = sizeof pmc;
    if (!GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&pmc), sizeof pmc))
        return QStringLiteral("unavailable");
    return QStringLiteral("working set %1 (peak %2), private %3")
        .arg(mb(static_cast<double>(pmc.WorkingSetSize)), mb(static_cast<double>(pmc.PeakWorkingSetSize)),
             mb(static_cast<double>(pmc.PrivateUsage)));
#else
    QFile f(QStringLiteral("/proc/self/status"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QStringLiteral("unavailable");
    double rss = 0.0, peak = 0.0;
    for (const QByteArray &line : f.readAll().split('\n')) {
        // Lines look like "VmRSS:     61234 kB".
        if (line.startsWith("VmRSS:"))
            rss = line.mid(6).trimmed().split(' ').value(0).toDouble() * 1024.0;
        else if (line.startsWith("VmHWM:"))
            peak = line.mid(6).trimmed().split(' ').value(0).toDouble() * 1024.0;
    }
    return QStringLiteral("resident %1 (peak %2)").arg(mb(rss), mb(peak));
#endif
}

} // namespace lightcord
