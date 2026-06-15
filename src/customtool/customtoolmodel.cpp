#include "customtoolmodel.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
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
}

void CustomToolModel::setFormatIcons(const QVariantMap &formatIcons)
{
    if (m_formatIcons == formatIcons) {
        return;
    }
    m_formatIcons = formatIcons;
    emit formatIconsChanged();
}

void CustomToolModel::refresh()
{
    if (m_command.trimmed().isEmpty() || m_process.state() != QProcess::NotRunning) {
        return;
    }

    startProcess();
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

void CustomToolModel::finishWithOutput(const QString &output, bool success)
{
    setLoading(false);
    setRawOutput(output);

    qDebug() << "[CustomTool] output:" << output << "success:" << success << "waybarFormat:" << m_waybarFormat;

    QString text = output.trimmed();
    QString tooltip = {};
    QString icon = {};
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
            className = object.value(QStringLiteral("class")).toString();
            percentage = object.value(QStringLiteral("percentage")).toDouble(percentage);
            qDebug() << "[CustomTool] parsed JSON - text:" << text << "icon:" << icon << "class:" << className;
        } else {
            qDebug() << "[CustomTool] JSON parse failed";
        }
    }

    setText(text);
    setTooltip(tooltip);
    setIcon(icon);
    setClassName(className);
    setPercentage(percentage);
    setAvailable(success && !text.isEmpty());
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
