#include "valueparser.h"

#include <QtGlobal>

namespace CssValueParser {

QColor parseColor(const QString &colorStr)
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
        if (parts.size() == 3) {
            return QColor(parts[0].trimmed().toInt(), parts[1].trimmed().toInt(), parts[2].trimmed().toInt());
        }
    }
    if (s.startsWith(QLatin1String("rgba("))) {
        const QString inner = s.mid(5, s.length() - 6);
        const QStringList parts = inner.split(QLatin1Char(','));
        if (parts.size() == 4) {
            return QColor(parts[0].trimmed().toInt(), parts[1].trimmed().toInt(), parts[2].trimmed().toInt(),
                          static_cast<int>(parts[3].trimmed().toFloat() * 255.0f));
        }
    }
    return QColor(s);
}

QStringList splitTopLevel(const QString &text, QChar sep)
{
    QStringList out;
    int depth = 0;
    int start = 0;
    for (int i = 0; i < text.length(); ++i) {
        const QChar c = text[i];
        if (c == QLatin1Char('(')) {
            ++depth;
        } else if (c == QLatin1Char(')')) {
            --depth;
        } else if (depth == 0 && c == sep) {
            out.append(text.mid(start, i - start));
            start = i + 1;
        }
    }
    out.append(text.mid(start));
    return out;
}

QStringList splitTopLevelWhitespace(const QString &text)
{
    QStringList out;
    int depth = 0;
    QString current;
    for (const QChar c : text) {
        if (c == QLatin1Char('(')) {
            ++depth;
            current.append(c);
        } else if (c == QLatin1Char(')')) {
            --depth;
            current.append(c);
        } else if (depth == 0 && c.isSpace()) {
            if (!current.isEmpty()) {
                out.append(current);
                current.clear();
            }
        } else {
            current.append(c);
        }
    }
    if (!current.isEmpty()) {
        out.append(current);
    }
    return out;
}

bool parseLengthPx(const QString &token, double *out)
{
    QString t = token.trimmed();
    if (t.endsWith(QLatin1String("px"), Qt::CaseInsensitive)) {
        t = t.left(t.length() - 2);
    }
    bool ok = false;
    const double v = t.toDouble(&ok);
    if (ok && out != nullptr) {
        *out = v;
    }
    return ok;
}

int parseDuration(const QString &cssValue, int fallbackMs)
{
    const QString s = cssValue.trimmed().toLower();
    if (s.isEmpty()) {
        return fallbackMs;
    }

    bool ok = false;
    const double value = s.endsWith(QStringLiteral("ms"))
        ? s.left(s.length() - 2).trimmed().toDouble(&ok)
        : (s.endsWith(QLatin1Char('s'))
            ? s.left(s.length() - 1).trimmed().toDouble(&ok) * 1000.0
            : s.toDouble(&ok));

    return ok ? qMax(0, qRound(value)) : fallbackMs;
}

QEasingCurve::Type parseEasing(const QString &cssValue, QEasingCurve::Type fallback)
{
    QString n = cssValue.trimmed().toLower();
    n.remove(QLatin1Char('-'));
    n.remove(QLatin1Char('_'));
    n.remove(QLatin1Char(' '));

    if (n.isEmpty()) return fallback;
    if (n == QStringLiteral("linear")) return QEasingCurve::Linear;
    if (n == QStringLiteral("ease") || n == QStringLiteral("easeinout") || n == QStringLiteral("inoutquad")) return QEasingCurve::InOutQuad;
    if (n == QStringLiteral("easein") || n == QStringLiteral("inquad")) return QEasingCurve::InQuad;
    if (n == QStringLiteral("easeout") || n == QStringLiteral("outquad")) return QEasingCurve::OutQuad;
    if (n == QStringLiteral("outinquad")) return QEasingCurve::OutInQuad;
    if (n == QStringLiteral("incubic")) return QEasingCurve::InCubic;
    if (n == QStringLiteral("outcubic")) return QEasingCurve::OutCubic;
    if (n == QStringLiteral("inoutcubic")) return QEasingCurve::InOutCubic;
    if (n == QStringLiteral("inquart")) return QEasingCurve::InQuart;
    if (n == QStringLiteral("outquart")) return QEasingCurve::OutQuart;
    if (n == QStringLiteral("inoutquart")) return QEasingCurve::InOutQuart;
    if (n == QStringLiteral("inquint")) return QEasingCurve::InQuint;
    if (n == QStringLiteral("outquint")) return QEasingCurve::OutQuint;
    if (n == QStringLiteral("inoutquint")) return QEasingCurve::InOutQuint;
    if (n == QStringLiteral("insine")) return QEasingCurve::InSine;
    if (n == QStringLiteral("outsine")) return QEasingCurve::OutSine;
    if (n == QStringLiteral("inoutsine")) return QEasingCurve::InOutSine;
    if (n == QStringLiteral("inexpo")) return QEasingCurve::InExpo;
    if (n == QStringLiteral("outexpo")) return QEasingCurve::OutExpo;
    if (n == QStringLiteral("inoutexpo")) return QEasingCurve::InOutExpo;
    if (n == QStringLiteral("incirc")) return QEasingCurve::InCirc;
    if (n == QStringLiteral("outcirc")) return QEasingCurve::OutCirc;
    if (n == QStringLiteral("inoutcirc")) return QEasingCurve::InOutCirc;
    if (n == QStringLiteral("inelastic")) return QEasingCurve::InElastic;
    if (n == QStringLiteral("outelastic")) return QEasingCurve::OutElastic;
    if (n == QStringLiteral("inoutelastic")) return QEasingCurve::InOutElastic;
    if (n == QStringLiteral("inback")) return QEasingCurve::InBack;
    if (n == QStringLiteral("outback")) return QEasingCurve::OutBack;
    if (n == QStringLiteral("inoutback")) return QEasingCurve::InOutBack;
    if (n == QStringLiteral("inbounce")) return QEasingCurve::InBounce;
    if (n == QStringLiteral("outbounce")) return QEasingCurve::OutBounce;
    if (n == QStringLiteral("inoutbounce")) return QEasingCurve::InOutBounce;
    return fallback;
}

} // namespace CssValueParser
