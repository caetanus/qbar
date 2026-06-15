#include "bspwmbackend.h"

#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QSet>
#include <QTimer>

BspwmBackend::BspwmBackend(QObject *parent)
    : WindowManagerBackend(parent)
{
    connect(&m_subscribeProcess, &QProcess::readyReadStandardOutput, this, &BspwmBackend::readSubscriptionEvents);
    connect(&m_subscribeProcess, &QProcess::finished, this, &BspwmBackend::restartSubscription);
}

QString BspwmBackend::name() const
{
    return QStringLiteral("bspwm");
}

WorkspaceModel *BspwmBackend::workspaceModel()
{
    return &m_workspaceModel;
}

QString BspwmBackend::currentWindowTitle() const
{
    return m_currentWindowTitle;
}

QString BspwmBackend::currentKeyboardLayout() const
{
    return {};
}

qint64 BspwmBackend::focusedContainerId() const
{
    return -1;
}

bool BspwmBackend::isAvailable()
{
    if (QStandardPaths::findExecutable(QStringLiteral("bspc")).isEmpty()) {
        return false;
    }

    return !runBspc({QStringLiteral("--print-socket-path")}).trimmed().isEmpty();
}

QList<WorkspaceModel::Workspace> BspwmBackend::parseDesktopState(const QStringList &desktopNames,
                                                                 const QString &focusedDesktop,
                                                                 const QStringList &occupiedDesktops,
                                                                 const QStringList &urgentDesktops)
{
    QList<WorkspaceModel::Workspace> result;
    result.reserve(desktopNames.size());
    const QSet<QString> occupied(occupiedDesktops.begin(), occupiedDesktops.end());
    const QSet<QString> urgent(urgentDesktops.begin(), urgentDesktops.end());

    for (int i = 0; i < desktopNames.size(); ++i) {
        const QString name = desktopNames.at(i).trimmed();
        if (name.isEmpty()) {
            continue;
        }

        bool numberOk = false;
        const int numericName = name.toInt(&numberOk);
        WorkspaceModel::Workspace workspace;
        workspace.name = name;
        workspace.number = numberOk ? numericName : i + 1;
        workspace.focused = name == focusedDesktop;
        workspace.visible = workspace.focused || occupied.contains(name);
        workspace.urgent = urgent.contains(name);
        result.append(workspace);
    }

    return result;
}

QString BspwmBackend::parseFocusedNodeTitle(const QByteArray &nodeJson)
{
    const QJsonDocument document = QJsonDocument::fromJson(nodeJson);
    const QJsonObject root = document.object();
    const QJsonObject client = root.value(QStringLiteral("client")).toObject();
    const QString title = client.value(QStringLiteral("name")).toString();
    if (!title.isEmpty()) {
        return title;
    }

    const QString className = client.value(QStringLiteral("className")).toString();
    if (!className.isEmpty()) {
        return className;
    }

    return root.value(QStringLiteral("name")).toString();
}

void BspwmBackend::start()
{
    refreshWorkspaces();
    refreshActiveWindow();
    startSubscription();
}

void BspwmBackend::runCommand(const QString &command)
{
    if (command.isEmpty()) {
        return;
    }

    runBspc(command.split(QLatin1Char(' '), Qt::SkipEmptyParts));
}

void BspwmBackend::activateWorkspace(const QString &workspaceName)
{
    if (!workspaceName.isEmpty()) {
        runBspc({QStringLiteral("desktop"), QStringLiteral("-f"), workspaceName});
    }
}

void BspwmBackend::activateRelativeWorkspace(int direction)
{
    if (direction == 0) {
        return;
    }

    runBspc({QStringLiteral("desktop"), QStringLiteral("-f"), direction > 0 ? QStringLiteral("next.local") : QStringLiteral("prev.local")});
}

void BspwmBackend::cycleKeyboardLayout()
{
}

void BspwmBackend::requestTreeSnapshot()
{
    refreshWorkspaces();
    refreshActiveWindow();
}

void BspwmBackend::readSubscriptionEvents()
{
    const QStringList events = lines(m_subscribeProcess.readAllStandardOutput());
    if (events.isEmpty()) {
        return;
    }

    bool workspaceChanged = false;
    bool titleChanged = false;
    for (const QString &event : events) {
        workspaceChanged = workspaceChanged
            || event.startsWith(QStringLiteral("desktop_"))
            || event.startsWith(QStringLiteral("monitor_"))
            || event.startsWith(QStringLiteral("node_flag"));
        titleChanged = titleChanged
            || event.startsWith(QStringLiteral("node_focus"))
            || event.startsWith(QStringLiteral("node_title"));
    }

    if (workspaceChanged) {
        emit workspaceFocusEvent();
        refreshWorkspaces();
    }
    if (titleChanged) {
        refreshActiveWindow();
    }
}

void BspwmBackend::restartSubscription()
{
    QTimer::singleShot(2000, this, &BspwmBackend::startSubscription);
}

QStringList BspwmBackend::lines(const QByteArray &data)
{
    QString text = QString::fromUtf8(data);
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    return text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
}

QByteArray BspwmBackend::runBspc(const QStringList &arguments, int timeoutMs)
{
    QProcess process;
    process.start(QStringLiteral("bspc"), arguments);
    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished(100);
        return {};
    }

    return process.readAllStandardOutput();
}

void BspwmBackend::refreshWorkspaces()
{
    const QStringList desktops = lines(runBspc({QStringLiteral("query"), QStringLiteral("-D"), QStringLiteral("--names")}));
    const QString focused = QString::fromUtf8(runBspc({QStringLiteral("query"), QStringLiteral("-D"), QStringLiteral("-d"), QStringLiteral(".focused"), QStringLiteral("--names")})).trimmed();
    const QStringList occupied = lines(runBspc({QStringLiteral("query"), QStringLiteral("-D"), QStringLiteral("-d"), QStringLiteral(".occupied"), QStringLiteral("--names")}));
    const QStringList urgent = lines(runBspc({QStringLiteral("query"), QStringLiteral("-D"), QStringLiteral("-d"), QStringLiteral(".urgent"), QStringLiteral("--names")}));
    m_workspaceModel.replace(parseDesktopState(desktops, focused, occupied, urgent));
}

void BspwmBackend::refreshActiveWindow()
{
    setCurrentWindowTitle(parseFocusedNodeTitle(runBspc({QStringLiteral("query"), QStringLiteral("-N"), QStringLiteral("-n"), QStringLiteral("focused"), QStringLiteral("-T")})));
}

void BspwmBackend::startSubscription()
{
    if (m_subscribeProcess.state() != QProcess::NotRunning || QStandardPaths::findExecutable(QStringLiteral("bspc")).isEmpty()) {
        return;
    }

    m_subscribeProcess.start(QStringLiteral("bspc"), {
        QStringLiteral("subscribe"),
        QStringLiteral("desktop_focus"),
        QStringLiteral("desktop_add"),
        QStringLiteral("desktop_remove"),
        QStringLiteral("desktop_rename"),
        QStringLiteral("node_focus"),
        QStringLiteral("node_title"),
        QStringLiteral("node_flag"),
    });
}

void BspwmBackend::setCurrentWindowTitle(const QString &title)
{
    if (m_currentWindowTitle == title) {
        return;
    }

    m_currentWindowTitle = title;
    emit currentWindowTitleChanged();
}
