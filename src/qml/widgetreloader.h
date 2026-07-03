#pragma once

#include "config.h"

#include <QByteArray>
#include <QObject>
#include <QStringList>

class QFileSystemWatcher;
class QQmlEngine;

// Hot-reload for runtime custom QML widgets: watches every .qml/.js sibling of each
// configured widget `source`, coalesces the burst of events a save produces by content
// hash, clears the QML component cache and bumps `reloadGeneration` — Bar.qml's widget
// Loaders are bound to it and re-read the widget from disk on each bump.
class WidgetReloader final : public QObject {
    Q_OBJECT
    Q_PROPERTY(int reloadGeneration READ reloadGeneration NOTIFY reloadGenerationChanged)

public:
    // `config` is the owning bar's LIVE config, held by reference: customTools and
    // configFilePath are re-read on every rescan, so config hot-reloads are picked up.
    WidgetReloader(QQmlEngine *engine, const BarConfig &config, QObject *parent = nullptr);

    int reloadGeneration() const { return m_reloadGeneration; }

signals:
    void reloadGenerationChanged();

private slots:
    void onWidgetFileChanged(const QString &path);

private:
    QStringList runtimeWidgetFiles() const;
    QByteArray widgetContentHash() const;
    void refreshWidgetWatch();
    void reloadWidgets();

    QQmlEngine *m_engine = nullptr;
    const BarConfig &m_config;
    QFileSystemWatcher *m_widgetWatcher = nullptr;
    QStringList m_widgetFiles;
    QByteArray m_widgetHash;
    int m_reloadGeneration = 0;
};
