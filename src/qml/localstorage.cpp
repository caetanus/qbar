#include "localstorage.h"

#include <QDir>
#include <QFileInfo>
#include <QJSEngine>
#include <QMetaObject>
#include <QQmlEngine>
#include <QStandardPaths>
#include <QThread>

#include <sqlite3.h>

namespace {

constexpr auto kCreateSql =
    "CREATE TABLE IF NOT EXISTS storage ("
    "key TEXT PRIMARY KEY NOT NULL,"
    "value TEXT NOT NULL,"
    "updated_at INTEGER NOT NULL DEFAULT (unixepoch())"
    ")";

QString columnText(sqlite3_stmt *statement, int column)
{
    const auto *text = sqlite3_column_text(statement, column);
    return text != nullptr ? QString::fromUtf8(reinterpret_cast<const char *>(text)) : QString();
}

int itemCount(sqlite3 *db)
{
    sqlite3_stmt *statement = nullptr;
    if (db == nullptr || sqlite3_prepare_v2(db, "SELECT count(*) FROM storage", -1,
                                            &statement, nullptr) != SQLITE_OK)
        return 0;
    const int result = sqlite3_step(statement) == SQLITE_ROW
        ? sqlite3_column_int(statement, 0) : 0;
    sqlite3_finalize(statement);
    return result;
}

QString databaseError(sqlite3 *db, const QString &fallback = QStringLiteral("storage unavailable"))
{
    return db != nullptr ? QString::fromUtf8(sqlite3_errmsg(db)) : fallback;
}

} // namespace

struct LocalStorage::State {
    sqlite3 *db = nullptr;
    QString path;
};

