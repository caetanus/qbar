#include "capslockmonitor.h"

#include <QDir>
#include <QFile>

QStringList CapsLockMonitor::ledBrightnessPaths(const QString &pattern)
{
    QDir leds(QStringLiteral("/sys/class/leds"));
    const QStringList entries = leds.entryList(QStringList() << pattern,
                                               QDir::AllEntries | QDir::System | QDir::NoDotAndDotDot);
    QStringList paths;
    for (const QString &entry : entries) {
        paths.append(leds.filePath(entry) + QStringLiteral("/brightness"));
    }
    return paths;
}

bool CapsLockMonitor::anyLedOn(const QStringList &paths)
{
    for (const QString &path : paths) {
        QFile file(path);
        if (file.open(QIODevice::ReadOnly)) {
            if (file.readAll().trimmed().toInt() != 0) {
                return true;
            }
        }
    }
    return false;
}

CapsLockMonitor::CapsLockMonitor(QObject *parent)
    : QObject(parent)
{
    m_brightnessPaths = ledBrightnessPaths(QStringLiteral("*capslock*"));
    m_numBrightnessPaths = ledBrightnessPaths(QStringLiteral("*numlock*"));

    connect(&m_timer, &QTimer::timeout, this, &CapsLockMonitor::poll);
    m_timer.start(400);
    poll();
}

void CapsLockMonitor::poll()
{
    const bool caps = anyLedOn(m_brightnessPaths);
    if (caps != m_active) {
        m_active = caps;
        emit activeChanged();
    }

    const bool num = anyLedOn(m_numBrightnessPaths);
    if (num != m_numLockActive) {
        m_numLockActive = num;
        emit numLockActiveChanged();
    }
}
