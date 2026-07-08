#include "loadmodel.h"

#include <QFile>
#include <QLocale>

LoadModel::LoadModel(QObject *parent)
    : QObject(parent)
{
    refresh();
    m_timer.setInterval(5000); // matches the kernel's own update cadence
    connect(&m_timer, &QTimer::timeout, this, &LoadModel::refresh);
    m_timer.start();
}

void LoadModel::refresh()
{
    QFile file(QStringLiteral("/proc/loadavg"));
    if (!file.open(QIODevice::ReadOnly)) {
        if (m_available) {
            m_available = false;
            emit changed();
        }
        return;
    }
    // "0.52 0.58 0.59 1/1189 12345"
    const QList<QByteArray> parts = file.readAll().simplified().split(' ');
    if (parts.size() < 3) {
        return;
    }
    const double l1 = parts.at(0).toDouble();
    const double l5 = parts.at(1).toDouble();
    const double l15 = parts.at(2).toDouble();
    if (m_available && qFuzzyCompare(l1, m_load1) && qFuzzyCompare(l5, m_load5)
        && qFuzzyCompare(l15, m_load15)) {
        return;
    }
    m_available = true;
    m_load1 = l1;
    m_load5 = l5;
    m_load15 = l15;
    const QLocale locale;
    m_displayText = locale.toString(m_load1, 'f', 2);
    m_tooltipText = tr("load: %1 (1 min) · %2 (5 min) · %3 (15 min)")
                        .arg(locale.toString(m_load1, 'f', 2),
                             locale.toString(m_load5, 'f', 2),
                             locale.toString(m_load15, 'f', 2));
    emit changed();
}
