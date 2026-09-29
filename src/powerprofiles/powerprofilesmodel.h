#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

class QDBusServiceWatcher;

// power-profiles-daemon front-end (waybar's "power-profiles-daemon" module).
// Talks to the system bus service entirely asynchronously: an initial GetAll, a
// PropertiesChanged subscription, and a service watcher for start/stop — no
// blocking calls on the GUI thread. Supports both the legacy net.hadess name and
// the newer org.freedesktop.UPower.PowerProfiles name.
class PowerProfilesModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(QString activeProfile READ activeProfile NOTIFY changed)
    Q_PROPERTY(QStringList profiles READ profiles NOTIFY changed)
    Q_PROPERTY(QString displayText READ displayText NOTIFY changed)
    Q_PROPERTY(QString tooltipText READ tooltipText NOTIFY changed)
    // CPU frequency boost (cpufreq/boost, or intel_pstate/no_turbo inverted).
    // Read straight from sysfs; written through a pkexec'd helper, since the
    // knob is root-only.
    Q_PROPERTY(bool boostSupported READ boostSupported NOTIFY boostChanged)
    Q_PROPERTY(bool boostWritable READ boostWritable NOTIFY boostChanged)
    Q_PROPERTY(bool boost READ boost NOTIFY boostChanged)
    Q_PROPERTY(bool boostPending READ boostPending NOTIFY boostChanged)
    Q_PROPERTY(QString boostError READ boostError NOTIFY boostChanged)

public:
    explicit PowerProfilesModel(QObject *parent = nullptr);

    bool available() const { return m_available; }
    QString activeProfile() const { return m_activeProfile; }
    QStringList profiles() const { return m_profiles; }
    QString displayText() const;
    QString tooltipText() const;

    // Set a specific profile ("performance" | "balanced" | "power-saver").
    Q_INVOKABLE void setProfile(const QString &profile);
    // Cycle through the daemon-reported profiles in order.
    Q_INVOKABLE void cycle();

    bool boostSupported() const { return m_boostSupported; }
    bool boostWritable() const;
    bool boost() const { return m_boost; }
    bool boostPending() const { return m_boostPending; }
    QString boostError() const { return m_boostError; }
    Q_INVOKABLE void setBoost(bool enabled);
    Q_INVOKABLE void refreshBoost();

signals:
    void changed();
    void boostChanged();

private slots:
    void handlePropertiesChanged(const QString &interface,
                                 const QVariantMap &changedProps,
                                 const QStringList &invalidated);

private:
    void connectToService();
    void disconnectFromService();
    void refresh();
    void applyActiveProfile(const QString &profile);
    void applyProfiles(const QVariant &profilesVariant);

    QString m_service;
    QString m_path;
    QString m_interface;  // properties interface — differs per service (see connectToService)
    bool m_available = false;
    QString m_activeProfile;
    QStringList m_profiles;
    QDBusServiceWatcher *m_watcher = nullptr;

    bool m_boostSupported = false;
    bool m_boost = false;
    bool m_boostPending = false;
    QString m_boostError;
};
