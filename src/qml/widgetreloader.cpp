#include "widgetreloader.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QQmlEngine>
#include <QUrl>

WidgetReloader::WidgetReloader(QQmlEngine *engine, const BarConfig &config, QObject *parent)
    : QObject(parent)
    , m_engine(engine)
    , m_config(config)
{
    m_widgetWatcher = new QFileSystemWatcher(this);
    connect(m_widgetWatcher, &QFileSystemWatcher::fileChanged, this, &WidgetReloader::onWidgetFileChanged);
    connect(m_widgetWatcher, &QFileSystemWatcher::directoryChanged, this, &WidgetReloader::onWidgetFileChanged);

    refreshWidgetWatch();
    m_widgetHash = widgetContentHash();
}

QStringList WidgetReloader::runtimeWidgetFiles() const
{
    // A customTool with a `source` (vs `exec`) is a runtime QML widget. A widget usually
    // pulls in siblings (a popup .qml, a .js helper), so we hot-reload every .qml/.js in
    // each directory that holds a widget source — not just the entry point named in config.
    const QString configDir = m_config.configFilePath.isEmpty()
        ? QString()
        : QFileInfo(m_config.configFilePath).absolutePath();

    QStringList dirs;
    const auto tools = m_config.customTools;
    for (auto it = tools.constBegin(); it != tools.constEnd(); ++it) {
        const QString source = it.value().toMap().value(QStringLiteral("source")).toString();
        if (source.isEmpty() || source.startsWith(QStringLiteral("qrc:")))
            continue; // empty, or compiled-in and not watchable
        QString path;
        if (source.startsWith(QStringLiteral("file://")))
            path = QUrl(source).toLocalFile();
        else if (source.startsWith(QLatin1Char('/')))
            path = source;
        else if (!configDir.isEmpty())
            path = QDir(configDir).filePath(source);
        const QString dir = path.isEmpty() ? QString() : QFileInfo(path).absolutePath();
        if (!dir.isEmpty() && !dirs.contains(dir))
            dirs.append(dir);
    }

    QStringList files;
    const QStringList filters{QStringLiteral("*.qml"), QStringLiteral("*.js")};
    for (const QString &dir : std::as_const(dirs)) {
        const auto entries = QDir(dir).entryInfoList(filters, QDir::Files);
        for (const QFileInfo &fi : entries) {
            const QString p = fi.absoluteFilePath();
            if (!files.contains(p))
                files.append(p);
        }
    }
    return files;
}

QByteArray WidgetReloader::widgetContentHash() const
{
    QCryptographicHash hash(QCryptographicHash::Md5);
    for (const QString &f : std::as_const(m_widgetFiles)) {
        hash.addData(f.toUtf8()); // fold the path in so renames/deletes change the digest
        QFile file(f);
        if (file.open(QIODevice::ReadOnly))
            hash.addData(file.readAll());
    }
    return hash.result();
}

void WidgetReloader::refreshWidgetWatch()
{
    // Re-enumerate widget files (a save may have added/removed siblings) and reconcile the
    // watcher: editors save atomically (write tmp + rename), which drops a file from the
    // watch list — re-adding here keeps it live. Directories are watched too so a brand-new
    // sibling file triggers a rescan.
    m_widgetFiles = runtimeWidgetFiles();

    QStringList dirs;
    for (const QString &f : std::as_const(m_widgetFiles)) {
        const QString dir = QFileInfo(f).absolutePath();
        if (!dirs.contains(dir))
            dirs.append(dir);
    }

    const QStringList wantFiles = m_widgetFiles;
    const QStringList watchedFiles = m_widgetWatcher->files();
    for (const QString &f : watchedFiles) {
        if (!wantFiles.contains(f))
            m_widgetWatcher->removePath(f);
    }
    for (const QString &f : std::as_const(wantFiles)) {
        if (QFileInfo::exists(f) && !m_widgetWatcher->files().contains(f))
            m_widgetWatcher->addPath(f);
    }
    for (const QString &d : std::as_const(dirs)) {
        if (QFileInfo::exists(d) && !m_widgetWatcher->directories().contains(d))
            m_widgetWatcher->addPath(d);
    }
}

void WidgetReloader::onWidgetFileChanged(const QString &)
{
    refreshWidgetWatch();

    // Coalesce the burst of events one save produces by hashing the watched files — a
    // no-op notification (touch, or our own re-add) leaves the digest unchanged.
    const QByteArray digest = widgetContentHash();
    if (digest == m_widgetHash)
        return;
    m_widgetHash = digest;
    reloadWidgets();
}

void WidgetReloader::reloadWidgets()
{
    // Drop cached QML so the next Loader source-set re-reads the widget from disk, then
    // bump the generation Bar.qml's widget Loaders are bound to (they reload on change).
    m_engine->clearComponentCache();
    ++m_reloadGeneration;
    emit reloadGenerationChanged();
}
