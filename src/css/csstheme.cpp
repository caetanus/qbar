#include "csstheme.h"

#include "valueparser.h"

#include <QCryptographicHash>
#include <QDebug>
#include <QFile>
#include <QHash>
#include <QMetaMethod>
#include <QMetaProperty>
#include <QRegularExpression>
#include <algorithm>

namespace {

QString stripComments(const QString &css)
{
    static const QRegularExpression re(QStringLiteral(R"(/\*.*?\*/)"),
                                       QRegularExpression::DotMatchesEverythingOption);
    QString result = css;
    result.remove(re);
    return result;
}

// Replace `@name` references with values from `vars`; unknown names (e.g.
// `@media`) are left untouched.
QString substituteVars(const QString &text, const QHash<QString, QString> &vars)
{
    static const QRegularExpression refRe(QStringLiteral(R"(@([A-Za-z0-9_-]+))"));
    QString result;
    qsizetype last = 0;
    auto it = refRe.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        result += text.mid(last, m.capturedStart() - last);
        const QString name = m.captured(1);
        result += vars.contains(name) ? vars.value(name) : m.captured(0);
        last = m.capturedEnd();
    }
    result += text.mid(last);
    return result;
}

// Expand GTK-style `@define-color name value;` declarations: collect them,
// resolve references between them, substitute `@name` throughout, then strip
// the declarations themselves.
QString expandDefineColors(const QString &cssIn)
{
    static const QRegularExpression defRe(
        QStringLiteral(R"(@define-color\s+([A-Za-z0-9_-]+)\s+([^;]+);)"));

    QHash<QString, QString> vars;
    auto it = defRe.globalMatch(cssIn);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        vars.insert(m.captured(1).trimmed(), m.captured(2).trimmed());
    }
    if (vars.isEmpty())
        return cssIn;

    // Resolve references between definitions (e.g. `@define-color a @b;`).
    for (int pass = 0; pass < 5; ++pass) {
        bool changed = false;
        for (auto v = vars.begin(); v != vars.end(); ++v) {
            const QString expanded = substituteVars(v.value(), vars);
            if (expanded != v.value()) {
                v.value() = expanded;
                changed = true;
            }
        }
        if (!changed)
            break;
    }

    QString css = cssIn;
    css.remove(defRe);
    return substituteVars(css, vars);
}

CssSimpleSelector parseSimpleSelector(const QString &raw)
{
    CssSimpleSelector sel;
    const QString s = raw.trimmed();

    if (s.isEmpty() || s == QStringLiteral("*")) {
        sel.universal = true;
        return sel;
    }

    int i = 0;
    while (i < s.length()) {
        const QChar c = s[i];
        if (c == QLatin1Char('*')) {
            sel.universal = true;
            ++i;
        } else if (c == QLatin1Char('#')) {
            int j = i + 1;
            while (j < s.length() && (s[j].isLetterOrNumber() || s[j] == QLatin1Char('-') || s[j] == QLatin1Char('_')))
                ++j;
            sel.id = s.mid(i + 1, j - i - 1);
            sel.specificity += 100;
            i = j;
        } else if (c == QLatin1Char('.')) {
            int j = i + 1;
            while (j < s.length() && (s[j].isLetterOrNumber() || s[j] == QLatin1Char('-') || s[j] == QLatin1Char('_')))
                ++j;
            sel.classes.append(s.mid(i + 1, j - i - 1));
            sel.specificity += 10;
            i = j;
        } else if (c == QLatin1Char(':')) {
            // Pseudo-classes (single colon, e.g. :hover) become state classes the
            // widget must supply at resolve time; pseudo-elements (::x) are ignored.
            int j = i + 1;
            bool pseudoElement = false;
            if (j < s.length() && s[j] == QLatin1Char(':')) {
                pseudoElement = true;
                ++j;
            }
            const int nameStart = j;
            while (j < s.length() && (s[j].isLetterOrNumber() || s[j] == QLatin1Char('-')))
                ++j;
            if (!pseudoElement) {
                sel.classes.append(s.mid(nameStart, j - nameStart));
                sel.specificity += 10;
            } else {
                // Pseudo-elements (::before/::after) name a decorative overlay; keep
                // the name so resolve*() can target it instead of dropping it.
                sel.pseudoElement = s.mid(nameStart, j - nameStart);
                sel.specificity += 1;
            }
            i = j;
        } else if (c.isLetter() || c == QLatin1Char('_')) {
            int j = i;
            while (j < s.length() && (s[j].isLetterOrNumber() || s[j] == QLatin1Char('-') || s[j] == QLatin1Char('_')))
                ++j;
            sel.element = s.mid(i, j - i);
            sel.specificity += 1;
            i = j;
        } else {
            ++i;
        }
    }

    return sel;
}

