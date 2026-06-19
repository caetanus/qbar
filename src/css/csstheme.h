#pragma once

#include <QByteArray>
#include <QColor>
#include <QFileSystemWatcher>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

struct CssSimpleSelector {
    bool universal = false;
    QString element;
    QString id;
    QStringList classes;
    // Pseudo-element after `::` (e.g. "before"/"after"), used for decorative
    // sub-overlays. Empty for ordinary selectors.
    QString pseudoElement;
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

    // Resolve styles for an element (top-level selectors only). `pseudoElement`
    // selects a `::before`/`::after` overlay; empty (default) matches only ordinary
    // selectors, so existing callers never pick up overlay rules by accident.
    Q_INVOKABLE QVariantMap resolve(const QString &id, const QStringList &classes = {},
                                    const QString &pseudoElement = {}) const;

    // Resolve with ancestor context — matches descendant selectors like "#workspaces button.focused"
    Q_INVOKABLE QVariantMap resolveWith(const QString &contextId, const QString &id, const QStringList &classes = {},
                                        const QString &pseudoElement = {}) const;

    // Resolve only rules that explicitly target this id — universal (*) rules are excluded.
    // Use this when you want to apply id-specific overrides on top of an already-resolved base.
    // `requiredClass`, when set, additionally restricts to rules carrying that class
    // (this is what powers resolvePart).
    Q_INVOKABLE QVariantMap resolveExact(const QString &id, const QStringList &classes = {},
                                         const QString &pseudoElement = {}, const QString &requiredClass = {}) const;

    // Resolve a named "part" of a module: only rules whose selector REQUIRES `part`
    // as a class, e.g. resolvePart("cpu", "graph") for `#cpu.graph { ... }`. Unlike
    // resolve/resolveExact it does NOT include the bare `#cpu` base, so a part can
    // use standard props (background-color, color, width) without inheriting the
    // module's own. Extra `classes` allow part state, e.g. `#cpu.graph:active`;
    // `pseudoElement` selects a `::before`/`::after` overlay of the part.
    Q_INVOKABLE QVariantMap resolvePart(const QString &id, const QString &part, const QStringList &classes = {},
                                        const QString &pseudoElement = {}) const;

    // Parse a CSS color string to a QColor
    Q_INVOKABLE QColor parseColor(const QString &cssColor) const;

    // Parse a CSS length ("11px", "8") to pixels, or `fallback` when unparseable.
    Q_INVOKABLE qreal parseLength(const QString &value, qreal fallback) const;

    // Parse `linear-gradient(<angle|to side>, <color> [<pos%>], ...)`.
    // Returns { "type": "linear", "angle": <deg, CSS convention>,
    //           "stops": [ { "position": <0..1>, "color": <QColor> }, ... ] }
    // or an empty map when the value is not a gradient.
    Q_INVOKABLE QVariantMap parseGradient(const QString &cssValue) const;

    // Parse `box-shadow: [inset] <x> <y> <blur> [spread] <color>`.
    // Returns { "x","y","blur","spread" (px), "color" (QColor), "inset" (bool) }
    // or an empty map when unset/none. (First shadow only.)
    Q_INVOKABLE QVariantMap parseBoxShadow(const QString &cssValue) const;

    // Parse a full comma-separated `box-shadow` list into one map per shadow (same
    // shape as parseBoxShadow). Standard CSS allows several shadows — e.g. a sunken
    // bevel is a dark `inset` plus a light `inset` — so a light bevel edge needs no
    // custom property.
    Q_INVOKABLE QVariantList parseBoxShadowList(const QString &cssValue) const;

    // Parse CSS-ish duration values: "180ms", "0.76s", or bare milliseconds.
    Q_INVOKABLE int parseDuration(const QString &cssValue, int fallbackMs) const;

    // Parse CSS/QML easing names into QEasingCurve::Type integer values.
    Q_INVOKABLE int parseEasing(const QString &cssValue, int fallbackType) const;

    // Parse the standard CSS `transition` shorthand into
    // { property, duration (ms), easing (QEasingCurve::Type), delay (ms) }.
    Q_INVOKABLE QVariantMap parseTransition(const QString &cssValue) const;

    // Parse the standard CSS `animation` shorthand into { name, duration (ms), delay (ms),
    // easing (QEasingCurve::Type), iterations (int; -1 = infinite), direction }.
    Q_INVOKABLE QVariantMap parseAnimation(const QString &cssValue) const;

    // Frames of a `@keyframes <name>` block: a list of { offset (0..1), properties },
    // sorted by offset. Empty when no such keyframes were defined.
    Q_INVOKABLE QVariantList keyframes(const QString &name) const;

    // Reverse-slot CSS application. The target carries its own identity, so we read it
    // straight off the object: `cssId`, the optional waybar-compat alias `cssAlternateId`
    // (string or list), the state `cssClass` (string or list), and `cssPrimitive` (or the
    // type itself). loadCss resolves the merged rules (primary id wins over the alias),
    // APPLIES them to `target`, and REGISTERS `target` so the engine re-applies on every
    // theme (re)load — the binding-free, reload-safe path: the engine PUSHES to each live
    // object (an inverted/"reverse" slot, engine→object), so no QML binding is needed and
    // an imperative apply can't go stale. A component re-calls loadCss(this) when its own
    // cssClass changes. Dead targets are pruned automatically.
    Q_INVOKABLE void loadCss(QObject *target);

signals:
    void loadedChanged();

private slots:
    void onCssFileChanged(const QString &path);
    // Connected to a registered target's `cssClass` NOTIFY: re-applies that target's
    // style when its state classes change (focused/urgent/hover/…). Uses sender().
    void reapplyForSender();

private:
    QVariantMap resolveImpl(const QString &contextId, const QString &id, const QStringList &classes,
                            const QString &pseudoElement) const;

    // Merge the waybar alias(es) under the primary id (primary wins), for `classes`.
    QVariantMap resolveMerged(const QString &cssId, const QStringList &alternateIds,
                              const QStringList &classes) const;
    // Read the target's identity properties, resolve, and push the style onto it (the
    // actual "apply the rules" step). Re-reads the object's current cssClass each time.
    void applyCssTo(QObject *target) const;
    // Reverse slot: re-push to every live registered target on (re)load; prune the dead.
    void reapplyAll();

    QList<CssRule> m_rules;
    // Parsed @keyframes by name (each a list of { offset, properties }).
    QHash<QString, QVariantList> m_keyframes;
    // Registered loadCss targets. Identity is re-read off each object at apply time, so
    // a target's cssClass change is picked up on the next apply (reload or loadCss(this)).
    QList<QPointer<QObject>> m_bindings;
    bool m_loaded = false;
    QString m_watchedPath;
    QByteArray m_contentHash;
    QFileSystemWatcher *m_watcher = nullptr;
};
