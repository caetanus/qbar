#pragma once

#include <QColor>
#include <QEasingCurve>
#include <QString>
#include <QStringList>

namespace CssValueParser {

QColor parseColor(const QString &cssColor);
QStringList splitTopLevel(const QString &text, QChar sep);
QStringList splitTopLevelWhitespace(const QString &text);
bool parseLengthPx(const QString &token, double *out);
int parseDuration(const QString &cssValue, int fallbackMs);
QEasingCurve::Type parseEasing(const QString &cssValue, QEasingCurve::Type fallback);

} // namespace CssValueParser
