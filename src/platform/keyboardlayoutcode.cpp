#include "keyboardlayoutcode.h"

#include <QHash>

#include <xkbcommon/xkbregistry.h>

namespace qbar {
namespace {

// Display nicknames for a canonical xkb code. The registry returns the official code
// ("us" for US English), but the bar shows a friendlier label for a few of them
// (e.g. "en"). Anything not listed is shown as its plain xkb code. Add entries here
// to rename more layouts.
QString displayAlias(const QString &code)
{
    static const QHash<QString, QString> aliases = {
        {QStringLiteral("us"), QStringLiteral("en")},
    };
    return aliases.value(code, code);
}

// Ask the xkb registry for the code of a layout given its human description, e.g.
// "Portuguese (Brazil)" → "br", "English (US)" → "us". This is the canonical mapping;
// it covers every layout xkb knows about, so no per-language special-casing is needed.
QString registryLayoutCode(const QString &layoutDescription)
{
    rxkb_context *context = rxkb_context_new(RXKB_CONTEXT_LOAD_EXOTIC_RULES);
    if (context == nullptr) {
        return {};
    }

    rxkb_context_parse_default_ruleset(context);
    QString code;
    for (rxkb_layout *layout = rxkb_layout_first(context); layout != nullptr; layout = rxkb_layout_next(layout)) {
        const char *description = rxkb_layout_get_description(layout);
        if (description == nullptr || layoutDescription != QString::fromUtf8(description)) {
            continue;
        }

        const char *name = rxkb_layout_get_name(layout);
        if (name != nullptr) {
            code = QString::fromUtf8(name);
        }
        break;
    }

    rxkb_context_unref(context);
    return code;
}

// Fallback for inputs the registry can't resolve (a raw code like "br", or a partial
// description). Recognises the common names, then takes the parenthesised code or the
// leading two letters.
QString normalizedLayoutCode(const QString &layout)
{
    QString value = layout.trimmed().toLower();
    if (value.isEmpty()) {
        return {};
    }

    if (value == QStringLiteral("br") || value.contains(QStringLiteral("brazil"))) {
        return QStringLiteral("br");
    }
    if (value == QStringLiteral("us") || value.contains(QStringLiteral("united states")) || value.contains(QStringLiteral("(us)"))) {
        return QStringLiteral("us");
    }
    if (value == QStringLiteral("pt") || value.contains(QStringLiteral("portuguese"))) {
        return QStringLiteral("br");
    }
    if (value == QStringLiteral("en") || value.contains(QStringLiteral("english"))) {
        return QStringLiteral("us");
    }

    const int parenStart = value.indexOf(QLatin1Char('('));
    const int parenEnd = value.indexOf(QLatin1Char(')'), parenStart + 1);
    if (parenStart >= 0 && parenEnd > parenStart + 1) {
        value = value.mid(parenStart + 1, parenEnd - parenStart - 1).trimmed();
    }

    return value.left(2);
}

} // namespace

QString keyboardLayoutCode(const QString &layoutNameOrDescription)
{
    QString code = registryLayoutCode(layoutNameOrDescription);
    if (code.isEmpty()) {
        code = normalizedLayoutCode(layoutNameOrDescription);
    }
    return displayAlias(code);
}

} // namespace qbar