struct FullSelectorParse {
    QString ancestorId;
    CssSimpleSelector subject;
};

// Split a full selector on combinators and extract the subject (last token)
// plus the ancestor id from the leading part.
FullSelectorParse parseFullSelector(const QString &fullSelector)
{
    static const QRegularExpression combinatorRe(QStringLiteral(R"(\s*[>+~]\s*|\s+)"));
    const QStringList parts = fullSelector.trimmed().split(combinatorRe, Qt::SkipEmptyParts);

    FullSelectorParse result;
    if (parts.isEmpty()) {
        result.subject.universal = true;
        return result;
    }

    result.subject = parseSimpleSelector(parts.last());

    for (int i = 0; i + 1 < parts.size(); ++i) {
        const CssSimpleSelector ancestor = parseSimpleSelector(parts.at(i));
        result.subject.specificity += ancestor.specificity;
        // Take the first #id found in the ancestor chain as context key
        if (!ancestor.id.isEmpty() && result.ancestorId.isEmpty())
            result.ancestorId = ancestor.id;
    }

    return result;
}

QVariantMap parseDeclarationBlock(const QString &block)
{
    QVariantMap props;
    int i = 0;
    while (i < block.length()) {
        int end = block.indexOf(QLatin1Char(';'), i);
        if (end < 0)
            end = block.length();
        const QString decl = block.mid(i, end - i).trimmed();
        const int colon = decl.indexOf(QLatin1Char(':'));
        if (colon > 0) {
            const QString prop = decl.left(colon).trimmed().toLower();
            QString val = decl.mid(colon + 1).trimmed();
            // Normalize CSS color keywords that Qt cannot parse to an explicit
            // fully-transparent hex, in the engine, so every consumer gets a usable
            // value — a raw QML `color:` binding or a Canvas fillStyle, not just
            // parseColor(). Qt's QColor rejects `transparent`/`none`/`inherit` on those
            // raw paths and falls back to black; `#00000000` reads as transparent black
            // under both Qt's #AARRGGBB and CSS's #RRGGBBAA. Whole-value only: a gradient
            // keeps its inner `transparent` stop (parseGradient routes it through
            // parseColor) and substrings inside url()/font-family are never touched.
            // `transparent` is always a colour keyword so it normalizes on any property;
            // `none`/`inherit` mean "absent" on non-colour shorthands (border/box-shadow),
            // so they only fold on colour properties.
            const QString low = val.toLower();
            const bool colorProp = prop.endsWith(QLatin1String("color"))
                || prop == QLatin1String("fill")
                || prop == QLatin1String("background");
            if (low == QLatin1String("transparent")
                || (colorProp && (low == QLatin1String("none") || low == QLatin1String("inherit"))))
                val = QStringLiteral("#00000000");
            if (!prop.isEmpty())
                props.insert(prop, val);
        }
        i = end + 1;
    }
    return props;
}

// Parse a `@keyframes` body — `0% { ... } 50% { ... } 100% { ... }` (also `from`/`to`,
// and comma-separated offsets) — into a list of { offset (0..1), properties }, sorted by
// offset. Reuses parseDeclarationBlock for each frame's declarations.
QVariantList parseKeyframeBlock(const QString &block)
{
    QVariantList frames;
    int i = 0;
    while (i < block.length()) {
        const int brace = block.indexOf(QLatin1Char('{'), i);
        if (brace < 0)
            break;
        const QString selector = block.mid(i, brace - i).trimmed();
        const int close = block.indexOf(QLatin1Char('}'), brace + 1);
        if (close < 0)
            break;
        const QVariantMap props = parseDeclarationBlock(block.mid(brace + 1, close - brace - 1));
        const QStringList sels = selector.split(QLatin1Char(','), Qt::SkipEmptyParts);
        for (const QString &s : sels) {
            const QString t = s.trimmed().toLower();
            double offset = -1.0;
            if (t == QLatin1String("from")) {
                offset = 0.0;
            } else if (t == QLatin1String("to")) {
                offset = 1.0;
            } else if (t.endsWith(QLatin1Char('%'))) {
                bool ok = false;
                const double pct = t.left(t.length() - 1).trimmed().toDouble(&ok);
                if (ok)
                    offset = pct / 100.0;
            }
            if (offset >= 0.0) {
                QVariantMap frame;
                frame.insert(QStringLiteral("offset"), offset);
                frame.insert(QStringLiteral("properties"), props);
                frames.append(frame);
            }
        }
        i = close + 1;
    }
    std::sort(frames.begin(), frames.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value(QStringLiteral("offset")).toDouble()
            < b.toMap().value(QStringLiteral("offset")).toDouble();
    });
    return frames;
}

