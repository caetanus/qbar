#pragma once

#include <QObject>
#include <QString>
#include <QTimer>

// System load averages (waybar's "load" module). /proc/loadavg on a slow timer —
// the kernel itself only updates the averages every 5 seconds.
class LoadModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(double load1 READ load1 NOTIFY changed)
    Q_PROPERTY(double load5 READ load5 NOTIFY changed)
    Q_PROPERTY(double load15 READ load15 NOTIFY changed)
    Q_PROPERTY(QString displayText READ displayText NOTIFY changed)
    Q_PROPERTY(QString tooltipText READ tooltipText NOTIFY changed)

public:
    explicit LoadModel(QObject *parent = nullptr);

    bool available() const { return m_available; }
    double load1() const { return m_load1; }
    double load5() const { return m_load5; }
    double load15() const { return m_load15; }
    QString displayText() const { return m_displayText; }
    QString tooltipText() const { return m_tooltipText; }

signals:
    void changed();

private:
    void refresh();

    bool m_available = false;
    double m_load1 = 0;
    double m_load5 = 0;
    double m_load15 = 0;
    QString m_displayText;
    QString m_tooltipText;
    QTimer m_timer;
};