LocalStorage::LocalStorage(const QString &databasePath, QObject *parent)
    : QObject(parent)
    , m_thread(new QThread(this))
    , m_worker(new QObject)
    , m_state(std::make_shared<State>())
{
    m_state->path = databasePath.isEmpty() ? defaultDatabasePath() : databasePath;
    m_worker->moveToThread(m_thread);
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    m_thread->setObjectName(QStringLiteral("qbar-local-storage"));
    m_thread->start();

    const auto state = m_state;
    QMetaObject::invokeMethod(m_worker, [this, state]() {
        QDir().mkpath(QFileInfo(state->path).absolutePath());
        QString error;
        if (sqlite3_open_v2(state->path.toUtf8().constData(), &state->db,
                            SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
                            nullptr) != SQLITE_OK) {
            error = databaseError(state->db, QStringLiteral("could not open database"));
        } else {
            sqlite3_busy_timeout(state->db, 1500);
            sqlite3_exec(state->db, "PRAGMA journal_mode=WAL", nullptr, nullptr, nullptr);
            if (sqlite3_exec(state->db, kCreateSql, nullptr, nullptr, nullptr) != SQLITE_OK)
                error = databaseError(state->db);
        }
        const int count = error.isEmpty() ? itemCount(state->db) : 0;
        QMetaObject::invokeMethod(this, [this, error, count]() {
            if (!error.isEmpty())
                setError(error);
            setLength(count);
            m_ready = error.isEmpty();
            emit readyChanged();
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
}

LocalStorage::~LocalStorage()
{
    const auto state = m_state;
    if (m_thread != nullptr && m_thread->isRunning()) {
        QMetaObject::invokeMethod(m_worker, [state]() {
            if (state->db != nullptr) {
                sqlite3_close(state->db);
                state->db = nullptr;
            }
        }, Qt::BlockingQueuedConnection);
        m_thread->quit();
        m_thread->wait();
    }
}

void LocalStorage::install(QQmlEngine *engine)
{
    if (engine == nullptr)
        return;
    auto *storage = new LocalStorage({}, engine);
    engine->globalObject().setProperty(QStringLiteral("LocalStorage"), engine->newQObject(storage));
}

quint64 LocalStorage::getItem(const QString &key)
{
    const quint64 id = nextRequestId();
    const auto state = m_state;
    QMetaObject::invokeMethod(m_worker, [this, state, id, key]() {
        QVariant value;
        bool found = false;
        QString error;
        sqlite3_stmt *statement = nullptr;
        if (state->db == nullptr || sqlite3_prepare_v2(state->db,
                "SELECT value FROM storage WHERE key = ?1", -1, &statement, nullptr) != SQLITE_OK) {
            error = databaseError(state->db);
        } else {
            const QByteArray utf8 = key.toUtf8();
            sqlite3_bind_text(statement, 1, utf8.constData(), utf8.size(), SQLITE_TRANSIENT);
            if (sqlite3_step(statement) == SQLITE_ROW) {
                value = columnText(statement, 0);
                found = true;
            }
            sqlite3_finalize(statement);
        }
        QMetaObject::invokeMethod(this, [this, id, key, value, found, error]() {
            if (!error.isEmpty()) setError(error);
            emit itemLoaded(id, key, value, found);
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
    return id;
}

quint64 LocalStorage::setItem(const QString &key, const QString &value)
{
    const quint64 id = nextRequestId();
    const auto state = m_state;
    QMetaObject::invokeMethod(m_worker, [this, state, id, key, value]() {
        bool ok = false;
        QString error;
        sqlite3_stmt *statement = nullptr;
        constexpr auto sql = "INSERT INTO storage(key,value,updated_at) VALUES(?1,?2,unixepoch()) "
                             "ON CONFLICT(key) DO UPDATE SET value=excluded.value,updated_at=excluded.updated_at";
        if (state->db == nullptr || key.isEmpty()
            || sqlite3_prepare_v2(state->db, sql, -1, &statement, nullptr) != SQLITE_OK) {
            error = key.isEmpty() ? QStringLiteral("key must not be empty") : databaseError(state->db);
        } else {
            const QByteArray keyUtf8 = key.toUtf8();
            const QByteArray valueUtf8 = value.toUtf8();
            sqlite3_bind_text(statement, 1, keyUtf8.constData(), keyUtf8.size(), SQLITE_TRANSIENT);
            sqlite3_bind_text(statement, 2, valueUtf8.constData(), valueUtf8.size(), SQLITE_TRANSIENT);
            ok = sqlite3_step(statement) == SQLITE_DONE;
            if (!ok) error = databaseError(state->db);
            sqlite3_finalize(statement);
        }
        const int count = ok ? itemCount(state->db) : -1;
        QMetaObject::invokeMethod(this, [this, id, key, value, ok, error, count]() {
            if (!error.isEmpty()) setError(error);
            if (count >= 0) setLength(count);
            emit itemStored(id, key, ok, error);
            if (ok) emit changed(key, value);
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
    return id;
}

quint64 LocalStorage::removeItem(const QString &key)
{
    const quint64 id = nextRequestId();
    const auto state = m_state;
    QMetaObject::invokeMethod(m_worker, [this, state, id, key]() {
        bool ok = false;
        bool changed = false;
        QString error;
        sqlite3_stmt *statement = nullptr;
        if (state->db == nullptr || key.isEmpty()
            || sqlite3_prepare_v2(state->db, "DELETE FROM storage WHERE key = ?1", -1,
                                  &statement, nullptr) != SQLITE_OK) {
            error = key.isEmpty() ? QStringLiteral("key must not be empty") : databaseError(state->db);
        } else {
            const QByteArray utf8 = key.toUtf8();
            sqlite3_bind_text(statement, 1, utf8.constData(), utf8.size(), SQLITE_TRANSIENT);
            ok = sqlite3_step(statement) == SQLITE_DONE;
            changed = ok && sqlite3_changes(state->db) > 0;
            if (!ok) error = databaseError(state->db);
            sqlite3_finalize(statement);
        }
        const int count = ok ? itemCount(state->db) : -1;
        QMetaObject::invokeMethod(this, [this, id, key, ok, changed, error, count]() {
            if (!error.isEmpty()) setError(error);
            if (count >= 0) setLength(count);
            emit itemRemoved(id, key, ok, error);
            if (changed) emit removed(key);
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
    return id;
}

quint64 LocalStorage::clear()
{
    const quint64 id = nextRequestId();
    const auto state = m_state;
    QMetaObject::invokeMethod(m_worker, [this, state, id]() {
        const bool ok = state->db != nullptr
            && sqlite3_exec(state->db, "DELETE FROM storage", nullptr, nullptr, nullptr) == SQLITE_OK;
        const QString error = ok ? QString() : databaseError(state->db);
        QMetaObject::invokeMethod(this, [this, id, ok, error]() {
            if (!error.isEmpty()) setError(error);
            if (ok) {
                setLength(0);
                emit cleared();
            }
            emit storageCleared(id, ok, error);
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
    return id;
}

quint64 LocalStorage::contains(const QString &key)
{
    const quint64 id = nextRequestId();
    const auto state = m_state;
    QMetaObject::invokeMethod(m_worker, [this, state, id, key]() {
        bool found = false;
        sqlite3_stmt *statement = nullptr;
        if (state->db != nullptr && sqlite3_prepare_v2(state->db,
                "SELECT 1 FROM storage WHERE key = ?1", -1, &statement, nullptr) == SQLITE_OK) {
            const QByteArray utf8 = key.toUtf8();
            sqlite3_bind_text(statement, 1, utf8.constData(), utf8.size(), SQLITE_TRANSIENT);
            found = sqlite3_step(statement) == SQLITE_ROW;
            sqlite3_finalize(statement);
        }
        QMetaObject::invokeMethod(this, [this, id, key, found]() {
            emit containsLoaded(id, key, found);
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
    return id;
}

quint64 LocalStorage::keys()
{
    const quint64 id = nextRequestId();
    const auto state = m_state;
    QMetaObject::invokeMethod(m_worker, [this, state, id]() {
        QStringList result;
        sqlite3_stmt *statement = nullptr;
        if (state->db != nullptr && sqlite3_prepare_v2(state->db,
                "SELECT key FROM storage ORDER BY key", -1, &statement, nullptr) == SQLITE_OK) {
            while (sqlite3_step(statement) == SQLITE_ROW)
                result.append(columnText(statement, 0));
            sqlite3_finalize(statement);
        }
        QMetaObject::invokeMethod(this, [this, id, result]() {
            emit keysLoaded(id, result);
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
    return id;
}

QString LocalStorage::defaultDatabasePath()
{
    const QString root = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    return QDir(root).filePath(QStringLiteral("qbar/localstorage.db"));
}

quint64 LocalStorage::nextRequestId()
{
    return m_nextRequestId++;
}

void LocalStorage::setLength(int length)
{
    if (m_length == length)
        return;
    m_length = length;
    emit lengthChanged();
}

void LocalStorage::setError(const QString &message)
{
    if (m_lastError == message)
        return;
    m_lastError = message;
    emit lastErrorChanged();
}