// Extract `@keyframes <name> { ... }` blocks (brace-matched, since they nest — which the
// flat parseCss cannot handle) into `out`, and return the CSS with them removed so the
// ordinary rule parser never sees them.
QString extractKeyframes(const QString &css, QHash<QString, QVariantList> &out)
{
    QString result;
    int i = 0;
    while (i < css.length()) {
        const int at = css.indexOf(QStringLiteral("@keyframes"), i, Qt::CaseInsensitive);
        if (at < 0) {
            result += css.mid(i);
            break;
        }
        result += css.mid(i, at - i);
        int j = at + 10; // past "@keyframes"
        while (j < css.length() && css[j].isSpace())
            ++j;
        const int nameStart = j;
        while (j < css.length() && css[j] != QLatin1Char('{') && !css[j].isSpace())
            ++j;
        const QString name = css.mid(nameStart, j - nameStart).trimmed();
        while (j < css.length() && css[j] != QLatin1Char('{'))
            ++j;
        if (j >= css.length())
            break;
        int depth = 0;
        int k = j;
        for (; k < css.length(); ++k) {
            if (css[k] == QLatin1Char('{')) {
                ++depth;
            } else if (css[k] == QLatin1Char('}')) {
                --depth;
                if (depth == 0) {
                    ++k;
                    break;
                }
            }
        }
        const QString inner = css.mid(j + 1, k - j - 2);
        if (!name.isEmpty())
            out.insert(name, parseKeyframeBlock(inner));
        i = k;
    }
    return result;
}

QList<CssRule> parseCss(const QString &css)
{
    QList<CssRule> rules;
    const QString cleaned = expandDefineColors(stripComments(css));

    int i = 0;
    while (i < cleaned.length()) {
        const int blockStart = cleaned.indexOf(QLatin1Char('{'), i);
        if (blockStart < 0)
            break;

        const int blockEnd = cleaned.indexOf(QLatin1Char('}'), blockStart + 1);
        if (blockEnd < 0)
            break;

        QString selectorPart = cleaned.mid(i, blockStart - i);
        // Discard any preceding at-rule statements (e.g. "@define-color name #hex;"),
        // which have no {} block of their own and would otherwise be glued onto the
        // next rule's selector text. Selectors never contain ';', so only keep what
        // follows the last one.
        const int lastSemicolon = selectorPart.lastIndexOf(QLatin1Char(';'));
        if (lastSemicolon >= 0)
            selectorPart = selectorPart.mid(lastSemicolon + 1);
        selectorPart = selectorPart.trimmed();
        const QString blockContent = cleaned.mid(blockStart + 1, blockEnd - blockStart - 1);

        if (!selectorPart.isEmpty()) {
            const QVariantMap props = parseDeclarationBlock(blockContent);
            if (!props.isEmpty()) {
                const QStringList selectors = selectorPart.split(QLatin1Char(','), Qt::SkipEmptyParts);
                for (const QString &sel : selectors) {
                    const FullSelectorParse parsed = parseFullSelector(sel.trimmed());
                    rules.append({parsed.ancestorId, parsed.subject, props});
                }
            }
        }

        i = blockEnd + 1;
    }

    return rules;
}

