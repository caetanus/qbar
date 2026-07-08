#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantList>

// Failed systemd units (waybar's "systemd-failed-units"), watched on BOTH the
// system manager (system bus) and the user manager (session bus). Event-driven:
// Manager.Subscribe() + JobRemoved/Reloading trigger a refresh, with a slow
// failsafe timer for state changes that produce no job (e.g. a watchdog kill).
class FailedUnitsModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(int count READ count NOTIFY changed)
    Q_PROPERTY(QVariantList units READ units NOTIFY changed)
    Q_PROPERTY(QString tooltipText READ tooltipText NOTIFY changed)

public:
    explicit FailedUnitsModel(QObject *parent = nullptr);

    bool available() const { return m_available; }
    int count() const { return static_cast<int>(m_units.size()); }
    QVariantList units() const { return m_units; }
    QString tooltipText() const { return m_tooltipText; }

private slots:
    void scheduleRefresh();

private:
    void wireBus(bool systemBus);
    void refresh();
    void refreshBus(bool systemBus);
    void applyBusUnits(bool systemBus, const QVariantList &units);
    void rebuild();

    bool m_available = false;
    QVariantList m_systemUnits;
    QVariantList m_userUnits;
    QVariantList m_units;
    QString m_tooltipText;
    QTimer m_refreshDebounce;
    QTimer m_failsafe;

signals:
    void changed();
};
