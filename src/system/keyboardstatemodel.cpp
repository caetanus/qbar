#include "keyboardstatemodel.h"

#include <QDir>
#include <QFile>

KeyboardStateModel::KeyboardStateModel(QObject *parent)
    : QObject(parent)
{
    discoverLeds();
    refresh();
    m_timer.setInterval(400);
    connect(&m_timer, &QTimer::timeout, this, &KeyboardStateModel::refresh);
    if (m_available) {
        m_timer.start();
    }
}

void KeyboardStateModel::discoverLeds()
{
    // Every keyboard registers its own LED trio ("input3::capslock", …). Collect
    // ALL of them: with more than one keyboard the lock state is per-device, and
    // "any lit" is what the user means by "caps is on".
    const QDir leds(QStringLiteral("/sys/class/leds"));
    const QStringList entries = leds.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &entry : entries) {
        const QString brightness = leds.filePath(entry + QStringLiteral("/brightness"));
        if (entry.endsWith(QLatin1String("::capslock"))) {
            m_capsPaths.append(brightness);
        } else if (entry.endsWith(QLatin1String("::numlock"))) {
            m_numPaths.append(brightness);
        } else if (entry.endsWith(QLatin1String("::scrolllock"))) {
            m_scrollPaths.append(brightness);
        }
    }
    m_available = !m_capsPaths.isEmpty() || !m_numPaths.isEmpty();
}

bool KeyboardStateModel::readLed(const QStringList &paths)
{
    for (const QString &path : paths) {
        QFile file(path);
        if (file.open(QIODevice::ReadOnly) && file.read(1).startsWith('1')) {
            return true;
        }
    }
    return false;
}

void KeyboardStateModel::refresh()
{
    const bool caps = readLed(m_capsPaths);
    const bool num = readLed(m_numPaths);
    const bool scroll = readLed(m_scrollPaths);
    if (caps == m_caps && num == m_num && scroll == m_scroll) {
        return;
    }
    m_caps = caps;
    m_num = num;
    m_scroll = scroll;
    emit changed();
}
