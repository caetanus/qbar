#include "hyprlandbackend.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QSet>

namespace {

QString hyprlandInstanceDirectory()
{
    const auto environment = QProcessEnvironment::systemEnvironment();
    const QString runtimeDir = environment.value(QStringLiteral("XDG_RUNTIME_DIR"));
    const QString signature = environment.value(QStringLiteral("HYPRLAND_INSTANCE_SIGNATURE"));
    if (runtimeDir.isEmpty() || signature.isEmpty()) {
        return {};
    }

    return QDir(runtimeDir).filePath(QStringLiteral("hypr/%1").arg(signature));
}

int workspaceIdFromObject(const QJsonObject &object)
{
    return object.value(QStringLiteral("id")).toInt(object.value(QStringLiteral("num")).toInt(-1));
}

QString workspaceNameFromObject(const QJsonObject &object)
{
    const QString name = object.value(QStringLiteral("name")).toString();
    if (!name.isEmpty()) {
        return name;
    }

    const int id = workspaceIdFromObject(object);
    return id >= 0 ? QString::number(id) : QString();
}

QString firstNonEmpty(const QStringList &values)
{
    for (const QString &value : values) {
        if (!value.trimmed().isEmpty()) {
            return value.trimmed();
        }
    }
    return {};
}

} // namespace

HyprlandBackend::HyprlandBackend(QObject *parent)
    : WindowManagerBackend(parent)
{
    m_reconnectTimer.setInterval(2000);
    m_reconnectTimer.setSingleShot(true);

    connect(&m_reconnectTimer, &QTimer::timeout, this, &HyprlandBackend::reconnect);
    connect(&m_eventSocket, &QLocalSocket::readyRead, this, &HyprlandBackend::readEventMessages);
    connect(&m_eventSocket, &QLocalSocket::disconnected, this, &HyprlandBackend::handleEventDisconnected);
}

QString HyprlandBackend::name() const
{
    return QStringLiteral("hyprland");
}

WorkspaceModel *HyprlandBackend::workspaceModel()
{
    return &m_workspaceModel;
}

QString HyprlandBackend::currentWindowTitle() const
{
    return m_currentWindowTitle;
}

QString HyprlandBackend::currentKeyboardLayout() const
{
    return m_currentKeyboardLayout;
}

qint64 HyprlandBackend::focusedContainerId() const
{
    return m_focusedContainerId;
}

bool HyprlandBackend::isAvailable()
{
    return QFileInfo::exists(commandSocketPath()) && QFileInfo::exists(eventSocketPath());
}

QString HyprlandBackend::commandSocketPath()
{
    const QString directory = hyprlandInstanceDirectory();
    return directory.isEmpty() ? QString() : QDir(directory).filePath(QStringLiteral(".socket.sock"));
}

QString HyprlandBackend::eventSocketPath()
{
    const QString directory = hyprlandInstanceDirectory();
    return directory.isEmpty() ? QString() : QDir(directory).filePath(QStringLiteral(".socket2.sock"));
}

