#pragma once

#include "config.h"

#include <QByteArray>
#include <QObject>

class QFileSystemWatcher;
class QTimer;

// Watches the bar's config file, debounces editor save bursts, and re-parses the JSONC
// once the write settles — emitting configReloaded(fresh) only when the content really
// changed and parsed cleanly. Applying the fresh config is the owner's business.
class ConfigReloader final : public QObject {
    Q_OBJECT

public:
    // `configIndex` selects this bar's object when the config file is an array of bars.
    ConfigReloader(const QString &configFilePath, int configIndex, QObject *parent = nullptr);

signals:
    void configReloaded(const BarConfig &fresh);

private slots:
    void onConfigFileChanged(const QString &path);
    void reloadFromDisk();

private:
    QString m_path;
    int m_configIndex = -1;
    QFileSystemWatcher *m_watcher = nullptr;
    QTimer *m_reloadTimer = nullptr;
    QByteArray m_hash;
};