bool selectorMatches(const CssSimpleSelector &sel, const QString &contextId, const QString &id,
                     const QStringList &classes, const QString &pseudoElement)
{
    // A pseudo-element rule (`::before`) only matches when that overlay is requested,
    // and an ordinary rule only matches when no overlay is requested.
    if (sel.pseudoElement != pseudoElement)
        return false;
    // Element-only selectors (e.g. bare `label` from GTK CSS) have no id and no classes.
    // They make no sense at the top level in qbar — only allow them when there is an ancestor
    // context (i.e. called via resolveWith), so they don't bleed into every applet.
    if (!sel.universal && sel.id.isEmpty() && sel.classes.isEmpty() && contextId.isEmpty())
        return false;
    if (!sel.id.isEmpty() && sel.id != id)
        return false;
    for (const QString &cls : sel.classes) {
        if (!classes.contains(cls))
            return false;
    }
    return true;
}

// CSS gradient side keywords → angle (0deg points up, increasing clockwise).
double cssSideToAngle(const QString &sideRaw)
{
    const QString side = sideRaw.toLower().simplified();
    if (side == QLatin1String("top")) return 0.0;
    if (side == QLatin1String("right")) return 90.0;
    if (side == QLatin1String("bottom")) return 180.0;
    if (side == QLatin1String("left")) return 270.0;
    if (side == QLatin1String("top right") || side == QLatin1String("right top")) return 45.0;
    if (side == QLatin1String("bottom right") || side == QLatin1String("right bottom")) return 135.0;
    if (side == QLatin1String("bottom left") || side == QLatin1String("left bottom")) return 225.0;
    if (side == QLatin1String("top left") || side == QLatin1String("left top")) return 315.0;
    return 180.0;
}

} // namespace

CssTheme::CssTheme(QObject *parent)
    : QObject(parent)
    , m_watcher(new QFileSystemWatcher(this))
{
    connect(m_watcher, &QFileSystemWatcher::fileChanged, this, &CssTheme::onCssFileChanged);
}

void CssTheme::load(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "qbar: cannot open CSS file" << path;
        return;
    }
    const QByteArray data = file.readAll();
    const QByteArray hash = QCryptographicHash::hash(data, QCryptographicHash::Md5);

    // Re-add to watcher (editors often atomically replace files, removing them from the watch list)
    if (!m_watcher->files().contains(path)) {
        m_watcher->addPath(path);
    }

    if (hash == m_contentHash) {
        return; // content unchanged — touch or spurious notification
    }

    // Stop watching the old path when switching files
    if (!m_watchedPath.isEmpty() && m_watchedPath != path)
        m_watcher->removePath(m_watchedPath);

    m_contentHash = hash;
    m_watchedPath = path;
    loadFromString(QString::fromUtf8(data));
}

void CssTheme::onCssFileChanged(const QString &path)
{
    load(path);
}

void CssTheme::loadFromString(const QString &css)
{
    // @keyframes blocks nest, which the flat parseCss can't handle — extract them first
    // (on the comment-stripped, @define-color-expanded source so frame values resolve).
    const QString cleaned = expandDefineColors(stripComments(css));
    m_keyframes.clear();
    const QString body = extractKeyframes(cleaned, m_keyframes);
    m_rules = parseCss(body);
    m_loaded = true;
    emit loadedChanged();
    // Reverse slot: push freshly-resolved styles to every registered object so a theme
    // reload re-styles the live UI without any QML binding.
    reapplyAll();
}

QVariantMap CssTheme::parseAnimation(const QString &cssValue) const
{
    return CssValueParser::parseAnimation(cssValue);
}

QVariantList CssTheme::keyframes(const QString &name) const
{
    return m_keyframes.value(name);
}

// Coerce a CssQmlItem identity property (cssAlternateId / cssClass) to a string list:
// it may be authored as a single string ("waybar", "focused"), a space-separated string,
// or a QML list (["network", "nm-applet"]).
static QStringList cssVariantToStringList(const QVariant &v)
{
    if (!v.isValid())
        return {};
    if (v.metaType().id() == QMetaType::QStringList)
        return v.toStringList();
    if (v.metaType().id() == QMetaType::QVariantList) {
        QStringList out;
        const QVariantList list = v.toList();
        for (const QVariant &e : list) {
            const QString s = e.toString().trimmed();
            if (!s.isEmpty())
                out << s;
        }
        return out;
    }
    return v.toString().split(QLatin1Char(' '), Qt::SkipEmptyParts);
}

