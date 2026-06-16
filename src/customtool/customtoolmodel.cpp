#include "customtoolmodel.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QtGlobal>

namespace {

QStringList variantListToStringList(const QVariantList &values)
{
    QStringList result;
    result.reserve(values.size());
    for (const QVariant &value : values) {
        result.append(value.toString());
    }
    return result;
}

// Applies a (subset of) Python/fmt numeric format spec like ".1f", ".2f", "d",
// ".0f" to a value. Non-numeric values, or an empty spec, are returned as-is.
QString applyFormatSpec(const QString &value, const QString &spec)
{
    if (spec.isEmpty()) {
        return value;
    }
    bool ok = false;
    const double number = value.toDouble(&ok);
    if (!ok) {
        return value;
    }

    QString body = spec;
    char type = 'f';
    if (!body.isEmpty() && body.back().isLetter()) {
        type = body.back().toLatin1();
        body.chop(1);
    }
    int precision = 6;
    if (body.startsWith(QLatin1Char('.'))) {
        precision = body.mid(1).toInt();
    }

    switch (type) {
    case 'f':
        return QString::number(number, 'f', precision);
    case 'e':
        return QString::number(number, 'e', precision);
    case 'g':
        return QString::number(number, 'g', precision > 0 ? precision : 6);
    case 'd':
        return QString::number(qRound(number));
    default:
        return QString::number(number, 'f', precision);
    }
}

// Substitutes "{text}", "{icon}", "{alt}", "{percentage}" and "{}" (alias for
// "{text}") in `format` with values from `values`, with optional fmt-style
// numeric specs ("{percentage:.1f}"). Unknown placeholders become "".
// "\{foo}" and "{ foo}" are left untouched, matching waybar's behavior.
QString applyFormat(const QString &format, const QMap<QString, QString> &values)
{
    static const QRegularExpression placeholder(QStringLiteral(R"((?<!\\)\{([A-Za-z]*)(?::([^}]*))?\})"));

    QString result;
    qsizetype lastEnd = 0;
    QRegularExpressionMatchIterator it = placeholder.globalMatch(format);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        result += format.mid(lastEnd, match.capturedStart() - lastEnd);
        QString key = match.captured(1);
        if (key.isEmpty()) {
            key = QStringLiteral("text");
        }
        result += applyFormatSpec(values.value(key), match.captured(2));
        lastEnd = match.capturedEnd();
    }
    result += format.mid(lastEnd);
    return result;
}

// Converts waybar/Pango "<span ...>...</span>" markup into Qt rich text
// "<font ...>...</font>" so attributes like color="..." keep working.
QString convertPangoMarkup(const QString &input)
{
    static const QRegularExpression openTag(QStringLiteral(R"(<span\b)"));
    static const QRegularExpression closeTag(QStringLiteral(R"(</span>)"));

    QString output = input;
    output.replace(openTag, QStringLiteral("<font"));
    output.replace(closeTag, QStringLiteral("</font>"));
    return output;
}

} // namespace

CustomToolModel::CustomToolModel(QObject *parent)
    : QObject(parent)
{
    m_timer.setSingleShot(false);
    connect(&m_timer, &QTimer::timeout, this, &CustomToolModel::refresh);
    connect(&m_process, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus exitStatus) {
        const QString output = QString::fromLocal8Bit(m_process.readAllStandardOutput());
        const bool success = exitStatus == QProcess::NormalExit && exitCode == 0;
        finishWithOutput(output, success);
    });
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        if (m_process.state() == QProcess::NotRunning) {
            finishWithOutput(QString(), false);
        }
    });
}

void CustomToolModel::setCommand(const QString &command)
{
    if (m_command == command) {
        return;
    }
    m_command = command;
    emit commandChanged();
}

void CustomToolModel::setArguments(const QVariantList &arguments)
{
    if (m_arguments == arguments) {
        return;
    }
    m_arguments = arguments;
    emit argumentsChanged();
}

void CustomToolModel::setWorkingDirectory(const QString &workingDirectory)
{
    if (m_workingDirectory == workingDirectory) {
        return;
    }
    m_workingDirectory = workingDirectory;
    emit workingDirectoryChanged();
}

void CustomToolModel::setIntervalMs(int intervalMs)
{
    const int normalized = std::max(0, intervalMs);
    if (m_intervalMs == normalized) {
        return;
    }
    m_intervalMs = normalized;
    emit intervalMsChanged();
    scheduleTimer();
}

void CustomToolModel::setWaybarFormat(bool waybarFormat)
{
    if (m_waybarFormat == waybarFormat) {
        return;
    }
    m_waybarFormat = waybarFormat;
    emit waybarFormatChanged();
}

void CustomToolModel::setFormat(const QString &format)
{
    if (m_format == format) {
        return;
    }
    m_format = format;
    emit formatChanged();
    recomputeDisplayText();
}

