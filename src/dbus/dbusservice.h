#pragma once

#include <QJSValue>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantList>

class QQmlEngine;

// Async D-Bus bridge for QML/JS. Calls are dispatched with QDBusPendingCall
// (non-blocking, event-loop driven) and return a JS Promise, so the GUI thread
// is never blocked. Usage:
//
//   ifId.speed = await dbus.getProperty("system", service, path, iface, "Bitrate")
//   dbus.call("session", service, path, iface, "Method", [a, b]).then(r => ...)
//
// The promise resolves with the demarshalled result, or rejects with the D-Bus
// error message.
class DBusService final : public QObject {
    Q_OBJECT

public:
    explicit DBusService(QQmlEngine *engine, QObject *parent = nullptr);

    Q_INVOKABLE QJSValue getProperty(const QString &bus,
                                     const QString &service,
                                     const QString &path,
                                     const QString &interface,
                                     const QString &property);

    Q_INVOKABLE QJSValue call(const QString &bus,
                              const QString &service,
                              const QString &path,
                              const QString &interface,
                              const QString &method,
                              const QVariantList &arguments);

private:
    QJSValue newPromise(QJSValue *resolve, QJSValue *reject);

    QPointer<QQmlEngine> m_engine;
    QJSValue m_deferredFactory;
};