QVariantMap CssTheme::resolveMerged(const QString &cssId, const QStringList &alternateIds,
                                    const QStringList &classes) const
{
    QVariantMap merged;
    // Waybar-compat aliases first (lowest priority), then the primary id wins on conflict.
    for (const QString &alt : alternateIds) {
        if (alt.isEmpty())
            continue;
        const QVariantMap m = resolve(alt, classes);
        for (auto it = m.constBegin(); it != m.constEnd(); ++it)
            merged.insert(it.key(), it.value());
    }
    if (!cssId.isEmpty()) {
        const QVariantMap primary = resolve(cssId, classes);
        for (auto it = primary.constBegin(); it != primary.constEnd(); ++it)
            merged.insert(it.key(), it.value());
    }
    return merged;
}

void CssTheme::applyCssTo(QObject *target) const
{
    if (!target)
        return;

    // Identity is read straight off the CssQmlItem target (re-read every apply, so a
    // later cssClass change is reflected on the next apply / reload).
    const QString cssId = target->property("cssId").toString();
    const QStringList classes = cssVariantToStringList(target->property("cssClass"));
    const QString cssPart = target->property("cssPart").toString();

    // A part target (`#tray.item`, `#cpu.graph`) resolves ONLY that part — excluding the
    // bare `#id` base — so a sub-element doesn't inherit the container's own background.
    // Otherwise merge the waybar alias(es) under the primary id.
    QVariantMap style;
    if (!cssPart.isEmpty()) {
        style = resolvePart(cssId, cssPart, classes);
    } else {
        const QStringList alternateIds = cssVariantToStringList(target->property("cssAlternateId"));
        style = resolveMerged(cssId, alternateIds, classes);
    }

    // Push the resolved map into the target's `style` sink; its renderer keys off it
    // (gradient/box-shadow/bevel with the alpha-fix a plain Rectangle cannot do).
    target->setProperty("style", style);
}

void CssTheme::loadCss(QObject *target)
{
    if (!target)
        return;

    // The engine's one guarantee: the target must carry the CssQmlItem signature
    // (identity `cssId` + a `style` sink). Without it loadCss would resolve nothing and
    // style nothing — i.e. "quebra o brinquedo silenciosamente". So fail LOUDLY instead.
    const QMetaObject *mo = target->metaObject();
    if (mo->indexOfProperty("cssId") < 0 || mo->indexOfProperty("style") < 0) {
        qWarning().noquote() << "CssTheme::loadCss: target" << mo->className()
                             << "lacks the CssQmlItem signature (needs cssId + style) — refusing to style it.";
        return;
    }

    // Register once; the reverse slot re-reads the object's identity on every (re)load.
    if (!m_bindings.contains(QPointer<QObject>(target))) {
        m_bindings.append(QPointer<QObject>(target));
        // The reverse slot must never dangle: drop the registration when the object dies.
        connect(target, &QObject::destroyed, this, [this](QObject *obj) {
            m_bindings.removeIf([obj](const QPointer<QObject> &p) { return p.isNull() || p == obj; });
        });
        // Observe the registered cssClass: a state change (focused/urgent/hover) must
        // re-style automatically, so the applet registers once and never re-calls loadCss.
        const int classProp = mo->indexOfProperty("cssClass");
        if (classProp >= 0) {
            const QMetaProperty prop = mo->property(classProp);
            if (prop.hasNotifySignal()) {
                const QMetaMethod slot = staticMetaObject.method(
                    staticMetaObject.indexOfSlot("reapplyForSender()"));
                connect(target, prop.notifySignal(), this, slot);
            }
        }
    }

    applyCssTo(target);
}

void CssTheme::reapplyForSender()
{
    if (QObject *target = sender())
        applyCssTo(target);
}

void CssTheme::reapplyAll()
{
    m_bindings.removeIf([](const QPointer<QObject> &p) { return p.isNull(); });
    for (const QPointer<QObject> &p : m_bindings) {
        if (p)
            applyCssTo(p);
    }
}

QVariantMap CssTheme::resolve(const QString &id, const QStringList &classes, const QString &pseudoElement) const
{
    return resolveImpl(QString(), id, classes, pseudoElement);
}

QVariantMap CssTheme::resolveWith(const QString &contextId, const QString &id, const QStringList &classes,
                                  const QString &pseudoElement) const
{
    return resolveImpl(contextId, id, classes, pseudoElement);
}

