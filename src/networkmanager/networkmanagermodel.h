#pragma once

#include <QObject>
#include <QTimer>
#include <QVariant>
#include <QString>

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

    QString mode() const;
    QString iconName() const;
    QString label() const;
    QString interfaceName() const;
    QString ssid() const;
    QString ipText() const;
    QString ipv4Text() const;
    QString ipv6Text() const;
    int channel() const;
    QString linkSpeedText() const;
    QString tooltipText() const;
    int strength() const;
    bool available() const;

signals:
    void statusChanged();

private:
    enum class State {
        Disconnected,
        Wired,
        Wireless,
    };

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
    static int channelFromFrequency(int frequency);
    static QString linkSpeedString(double mbps);
    void refresh();
    void setState(State state,
                  int strength,
                  const QString &interfaceName = QString(),
                  const QString &ssid = QString(),
                  const QString &ipText = QString(),
                  const QString &ipv4Text = QString(),
                  const QString &ipv6Text = QString(),
                  int channel = 0,
                  const QString &linkSpeedText = QString());

    static QString iconNameForStrength(int strength);
    static QString modeName(State state);

    State m_state = State::Disconnected;
    int m_strength = 0;
    QString m_interfaceName;
    QString m_ssid;
    QString m_ipText;
    QString m_ipv4Text;
    QString m_ipv6Text;
    int m_channel = 0;
    QString m_linkSpeedText;
    QTimer m_timer;
};