void CustomToolModel::setFormatIcons(const QVariantMap &formatIcons)
{
    if (m_formatIcons == formatIcons) {
        return;
    }
    m_formatIcons = formatIcons;
    emit formatIconsChanged();
    recomputeDisplayText();
}

void CustomToolModel::refresh()
{
    if (m_command.trimmed().isEmpty() || m_process.state() != QProcess::NotRunning) {
        return;
    }

    startProcess();
}

void CustomToolModel::runAction(const QString &command)
{
    if (command.trimmed().isEmpty()) {
        return;
    }
    // Detached so the click handler returns immediately (waybar runs these via sh).
    QProcess::startDetached(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), command});
}

void CustomToolModel::startProcess()
{
    QStringList parts = QProcess::splitCommand(m_command.trimmed());
    if (parts.isEmpty()) {
        setAvailable(false);
        return;
    }

    QString program = parts.takeFirst();
    QStringList args = parts;
    args.append(variantListToStringList(m_arguments));

    setLoading(true);

    if (!m_workingDirectory.trimmed().isEmpty()) {
        m_process.setWorkingDirectory(m_workingDirectory);
    }
    m_process.setProgram(program);
    m_process.setArguments(args);
    m_process.setProcessEnvironment(QProcessEnvironment::systemEnvironment());
    m_process.start();
    scheduleTimer();
}

void CustomToolModel::scheduleTimer()
{
    if (m_intervalMs > 0) {
        m_timer.start(m_intervalMs);
    } else {
        m_timer.stop();
    }
}

void CustomToolModel::recomputeDisplayText()
{
    QString icon = m_icon;
    if (m_formatIcons.contains(m_alt)) {
        icon = m_formatIcons.value(m_alt).toString();
    }

    QMap<QString, QString> values;
    values.insert(QStringLiteral("text"), m_text);
    values.insert(QStringLiteral("icon"), icon);
    values.insert(QStringLiteral("alt"), m_alt);
    values.insert(QStringLiteral("percentage"), QString::number(m_percentage));

    const QString display = convertPangoMarkup(applyFormat(m_format, values));
    if (m_displayText == display) {
        return;
    }
    m_displayText = display;
    emit displayTextChanged();
}

void CustomToolModel::finishWithOutput(const QString &output, bool success)
{
    setLoading(false);

    // A non-zero exit (or spawn failure) leaves the displayed value untouched:
    // a transient failing run should not blank out the last good reading.
    if (!success) {
        return;
    }

    setRawOutput(output);

    QString text = output.trimmed();
    QString tooltip = {};
    QString icon = {};
    QString alt = {};
    QString className = {};
    double percentage = 0.0;

    if (m_waybarFormat) {
        const QJsonDocument document = QJsonDocument::fromJson(output.toUtf8());
        if (document.isObject()) {
            const QJsonObject object = document.object();
            if (object.contains(QStringLiteral("text"))) {
                text = object.value(QStringLiteral("text")).toString(text);
            }
            tooltip = object.value(QStringLiteral("tooltip")).toString();
            icon = object.value(QStringLiteral("icon")).toString();
            alt = object.value(QStringLiteral("alt")).toString();
            className = object.value(QStringLiteral("class")).toString();
            percentage = object.value(QStringLiteral("percentage")).toDouble(percentage);
        }
    }

    setText(text);
    setTooltip(tooltip);
    setIcon(icon);
    setAlt(alt);
    setClassName(className);
    setPercentage(percentage);
    setAvailable(success && !text.isEmpty());
    recomputeDisplayText();
}

void CustomToolModel::setAvailable(bool available)
{
    if (m_available == available) {
        return;
    }
    m_available = available;
    emit availableChanged();
}

void CustomToolModel::setLoading(bool loading)
{
    if (m_loading == loading) {
        return;
    }
    m_loading = loading;
    emit loadingChanged();
}

void CustomToolModel::setText(const QString &text)
{
    if (m_text == text) {
        return;
    }
    m_text = text;
    emit textChanged();
}

void CustomToolModel::setTooltip(const QString &tooltip)
{
    if (m_tooltip == tooltip) {
        return;
    }
    m_tooltip = tooltip;
    emit tooltipChanged();
}

void CustomToolModel::setIcon(const QString &icon)
{
    if (m_icon == icon) {
        return;
    }
    m_icon = icon;
    emit iconChanged();
}

void CustomToolModel::setAlt(const QString &alt)
{
    if (m_alt == alt) {
        return;
    }
    m_alt = alt;
    emit altChanged();
}

void CustomToolModel::setClassName(const QString &className)
{
    if (m_className == className) {
        return;
    }
    m_className = className;
    emit classNameChanged();
}

void CustomToolModel::setPercentage(double percentage)
{
    if (qFuzzyCompare(m_percentage, percentage)) {
        return;
    }
    m_percentage = percentage;
    emit percentageChanged();
}

void CustomToolModel::setRawOutput(const QString &rawOutput)
{
    if (m_rawOutput == rawOutput) {
        return;
    }
    m_rawOutput = rawOutput;
    emit rawOutputChanged();
}