QVariantMap CssTheme::resolveImpl(const QString &contextId, const QString &id, const QStringList &classes,
                                  const QString &pseudoElement) const
{
    struct Match {
        int specificity;
        int sourceOrder;
        QVariantMap props;
    };

    QList<Match> matches;
    for (int i = 0; i < m_rules.size(); ++i) {
        const CssRule &rule = m_rules.at(i);
        // A rule with a required ancestor only matches when called with that ancestor as context.
        // Rules with no ancestor requirement match in all contexts.
        if (!rule.requiredAncestorId.isEmpty() && rule.requiredAncestorId != contextId)
            continue;
        if (selectorMatches(rule.selector, contextId, id, classes, pseudoElement))
            matches.append({rule.selector.specificity, i, rule.properties});
    }

    std::stable_sort(matches.begin(), matches.end(), [](const Match &a, const Match &b) {
        return a.specificity != b.specificity ? a.specificity < b.specificity : a.sourceOrder < b.sourceOrder;
    });

    QVariantMap result;
    for (const Match &m : matches) {
        for (auto it = m.props.constBegin(); it != m.props.constEnd(); ++it)
            result.insert(it.key(), it.value());
    }
    return result;
}

QVariantMap CssTheme::resolveExact(const QString &id, const QStringList &classes, const QString &pseudoElement,
                                   const QString &requiredClass) const
{
    struct Match {
        int specificity;
        int sourceOrder;
        QVariantMap props;
    };

    QList<Match> matches;
    for (int i = 0; i < m_rules.size(); ++i) {
        const CssRule &rule = m_rules.at(i);
        if (!rule.requiredAncestorId.isEmpty())
            continue;
        const CssSimpleSelector &sel = rule.selector;
        if (sel.id != id || sel.pseudoElement != pseudoElement)
            continue;
        // `requiredClass` (used by resolvePart) restricts to rules carrying that class,
        // e.g. `#cpu.graph`, so the bare `#cpu` base is excluded.
        if (!requiredClass.isEmpty() && !sel.classes.contains(requiredClass))
            continue;
        // Every other class the selector demands must be a supplied state (the required
        // class is implicit), so `#cpu.graph:active` matches only when "active" is asked.
        bool ok = true;
        for (const QString &cls : sel.classes)
            if (cls != requiredClass && !classes.contains(cls)) { ok = false; break; }
        if (!ok)
            continue;
        matches.append({sel.specificity, i, rule.properties});
    }

    std::stable_sort(matches.begin(), matches.end(), [](const Match &a, const Match &b) {
        return a.specificity != b.specificity ? a.specificity < b.specificity : a.sourceOrder < b.sourceOrder;
    });

    QVariantMap result;
    for (const Match &m : matches)
        for (auto it = m.props.constBegin(); it != m.props.constEnd(); ++it)
            result.insert(it.key(), it.value());
    return result;
}

QVariantMap CssTheme::resolvePart(const QString &id, const QString &part, const QStringList &classes,
                                  const QString &pseudoElement) const
{
    // A "part" is an exact-match query that REQUIRES the part class — see resolveExact.
    return resolveExact(id, classes, pseudoElement, part);
}

QColor CssTheme::parseColor(const QString &cssColor) const
{
    return CssValueParser::parseColor(cssColor);
}

qreal CssTheme::parseLength(const QString &value, qreal fallback) const
{
    double out = 0.0;
    if (CssValueParser::parseLengthPx(value, &out))
        return out;
    return fallback;
}

