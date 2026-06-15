#pragma once

#include <QByteArray>
#include <QColor>
#include <QFileSystemWatcher>
#include <QObject>
#include <QStringList>
#include <QVariantMap>

struct CssSimpleSelector {
    bool universal = false;
    QString element;
    QString id;
    QStringList classes;
    int specificity = 0;
};

struct CssRule {
    QString requiredAncestorId;
    CssSimpleSelector selector;
    QVariantMap properties;
};

class CssTheme : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool loaded READ isLoaded NOTIFY loadedChanged)

public:
    explicit CssTheme(QObject *parent = nullptr);

    bool isLoaded() const { return m_loaded; }

    void load(const QString &path);
    void loadFromString(const QString &css);

    // Resolve styles for an element (top-level selectors only)
    Q_INVOKABLE QVariantMap resolve(const QString &id, const QStringList &classes = {}) const;

    // Resolve with ancestor context — matches descendant selectors like "#workspaces button.focused"
    Q_INVOKABLE QVariantMap resolveWith(const QString &contextId, const QString &id, const QStringList &classes = {}) const;

    // Resolve only rules that explicitly target this id — universal (*) rules are excluded.
    // Use this when you want to apply id-specific overrides on top of an already-resolved base.
    Q_INVOKABLE QVariantMap resolveExact(const QString &id, const QStringList &classes = {}) const;

    // Parse a CSS color string to a QColor
    Q_INVOKABLE QColor parseColor(const QString &cssColor) const;

signals:
    void loadedChanged();

private slots:
    void onCssFileChanged(const QString &path);

private:
    QVariantMap resolveImpl(const QString &contextId, const QString &id, const QStringList &classes) const;

    QList<CssRule> m_rules;
    bool m_loaded = false;
    QString m_watchedPath;
    QByteArray m_contentHash;
    QFileSystemWatcher *m_watcher = nullptr;
};
