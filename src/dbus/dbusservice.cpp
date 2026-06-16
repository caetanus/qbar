#include "dbusservice.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusVariant>
#include <QJSEngine>
#include <QQmlEngine>

namespace {

QDBusConnection busFor(const QString &bus)
{
    return bus.compare(QLatin1String("system"), Qt::CaseInsensitive) == 0
        ? QDBusConnection::systemBus()
        : QDBusConnection::sessionBus();
}

// Unwrap the QDBusVariant that Properties.Get returns so JS sees the inner value.
QVariant unwrap(const QVariant &value)
{
    if (value.canConvert<QDBusVariant>()) {
        return value.value<QDBusVariant>().variant();
    }
    return value;
}

} // namespace

DBusService::DBusService(QQmlEngine *engine, QObject *parent)
    : QObject(parent)
    , m_engine(engine)
{
    if (m_engine != nullptr) {
        // Factory returning a deferred: { promise, resolve, reject }.
        m_deferredFactory = m_engine->evaluate(QStringLiteral(
            "(function() {"
            "  var d = {};"
            "  d.promise = new Promise(function(res, rej) { d.resolve = res; d.reject = rej; });"
            "  return d;"
            "})"));
    }
}

QJSValue DBusService::newPromise(QJSValue *resolve, QJSValue *reject)
{
    QJSValue deferred = m_deferredFactory.call();
    *resolve = deferred.property(QStringLiteral("resolve"));
    *reject = deferred.property(QStringLiteral("reject"));
    return deferred.property(QStringLiteral("promise"));
}

QJSValue DBusService::getProperty(const QString &bus,
                                  const QString &service,
                                  const QString &path,
                                  const QString &interface,
                                  const QString &property)
{
    if (m_engine.isNull() || !m_deferredFactory.isCallable()) {
        return {};
    }

    QJSValue resolve;
    QJSValue reject;
    const QJSValue promise = newPromise(&resolve, &reject);

    QDBusMessage message = QDBusMessage::createMethodCall(service, path,
                                                          QStringLiteral("org.freedesktop.DBus.Properties"),
                                                          QStringLiteral("Get"));
    message << interface << property;

    auto *watcher = new QDBusPendingCallWatcher(busFor(bus).asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, resolve, reject](QDBusPendingCallWatcher *self) mutable {
        const QDBusMessage reply = self->reply();
        self->deleteLater();
        if (reply.type() == QDBusMessage::ErrorMessage) {
            reject.call(QJSValueList{m_engine->toScriptValue(reply.errorMessage())});
            return;
        }
        const QVariant value = reply.arguments().isEmpty() ? QVariant() : unwrap(reply.arguments().constFirst());
        resolve.call(QJSValueList{m_engine->toScriptValue(value)});
    });

    return promise;
}

QJSValue DBusService::call(const QString &bus,
                           const QString &service,
                           const QString &path,
                           const QString &interface,
                           const QString &method,
                           const QVariantList &arguments)
{
    if (m_engine.isNull() || !m_deferredFactory.isCallable()) {
        return {};
    }

    QJSValue resolve;
    QJSValue reject;
    const QJSValue promise = newPromise(&resolve, &reject);

    QDBusMessage message = QDBusMessage::createMethodCall(service, path, interface, method);
    message.setArguments(arguments);

    auto *watcher = new QDBusPendingCallWatcher(busFor(bus).asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, resolve, reject](QDBusPendingCallWatcher *self) mutable {
        const QDBusMessage reply = self->reply();
        self->deleteLater();
        if (reply.type() == QDBusMessage::ErrorMessage) {
            reject.call(QJSValueList{m_engine->toScriptValue(reply.errorMessage())});
            return;
        }
        const QVariantList values = reply.arguments();
        const QVariant result = values.isEmpty() ? QVariant()
            : (values.size() == 1 ? unwrap(values.constFirst()) : QVariant(values));
        resolve.call(QJSValueList{m_engine->toScriptValue(result)});
    });

    return promise;
}