QList<WorkspaceModel::Workspace> HyprlandBackend::parseWorkspaces(const QByteArray &workspacesJson,
                                                                  const QByteArray &activeWorkspaceJson,
                                                                  const QByteArray &monitorsJson)
{
    const QJsonDocument workspacesDocument = QJsonDocument::fromJson(workspacesJson);
    const QJsonDocument activeWorkspaceDocument = QJsonDocument::fromJson(activeWorkspaceJson);
    const QJsonDocument monitorsDocument = QJsonDocument::fromJson(monitorsJson);

    const QJsonObject activeWorkspace = activeWorkspaceDocument.object();
    const int activeId = workspaceIdFromObject(activeWorkspace);
    const QString activeName = workspaceNameFromObject(activeWorkspace);

    QSet<int> visibleIds;
    QSet<QString> visibleNames;
    if (monitorsDocument.isArray()) {
        for (const auto &value : monitorsDocument.array()) {
            const QJsonObject monitor = value.toObject();
            const QJsonObject workspace = monitor.value(QStringLiteral("activeWorkspace")).toObject();
            const int id = workspaceIdFromObject(workspace);
            const QString name = workspaceNameFromObject(workspace);
            if (id >= 0) {
                visibleIds.insert(id);
            }
            if (!name.isEmpty()) {
                visibleNames.insert(name);
            }
        }
    }

    QList<WorkspaceModel::Workspace> result;
    if (!workspacesDocument.isArray()) {
        return result;
    }

    for (const auto &value : workspacesDocument.array()) {
        const QJsonObject object = value.toObject();
        WorkspaceModel::Workspace workspace;
        workspace.number = workspaceIdFromObject(object);
        workspace.name = workspaceNameFromObject(object);
        workspace.output = object.value(QStringLiteral("monitor")).toString();
        workspace.focused = (workspace.number >= 0 && workspace.number == activeId)
            || (!workspace.name.isEmpty() && workspace.name == activeName);
        workspace.visible = (workspace.number >= 0 && visibleIds.contains(workspace.number))
            || (!workspace.name.isEmpty() && visibleNames.contains(workspace.name));
        workspace.urgent = object.value(QStringLiteral("urgent")).toBool(false);
        result.append(workspace);
    }

    std::sort(result.begin(), result.end(), [](const auto &left, const auto &right) {
        if (left.number >= 0 && right.number >= 0 && left.number != right.number) {
            return left.number < right.number;
        }
        return left.name < right.name;
    });
    return result;
}

QString HyprlandBackend::parseActiveWindowTitle(const QByteArray &activeWindowJson)
{
    const QJsonDocument document = QJsonDocument::fromJson(activeWindowJson);
    if (!document.isObject()) {
        return {};
    }

    const QJsonObject object = document.object();
    return firstNonEmpty({
        object.value(QStringLiteral("title")).toString(),
        object.value(QStringLiteral("initialTitle")).toString(),
        object.value(QStringLiteral("class")).toString(),
        object.value(QStringLiteral("initialClass")).toString(),
    });
}

QString HyprlandBackend::parseActiveKeyboardLayout(const QByteArray &devicesJson)
{
    const QJsonDocument document = QJsonDocument::fromJson(devicesJson);
    if (!document.isObject()) {
        return {};
    }

    const QJsonArray keyboards = document.object().value(QStringLiteral("keyboards")).toArray();
    for (const auto &value : keyboards) {
        const QJsonObject keyboard = value.toObject();
        const QString layout = firstNonEmpty({
            keyboard.value(QStringLiteral("active_keymap")).toString(),
            keyboard.value(QStringLiteral("layout")).toString(),
        }).toLower();
        if (layout.contains(QStringLiteral("br")) || layout.contains(QStringLiteral("brazil"))) {
            return QStringLiteral("br");
        }
        if (layout.contains(QStringLiteral("us")) || layout.contains(QStringLiteral("english"))) {
            return QStringLiteral("us");
        }
        if (!layout.isEmpty()) {
            return layout.left(2);
        }
    }

    return {};
}

void HyprlandBackend::start()
{
    refreshWorkspaces();
    refreshActiveWindow();
    refreshKeyboardLayout();
    connectEventSocket();
}

void HyprlandBackend::runCommand(const QString &command)
{
    if (!command.isEmpty()) {
        request(command.toUtf8());
    }
}

void HyprlandBackend::activateWorkspace(const QString &workspaceName)
{
    if (!workspaceName.isEmpty()) {
        runCommand(QStringLiteral("dispatch workspace %1").arg(workspaceName));
    }
}

void HyprlandBackend::activateRelativeWorkspace(int direction)
{
    if (direction == 0) {
        return;
    }

    runCommand(direction > 0
                   ? QStringLiteral("dispatch workspace e+1")
                   : QStringLiteral("dispatch workspace e-1"));
}

