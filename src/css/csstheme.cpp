#include "csstheme.h"

#include <QCryptographicHash>
#include <QDebug>
#include <QFile>
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
            // Skip pseudo-classes/pseudo-elements — no dynamic state matching needed
            int j = i + 1;
            if (j < s.length() && s[j] == QLatin1Char(':'))
                ++j;
            while (j < s.length() && (s[j].isLetterOrNumber() || s[j] == QLatin1Char('-')))
                ++j;
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
            const QString val  = decl.mid(colon + 1).trimmed();
            if (!prop.isEmpty())
                props.insert(prop, val);
        }
        i = end + 1;
    }
    return props;
}

QList<CssRule> parseCss(const QString &css)
{
    QList<CssRule> rules;
    const QString cleaned = stripComments(css);

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

bool selectorMatches(const CssSimpleSelector &sel, const QString &contextId, const QString &id, const QStringList &classes)
{
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

QColor parseColorString(const QString &colorStr)
{
    const QString s = colorStr.trimmed();
    if (s.compare(QLatin1String("transparent"), Qt::CaseInsensitive) == 0) {
        return QColor(0, 0, 0, 0);
    }
    if (s.startsWith(QLatin1String("#"))) {
        return QColor(s);
    }
    if (s.startsWith(QLatin1String("rgb("))) {
        const QString inner = s.mid(4, s.length() - 5);
        const QStringList parts = inner.split(QLatin1Char(','));
        if (parts.size() == 3)
            return QColor(parts[0].trimmed().toInt(), parts[1].trimmed().toInt(), parts[2].trimmed().toInt());
    }
    if (s.startsWith(QLatin1String("rgba("))) {
        const QString inner = s.mid(5, s.length() - 6);
        const QStringList parts = inner.split(QLatin1Char(','));
        if (parts.size() == 4)
            return QColor(parts[0].trimmed().toInt(), parts[1].trimmed().toInt(), parts[2].trimmed().toInt(),
                          static_cast<int>(parts[3].trimmed().toFloat() * 255.0f));
    }
    return QColor(s); // named colors
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
    m_rules = parseCss(css);
    m_loaded = true;
    emit loadedChanged();
}

QVariantMap CssTheme::resolve(const QString &id, const QStringList &classes) const
{
    return resolveImpl(QString(), id, classes);
}

QVariantMap CssTheme::resolveWith(const QString &contextId, const QString &id, const QStringList &classes) const
{
    return resolveImpl(contextId, id, classes);
}

QVariantMap CssTheme::resolveImpl(const QString &contextId, const QString &id, const QStringList &classes) const
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
        if (selectorMatches(rule.selector, contextId, id, classes))
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

QVariantMap CssTheme::resolveExact(const QString &id, const QStringList &classes) const
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
        if (sel.id != id)
            continue;
        bool ok = true;
        for (const QString &cls : sel.classes)
            if (!classes.contains(cls)) { ok = false; break; }
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

QColor CssTheme::parseColor(const QString &cssColor) const
{
    return parseColorString(cssColor);
}