QVariantMap CssTheme::parseGradient(const QString &cssValue) const
{
    const QString s = cssValue.trimmed();
    const QString prefix = QStringLiteral("linear-gradient(");
    if (!s.startsWith(prefix, Qt::CaseInsensitive) || !s.endsWith(QLatin1Char(')')))
        return {};

    const QString inner = s.mid(prefix.length(), s.length() - prefix.length() - 1);
    QStringList parts = CssValueParser::splitTopLevel(inner, QLatin1Char(','));
    for (QString &p : parts)
        p = p.trimmed();
    if (parts.isEmpty())
        return {};

    double angle = 180.0; // CSS default direction: "to bottom"
    int firstStop = 0;
    const QString head = parts.first();
    if (head.endsWith(QLatin1String("deg"), Qt::CaseInsensitive)) {
        angle = head.left(head.length() - 3).trimmed().toDouble();
        firstStop = 1;
    } else if (head.startsWith(QLatin1String("to "), Qt::CaseInsensitive)) {
        angle = cssSideToAngle(head.mid(3));
        firstStop = 1;
    }

    QList<QColor> colors;
    QList<double> positions;
    for (int i = firstStop; i < parts.size(); ++i) {
        const QStringList toks = CssValueParser::splitTopLevelWhitespace(parts.at(i));
        if (toks.isEmpty())
            continue;
        colors.append(CssValueParser::parseColor(toks.first()));
        double pos = -1.0;
        if (toks.size() >= 2 && toks.at(1).endsWith(QLatin1Char('%')))
            pos = toks.at(1).left(toks.at(1).length() - 1).toDouble() / 100.0;
        positions.append(pos);
    }
    if (colors.size() < 2)
        return {};

    QVariantList stops;
    for (int i = 0; i < colors.size(); ++i) {
        double pos = positions.at(i);
        if (pos < 0.0)
            pos = static_cast<double>(i) / static_cast<double>(colors.size() - 1);
        QVariantMap stop;
        stop.insert(QStringLiteral("position"), pos);
        stop.insert(QStringLiteral("color"), colors.at(i));
        stops.append(stop);
    }

    QVariantMap result;
    result.insert(QStringLiteral("type"), QStringLiteral("linear"));
    result.insert(QStringLiteral("angle"), angle);
    result.insert(QStringLiteral("stops"), stops);
    return result;
}

static QVariantMap parseOneBoxShadow(const QString &segment)
{
    const QStringList toks = CssValueParser::splitTopLevelWhitespace(segment.trimmed());

    bool inset = false;
    QList<double> lengths;
    QColor color(0, 0, 0, 128);
    for (const QString &tok : toks) {
        if (tok.compare(QLatin1String("inset"), Qt::CaseInsensitive) == 0) {
            inset = true;
            continue;
        }
        double value = 0.0;
        if (CssValueParser::parseLengthPx(tok, &value)) {
            lengths.append(value);
            continue;
        }
        color = CssValueParser::parseColor(tok);
    }
    if (lengths.size() < 2)
        return {};

    QVariantMap result;
    result.insert(QStringLiteral("x"), lengths.value(0, 0.0));
    result.insert(QStringLiteral("y"), lengths.value(1, 0.0));
    result.insert(QStringLiteral("blur"), lengths.value(2, 0.0));
    result.insert(QStringLiteral("spread"), lengths.value(3, 0.0));
    result.insert(QStringLiteral("color"), color);
    result.insert(QStringLiteral("inset"), inset);
    return result;
}

QVariantMap CssTheme::parseBoxShadow(const QString &cssValue) const
{
    const QString s = cssValue.trimmed();
    if (s.isEmpty() || s.compare(QLatin1String("none"), Qt::CaseInsensitive) == 0)
        return {};
    // First shadow only — take the first comma-separated segment.
    return parseOneBoxShadow(CssValueParser::splitTopLevel(s, QLatin1Char(',')).first());
}

QVariantList CssTheme::parseBoxShadowList(const QString &cssValue) const
{
    QVariantList shadows;
    const QString s = cssValue.trimmed();
    if (s.isEmpty() || s.compare(QLatin1String("none"), Qt::CaseInsensitive) == 0)
        return shadows;
    const QStringList segments = CssValueParser::splitTopLevel(s, QLatin1Char(','));
    for (const QString &segment : segments) {
        const QVariantMap shadow = parseOneBoxShadow(segment);
        if (!shadow.isEmpty())
            shadows.append(shadow);
    }
    return shadows;
}

int CssTheme::parseDuration(const QString &cssValue, int fallbackMs) const
{
    return CssValueParser::parseDuration(cssValue, fallbackMs);
}

int CssTheme::parseEasing(const QString &cssValue, int fallbackType) const
{
    return static_cast<int>(CssValueParser::parseEasing(cssValue, static_cast<QEasingCurve::Type>(fallbackType)));
}

QVariantMap CssTheme::parseTransition(const QString &cssValue) const
{
    return CssValueParser::parseTransition(cssValue);
}