void HyprlandBackend::cycleKeyboardLayout()
{
    runCommand(QStringLiteral("switchxkblayout all next"));
}

void HyprlandBackend::requestTreeSnapshot()
{
    refreshWorkspaces();
    refreshActiveWindow();
    refreshWindows();
}

void HyprlandBackend::reconnect()
{
    connectEventSocket();
}

void HyprlandBackend::readEventMessages()
{
    m_eventBuffer.append(m_eventSocket.readAll());
    qsizetype newline = -1;
    while ((newline = m_eventBuffer.indexOf('\n')) >= 0) {
        const QByteArray line = m_eventBuffer.left(newline).trimmed();
        m_eventBuffer.remove(0, newline + 1);
        if (!line.isEmpty()) {
            handleEventLine(line);
        }
    }
}

void HyprlandBackend::handleEventDisconnected()
{
    m_eventBuffer.clear();
    if (!m_reconnectTimer.isActive()) {
        m_reconnectTimer.start();
    }
}

QByteArray HyprlandBackend::request(const QByteArray &command)
{
    const QString path = commandSocketPath();
    if (path.isEmpty()) {
        return {};
    }

    QLocalSocket socket;
    socket.connectToServer(path);
    if (!socket.waitForConnected(250)) {
        return {};
    }

    socket.write(command);
    socket.flush();
    QByteArray response;
    while (socket.waitForReadyRead(250)) {
        response.append(socket.readAll());
    }
    socket.disconnectFromServer();
    return response;
}

void HyprlandBackend::connectEventSocket()
{
    const QString path = eventSocketPath();
    if (path.isEmpty()) {
        if (!m_reconnectTimer.isActive()) {
            m_reconnectTimer.start();
        }
        return;
    }

    if (m_eventSocket.state() == QLocalSocket::UnconnectedState) {
        m_eventSocket.connectToServer(path);
    }
}

void HyprlandBackend::refreshWorkspaces()
{
    const QByteArray workspaces = request(QByteArrayLiteral("j/workspaces"));
    const QByteArray activeWorkspace = request(QByteArrayLiteral("j/activeworkspace"));
    const QByteArray monitors = request(QByteArrayLiteral("j/monitors"));
    m_workspaceModel.replace(parseWorkspaces(workspaces, activeWorkspace, monitors));
}

void HyprlandBackend::refreshActiveWindow()
{
    setCurrentWindowTitle(parseActiveWindowTitle(request(QByteArrayLiteral("j/activewindow"))));
}

void HyprlandBackend::refreshWindows()
{
    const QByteArray clients = request(QByteArrayLiteral("j/clients"));
    const QByteArray monitors = request(QByteArrayLiteral("j/monitors"));
    m_windows.replace(parseClients(clients, monitors, m_focusedContainerId));
}

QList<WindowModel::Window> HyprlandBackend::parseClients(const QByteArray &clientsJson,
                                                        const QByteArray &monitorsJson,
                                                        qint64 focusedAddress)
{
    // Hyprland reports a client's monitor as a numeric id; map it to the monitor
    // name so the taskbar can filter by output.
    QHash<int, QString> monitorNames;
    const QJsonDocument monitorsDoc = QJsonDocument::fromJson(monitorsJson);
    for (const auto &value : monitorsDoc.array()) {
        const QJsonObject monitor = value.toObject();
        monitorNames.insert(monitor.value(QStringLiteral("id")).toInt(-1),
                            monitor.value(QStringLiteral("name")).toString());
    }

    QList<WindowModel::Window> windows;
    const QJsonDocument clientsDoc = QJsonDocument::fromJson(clientsJson);
    for (const auto &value : clientsDoc.array()) {
        const QJsonObject client = value.toObject();
        if (!client.value(QStringLiteral("mapped")).toBool(true)) {
            continue;
        }
        const QString address = client.value(QStringLiteral("address")).toString();
        bool ok = false;
        const qint64 id = address.startsWith(QStringLiteral("0x"))
            ? address.mid(2).toLongLong(&ok, 16)
            : address.toLongLong(&ok, 16);
        if (!ok || id == 0) {
            continue;
        }

        WindowModel::Window window;
        window.id = id;
        window.title = client.value(QStringLiteral("title")).toString();
        window.appId = client.value(QStringLiteral("class")).toString();
        window.workspaceName = client.value(QStringLiteral("workspace")).toObject()
                                   .value(QStringLiteral("name")).toString();
        window.monitor = monitorNames.value(client.value(QStringLiteral("monitor")).toInt(-1));
        window.focused = (id == focusedAddress);
        windows.append(window);
    }
    return windows;
}

