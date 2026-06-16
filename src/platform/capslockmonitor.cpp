#include "capslockmonitor.h"

#include <QDir>
#include <QFile>

CapsLockMonitor::CapsLockMonitor(QObject *parent)
    : QObject(parent)
{
    QDir leds(QStringLiteral("/sys/class/leds"));
    const QStringList entries = leds.entryList(QStringList() << QStringLiteral("*capslock*"),
                                               QDir::AllEntries | QDir::System | QDir::NoDotAndDotDot);
    for (const QString &entry : entries) {
        m_brightnessPaths.append(leds.filePath(entry) + QStringLiteral("/brightness"));
    }

    connect(&m_timer, &QTimer::timeout, this, &CapsLockMonitor::poll);
    m_timer.start(400);
    poll();
}

void CapsLockMonitor::poll()
{
    bool on = false;
    for (const QString &path : m_brightnessPaths) {
        QFile file(path);
        if (file.open(QIODevice::ReadOnly)) {
            if (file.readAll().trimmed().toInt() != 0) {
                on = true;
                break;
            }
        }
    }

    if (on != m_active) {
        m_active = on;
        emit activeChanged();
    }
}
