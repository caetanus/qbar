#include "configreloader.h"

#include "json/jsonc.h"

#include <QCryptographicHash>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

ConfigReloader::ConfigReloader(const QString &configFilePath, int configIndex, QObject *parent)
    : QObject(parent)
    , m_path(configFilePath)
    , m_configIndex(configIndex)
{
    m_watcher = new QFileSystemWatcher(this);
    if (!m_path.isEmpty() && QFileInfo::exists(m_path)) {
        QFile f(m_path);
        if (f.open(QIODevice::ReadOnly)) {
            m_hash = QCryptographicHash::hash(f.readAll(), QCryptographicHash::Md5);
        }
        m_watcher->addPath(m_path);
    }
    connect(m_watcher, &QFileSystemWatcher::fileChanged, this, &ConfigReloader::onConfigFileChanged);

    // Debounce reloads: editors save by truncating or atomically replacing the file, so the
    // watcher often fires mid-write. Wait a beat so we read the SETTLED file, not a half-written
    // one (which parsed as "invalid JSONC at offset 0" and dropped the reload).
    m_reloadTimer = new QTimer(this);
    m_reloadTimer->setSingleShot(true);
    m_reloadTimer->setInterval(120);
    connect(m_reloadTimer, &QTimer::timeout, this, &ConfigReloader::reloadFromDisk);
}

void ConfigReloader::onConfigFileChanged(const QString &path)
{
    // Editors atomically replace the file, swapping its inode — re-add so future saves fire.
    if (!m_watcher->files().contains(path)) {
        m_watcher->addPath(path);
    }
    m_reloadTimer->start(); // coalesce + wait for the write to settle, then reload
}

void ConfigReloader::reloadFromDisk()
{
    QFile file(m_path);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "qbar: cannot read config file" << m_path;
        return;
    }
    const QByteArray data = file.readAll();
    if (data.isEmpty()) {
        return; // still mid-save (truncated) — the next fileChanged re-arms the timer
    }
    const QByteArray hash = QCryptographicHash::hash(data, QCryptographicHash::Md5);
    if (hash == m_hash) {
        return; // touch with no content change
    }

    QString jsonError;
    const auto doc = Jsonc::parse(QString::fromUtf8(data), &jsonError);
    QJsonObject root;
    bool foundRoot = false;
    if (doc.isArray()) {
        const QJsonArray bars = doc.array();
        if (m_configIndex >= 0 && m_configIndex < bars.size() && bars.at(m_configIndex).isObject()) {
            root = bars.at(m_configIndex).toObject();
            foundRoot = true;
        }
    } else if (doc.isObject()) {
        root = doc.object();
        foundRoot = true;
    }

    if (!foundRoot) {
        // Likely a still-settling write; leave m_hash untouched so the next event retries.
        qWarning() << "qbar: config.json is broken (invalid JSONC), ignoring reload" << jsonError;
        return;
    }
    m_hash = hash; // commit only after a clean parse

    emit configReloaded(parseBarObject(root));
}