void HyprlandBackend::activateWindow(qint64 id)
{
    if (id == 0) {
        return;
    }
    runCommand(QStringLiteral("dispatch focuswindow address:0x%1").arg(id, 0, 16));
}

void HyprlandBackend::closeWindow(qint64 id)
{
    if (id == 0) {
        return;
    }
    runCommand(QStringLiteral("dispatch closewindow address:0x%1").arg(id, 0, 16));
}

void HyprlandBackend::refreshKeyboardLayout()
{
    setCurrentKeyboardLayout(parseActiveKeyboardLayout(request(QByteArrayLiteral("j/devices"))));
}

void HyprlandBackend::handleEventLine(const QByteArray &line)
{
    const int separator = line.indexOf(">>");
    if (separator <= 0) {
        return;
    }

    const QByteArray event = line.left(separator);
    const QByteArray payload = line.mid(separator + 2);
    if (event == "workspace" || event == "workspacev2" || event == "focusedmon"
        || event == "focusedmonv2" || event == "createworkspace" || event == "createworkspacev2"
        || event == "destroyworkspace" || event == "destroyworkspacev2" || event == "moveworkspace"
        || event == "moveworkspacev2" || event == "renameworkspace" || event == "openwindow"
        || event == "closewindow" || event == "movewindow" || event == "movewindowv2") {
        emit workspaceFocusEvent();
        refreshWorkspaces();
        refreshWindows();
        return;
    }

    if (event == "activewindow") {
        const QString text = QString::fromUtf8(payload);
        const int comma = text.indexOf(QLatin1Char(','));
        setCurrentWindowTitle(comma >= 0 ? text.mid(comma + 1) : text);
        return;
    }

    if (event == "activewindowv2") {
        bool ok = false;
        const qint64 id = payload.toLongLong(&ok, 16);
        if (ok) {
            emit containerFocusEvent(id);
            setFocusedContainerId(id);
        }
        refreshActiveWindow();
        refreshWindows();
        return;
    }

    if (event == "windowtitle" || event == "windowtitlev2") {
        refreshActiveWindow();
        refreshWindows();
        return;
    }

    if (event == "activelayout") {
        const QString text = QString::fromUtf8(payload);
        const int comma = text.indexOf(QLatin1Char(','));
        setCurrentKeyboardLayout(comma >= 0 ? text.mid(comma + 1).left(2).toLower() : text.left(2).toLower());
    }
}

void HyprlandBackend::setCurrentWindowTitle(const QString &title)
{
    if (m_currentWindowTitle == title) {
        return;
    }

    m_currentWindowTitle = title;
    emit currentWindowTitleChanged();
}

void HyprlandBackend::setCurrentKeyboardLayout(const QString &layout)
{
    if (m_currentKeyboardLayout == layout) {
        return;
    }

    m_currentKeyboardLayout = layout;
    emit currentKeyboardLayoutChanged();
}

void HyprlandBackend::setFocusedContainerId(qint64 containerId)
{
    if (containerId < 0 || m_focusedContainerId == containerId) {
        return;
    }

    m_focusedContainerId = containerId;
    emit focusedContainerChanged(containerId);
}
