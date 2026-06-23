#pragma once

#include <QObject>
#include <QStringList>
#include <QVariant>

#include <memory>

class QQmlEngine;
class QThread;

// Asynchronous SQLite-backed key/value storage for QML widgets. Every database
// operation runs on one serial worker thread; results return through signals.
class LocalStorage final : public QObject {
    Q_OBJECT
    Q_PROPERTY(int length READ length NOTIFY lengthChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY readyChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

public:
    explicit LocalStorage(const QString &databasePath = {}, QObject *parent = nullptr);
    ~LocalStorage() override;

    static void install(QQmlEngine *engine);

    Q_INVOKABLE quint64 getItem(const QString &key);
    Q_INVOKABLE quint64 setItem(const QString &key, const QString &value);
    Q_INVOKABLE quint64 removeItem(const QString &key);
    Q_INVOKABLE quint64 clear();
    Q_INVOKABLE quint64 contains(const QString &key);
    Q_INVOKABLE quint64 keys();

    int length() const { return m_length; }
    bool ready() const { return m_ready; }
    QString lastError() const { return m_lastError; }

signals:
    void readyChanged();
    void lengthChanged();
    void lastErrorChanged();

    void itemLoaded(quint64 requestId, const QString &key, const QVariant &value, bool found);
    void itemStored(quint64 requestId, const QString &key, bool success, const QString &error);
    void itemRemoved(quint64 requestId, const QString &key, bool success, const QString &error);
    void storageCleared(quint64 requestId, bool success, const QString &error);
    void containsLoaded(quint64 requestId, const QString &key, bool found);
    void keysLoaded(quint64 requestId, const QStringList &keys);

    void changed(const QString &key, const QString &value);
    void removed(const QString &key);
    void cleared();

private:
    struct State;
    static QString defaultDatabasePath();
    quint64 nextRequestId();
    void setLength(int length);
    void setError(const QString &message);

    QThread *m_thread = nullptr;
    QObject *m_worker = nullptr;
    std::shared_ptr<State> m_state;
    quint64 m_nextRequestId = 1;
    int m_length = 0;
    bool m_ready = false;
    QString m_lastError;
};
