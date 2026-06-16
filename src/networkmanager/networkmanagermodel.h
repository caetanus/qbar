#pragma once

#include <QDBusConnection>
#include <QMetaType>
#include <QObject>
#include <QString>
#include <QThread>
#include <QTimer>
#include <QVariant>

// Snapshot of the primary connection, computed off the GUI thread.
struct NmStatus {
    int state = 0; // 0 = disconnected, 1 = wired, 2 = wireless
    int strength = 0;
    QString interfaceName;
    QString ssid;
    QString ipText;
    QString ipv4Text;
    QString ipv6Text;
    int channel = 0;
    QString linkSpeedText;
};
Q_DECLARE_METATYPE(NmStatus)

// Lives on a worker thread with its own system-bus connection. It subscribes to
// NetworkManager's D-Bus change signals and (debounced) reads the primary
// connection's properties there, emitting a finished snapshot — so the blocking
// D-Bus round-trips never run on the GUI event loop.
class NmReader final : public QObject {
    Q_OBJECT

public slots:
    void start();
    void scheduleRefresh();
    void refresh();

signals:
    void updated(NmStatus status);

private:
    QVariant readProperty(const QString &service, const QString &path, const QString &interface, const QString &name) const;
    QString readStringProperty(const QString &service, const QString &path, const QString &interface, const QString &name) const;
    int readIntProperty(const QString &service, const QString &path, const QString &interface, const QString &name) const;
    QString readPrimaryConnectionType() const;
    QString readPrimaryConnectionPath() const;
    int readWirelessStrength(const QString &devicePath) const;
    int readWirelessFrequency(const QString &devicePath) const;
    int readWirelessBitrate(const QString &devicePath) const;
    int readEthernetSpeed(const QString &devicePath) const;
    QString readWirelessSsid(const QString &devicePath) const;
    QString readIpText(const QString &devicePath) const;
    QString readAddressDataFirstIp(const QString &configPath, bool ipv6) const;

    QDBusConnection m_bus = QDBusConnection(QString());
    QTimer *m_debounce = nullptr;
};

class NetworkManagerModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString mode READ mode NOTIFY statusChanged)
    Q_PROPERTY(QString iconName READ iconName NOTIFY statusChanged)
    Q_PROPERTY(QString label READ label NOTIFY statusChanged)
    Q_PROPERTY(QString interfaceName READ interfaceName NOTIFY statusChanged)
    Q_PROPERTY(QString ssid READ ssid NOTIFY statusChanged)
    Q_PROPERTY(QString ipText READ ipText NOTIFY statusChanged)
    Q_PROPERTY(QString ipv4Text READ ipv4Text NOTIFY statusChanged)
    Q_PROPERTY(QString ipv6Text READ ipv6Text NOTIFY statusChanged)
    Q_PROPERTY(int channel READ channel NOTIFY statusChanged)
    Q_PROPERTY(QString linkSpeedText READ linkSpeedText NOTIFY statusChanged)
    Q_PROPERTY(QString tooltipText READ tooltipText NOTIFY statusChanged)
    Q_PROPERTY(int strength READ strength NOTIFY statusChanged)
    Q_PROPERTY(bool available READ available CONSTANT)

public:
    explicit NetworkManagerModel(QObject *parent = nullptr);
    ~NetworkManagerModel() override;

    QString mode() const;
    QString iconName() const;
    QString label() const;
    QString interfaceName() const { return m_interfaceName; }
    QString ssid() const { return m_ssid; }
    QString ipText() const { return m_ipText; }
    QString ipv4Text() const { return m_ipv4Text; }
    QString ipv6Text() const { return m_ipv6Text; }
    int channel() const { return m_channel; }
    QString linkSpeedText() const { return m_linkSpeedText; }
    QString tooltipText() const;
    int strength() const { return m_strength; }
    bool available() const { return true; }

signals:
    void statusChanged();

private slots:
    void apply(const NmStatus &status);

private:
    static QString iconNameForStrength(int strength);
    static QString modeName(int state);

    int m_state = 0;
    int m_strength = 0;
    QString m_interfaceName;
    QString m_ssid;
    QString m_ipText;
    QString m_ipv4Text;
    QString m_ipv6Text;
    int m_channel = 0;
    QString m_linkSpeedText;

    QThread m_thread;
    NmReader *m_reader = nullptr;
};
