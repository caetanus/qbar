#include "i3ipcclient.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QDebug>
#include <xkbcommon/xkbregistry.h>

namespace {

constexpr auto magic = "i3-ipc";
constexpr int headerSize = 14;
constexpr quint32 eventBit = 1u << 31u;
constexpr quint32 workspaceEvent = eventBit | 0u;
constexpr quint32 modeEvent = eventBit | 2u;
constexpr quint32 windowEvent = eventBit | 3u;
constexpr quint32 inputEvent = eventBit | 21u;

void appendUint32(QByteArray *buffer, quint32 value)
{
    buffer->append(static_cast<char>(value & 0xff));
    buffer->append(static_cast<char>((value >> 8) & 0xff));
    buffer->append(static_cast<char>((value >> 16) & 0xff));
    buffer->append(static_cast<char>((value >> 24) & 0xff));
}

quint32 readUint32(const QByteArray &buffer, int offset)
{
    return static_cast<quint8>(buffer.at(offset))
        | (static_cast<quint8>(buffer.at(offset + 1)) << 8)
        | (static_cast<quint8>(buffer.at(offset + 2)) << 16)
        | (static_cast<quint8>(buffer.at(offset + 3)) << 24);
}

qint64 findQbarNode(const QJsonObject &node, qint64 pid)
{
    const QJsonObject properties = node.value(QStringLiteral("window_properties")).toObject();
    const qint64 nodePid = node.value(QStringLiteral("pid")).toInteger(-1);
    const bool matches = nodePid == pid
        || node.value(QStringLiteral("app_id")).toString(QStringLiteral("-")) == QStringLiteral("qbar")
        || node.value(QStringLiteral("name")).toString(QStringLiteral("-")) == QStringLiteral("QBar")
        || properties.value(QStringLiteral("title")).toString(QStringLiteral("-")) == QStringLiteral("QBar");
    if (matches) {
        return node.value(QStringLiteral("id")).toInteger(-1);
    }

    const auto nodes = node.value(QStringLiteral("nodes")).toArray();
    for (const auto &child : nodes) {
        const qint64 id = findQbarNode(child.toObject(), pid);
        if (id >= 0) {
            return id;
        }
    }

    const auto floatingNodes = node.value(QStringLiteral("floating_nodes")).toArray();
    for (const auto &child : floatingNodes) {
        const qint64 id = findQbarNode(child.toObject(), pid);
        if (id >= 0) {
            return id;
        }
    }

    return -1;
}

bool isWindowNode(const QJsonObject &node)
{
    return !node.value(QStringLiteral("window")).isNull()
        || !node.value(QStringLiteral("app_id")).toString().isEmpty()
        || node.value(QStringLiteral("pid")).toInteger(-1) > 0;
}

bool isQbarWindowNode(const QJsonObject &node)
{
    const QJsonObject properties = node.value(QStringLiteral("window_properties")).toObject();
    const QString appId = node.value(QStringLiteral("app_id")).toString();
    const QString name = node.value(QStringLiteral("name")).toString();
    const QString title = properties.value(QStringLiteral("title")).toString();
    return appId == QStringLiteral("qbar")
        || name == QStringLiteral("QBar")
        || title == QStringLiteral("QBar");
}

QString nodeTitle(const QJsonObject &node)
{
    const QString name = node.value(QStringLiteral("name")).toString();
    if (!name.isEmpty()) {
        return name;
    }

    const QJsonObject properties = node.value(QStringLiteral("window_properties")).toObject();
    return properties.value(QStringLiteral("title")).toString();
}

QString focusedWindowTitle(const QJsonObject &node)
{
    const auto nodes = node.value(QStringLiteral("nodes")).toArray();
    for (const auto &child : nodes) {
        const QString title = focusedWindowTitle(child.toObject());
        if (!title.isEmpty()) {
            return title;
        }
    }

    const auto floatingNodes = node.value(QStringLiteral("floating_nodes")).toArray();
    for (const auto &child : floatingNodes) {
        const QString title = focusedWindowTitle(child.toObject());
        if (!title.isEmpty()) {
            return title;
        }
    }

    if (!node.value(QStringLiteral("focused")).toBool(false) || !isWindowNode(node)) {
        return {};
    }

    const QString appId = node.value(QStringLiteral("app_id")).toString();
    const QString title = nodeTitle(node);
    if (appId == QStringLiteral("qbar") || title == QStringLiteral("QBar")) {
        return {};
    }

    return title;
}

qint64 focusedContainerNodeId(const QJsonObject &node)
{
    const auto nodes = node.value(QStringLiteral("nodes")).toArray();
    for (const auto &child : nodes) {
        const qint64 id = focusedContainerNodeId(child.toObject());
        if (id >= 0) {
            return id;
        }
    }

    const auto floatingNodes = node.value(QStringLiteral("floating_nodes")).toArray();
    for (const auto &child : floatingNodes) {
        const qint64 id = focusedContainerNodeId(child.toObject());
        if (id >= 0) {
            return id;
        }
    }

    if (!node.value(QStringLiteral("focused")).toBool(false) || !isWindowNode(node) || isQbarWindowNode(node)) {
        return -1;
    }

    return node.value(QStringLiteral("id")).toInteger(-1);
}

qint64 containerId(const QJsonObject &node)
{
    if (!isWindowNode(node) || isQbarWindowNode(node)) {
        return -1;
    }

    return node.value(QStringLiteral("id")).toInteger(-1);
}

// Walk the i3/sway tree collecting leaf windows for the taskbar, threading the
// enclosing output (monitor) and workspace name down the recursion.
void collectWindows(const QJsonObject &node, const QString &output, const QString &workspace,
                    QList<WindowModel::Window> &out)
{
    QString currentOutput = output;
    QString currentWorkspace = workspace;
    const QString type = node.value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("output")) {
        currentOutput = node.value(QStringLiteral("name")).toString();
    } else if (type == QStringLiteral("workspace")) {
        currentWorkspace = node.value(QStringLiteral("name")).toString();
    }

    if (isWindowNode(node) && !isQbarWindowNode(node)) {
        WindowModel::Window window;
        window.id = node.value(QStringLiteral("id")).toInteger(-1);
        window.title = nodeTitle(node);
        window.appId = node.value(QStringLiteral("app_id")).toString();
        if (window.appId.isEmpty()) {
            // X11 clients under sway expose WM_CLASS instead of app_id.
            window.appId = node.value(QStringLiteral("window_properties")).toObject()
                               .value(QStringLiteral("class")).toString();
        }
        window.workspaceName = currentWorkspace;
        window.monitor = currentOutput;
        window.focused = node.value(QStringLiteral("focused")).toBool(false);
        window.urgent = node.value(QStringLiteral("urgent")).toBool(false);
        out.append(window);
    }

    const auto nodes = node.value(QStringLiteral("nodes")).toArray();
    for (const auto &child : nodes) {
        collectWindows(child.toObject(), currentOutput, currentWorkspace, out);
    }
    const auto floatingNodes = node.value(QStringLiteral("floating_nodes")).toArray();
    for (const auto &child : floatingNodes) {
        collectWindows(child.toObject(), currentOutput, currentWorkspace, out);
    }
}

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

QString activeKeyboardLayout(const QJsonArray &inputs)
{
    for (const auto &value : inputs) {
        const QJsonObject input = value.toObject();
        if (input.value(QStringLiteral("type")).toString() != QStringLiteral("keyboard")) {
            continue;
        }

        const int activeIndex = input.value(QStringLiteral("xkb_active_layout_index")).toInt(-1);
        const QJsonArray layoutNames = input.value(QStringLiteral("xkb_layout_names")).toArray();
        if (activeIndex >= 0 && activeIndex < layoutNames.size()) {
            const QString layoutName = layoutNames.at(activeIndex).toString();
            const QString layoutCode = registryLayoutCode(layoutName);
            if (!layoutCode.isEmpty()) {
                return layoutCode;
            }
            const QString normalizedCode = normalizedLayoutCode(layoutName);
            if (!normalizedCode.isEmpty()) {
                return normalizedCode;
            }
        }

        const QString activeName = input.value(QStringLiteral("xkb_active_layout_name")).toString();
        const QString registryCode = registryLayoutCode(activeName);
        if (!registryCode.isEmpty()) {
            return registryCode;
        }
        const QString layout = normalizedLayoutCode(activeName);
        if (!layout.isEmpty()) {
            return layout;
        }
    }

    return {};
}

} // namespace

I3IpcClient::I3IpcClient(QObject *parent)
    : WindowManagerBackend(parent)
{
    m_reconnectTimer.setInterval(2000);
    m_reconnectTimer.setSingleShot(true);

    connect(&m_reconnectTimer, SIGNAL(timeout()), this, SLOT(reconnect()));
    connect(&m_commandSocket, SIGNAL(readyRead()), this, SLOT(readCommandMessages()));
    connect(&m_eventSocket, SIGNAL(readyRead()), this, SLOT(readEventMessages()));
    connect(&m_commandSocket, SIGNAL(disconnected()), this, SLOT(handleCommandDisconnected()));
    connect(&m_eventSocket, SIGNAL(disconnected()), this, SLOT(handleEventDisconnected()));
    connect(&m_commandSocket, SIGNAL(connected()), this, SLOT(requestWorkspaces()));
    connect(&m_commandSocket, SIGNAL(connected()), this, SLOT(flushPendingCommands()));
    connect(&m_eventSocket, SIGNAL(connected()), this, SLOT(subscribeWorkspaceEvents()));
}

QString I3IpcClient::name() const
{
    return supportsSwayInputs() ? QStringLiteral("sway") : QStringLiteral("i3");
}

WorkspaceModel *I3IpcClient::workspaceModel()
{
    return &m_workspaceModel;
}

QString I3IpcClient::currentWindowTitle() const
{
    return m_currentWindowTitle;
}

QString I3IpcClient::currentKeyboardLayout() const
{
    return m_currentKeyboardLayout;
}

qint64 I3IpcClient::focusedContainerId() const
{
    return m_focusedContainerId;
}

QString I3IpcClient::bindingMode() const
{
    return m_bindingMode;
}

void I3IpcClient::start()
{
    connectSockets();
}

void I3IpcClient::runCommand(const QString &command)
{
    if (command.isEmpty()) {
        return;
    }

    m_pendingCommands.append(command);
    if (m_commandSocket.state() == QLocalSocket::ConnectedState) {
        flushPendingCommands();
    } else {
        connectSockets();
    }
}

void I3IpcClient::activateWorkspace(const QString &workspaceName)
{
    if (workspaceName.isEmpty()) {
        return;
    }

    QString escapedName = workspaceName;
    escapedName.replace(QStringLiteral("\\"), QStringLiteral("\\\\"));
    escapedName.replace(QStringLiteral("\""), QStringLiteral("\\\""));
    runCommand(QStringLiteral("workspace \"%1\"").arg(escapedName));
}

void I3IpcClient::activateRelativeWorkspace(int direction)
{
    if (direction == 0) {
        return;
    }

    runCommand(direction > 0
                   ? QStringLiteral("workspace next_on_output")
                   : QStringLiteral("workspace prev_on_output"));
}

void I3IpcClient::activateWindowByPid(qint64 pid)
{
    if (pid <= 0) {
        return;
    }
    // The pid criterion matches the window owned by that process (switching to
    // its workspace). Windowless/headless players (mpd, mpDris2) match nothing,
    // so this is a harmless no-op for them.
    runCommand(QStringLiteral("[pid=%1] focus").arg(pid));
}

void I3IpcClient::activateWindow(qint64 id)
{
    if (id < 0) {
        return;
    }
    runCommand(QStringLiteral("[con_id=%1] focus").arg(id));
}

void I3IpcClient::closeWindow(qint64 id)
{
    if (id < 0) {
        return;
    }
    runCommand(QStringLiteral("[con_id=%1] kill").arg(id));
}

void I3IpcClient::cycleKeyboardLayout()
{
    if (supportsSwayInputs()) {
        runCommand(QStringLiteral("input type:keyboard xkb_switch_layout next"));
    }
}

void I3IpcClient::reconnect()
{
    connectSockets();
}

void I3IpcClient::readCommandMessages()
{
    consumeMessages(&m_commandSocket, &m_commandBuffer, false);
}

void I3IpcClient::readEventMessages()
{
    consumeMessages(&m_eventSocket, &m_eventBuffer, true);
}

void I3IpcClient::handleCommandDisconnected()
{
    m_commandBuffer.clear();
    if (!m_reconnectTimer.isActive()) {
        m_reconnectTimer.start();
    }
}

void I3IpcClient::handleEventDisconnected()
{
    m_eventBuffer.clear();
    if (!m_reconnectTimer.isActive()) {
        m_reconnectTimer.start();
    }
}

void I3IpcClient::connectSockets()
{
    const QString path = socketPath();
    if (path.isEmpty()) {
        if (!m_reconnectTimer.isActive()) {
            m_reconnectTimer.start();
        }
        return;
    }

    if (m_commandSocket.state() == QLocalSocket::UnconnectedState) {
        m_commandSocket.connectToServer(path);
        if (m_commandSocket.waitForConnected(100)) {
            requestWorkspaces();
            requestInputs();
            requestTreeSnapshot();
            flushPendingCommands();
        }
    }

    if (m_eventSocket.state() == QLocalSocket::UnconnectedState) {
        m_eventSocket.connectToServer(path);
    }
}

void I3IpcClient::requestWorkspaces()
{
    sendMessage(&m_commandSocket, GetWorkspaces);
}

void I3IpcClient::requestInputs()
{
    if (supportsSwayInputs()) {
        sendMessage(&m_commandSocket, GetInputs);
    }
}

void I3IpcClient::subscribeWorkspaceEvents()
{
    sendMessage(&m_eventSocket, Subscribe, supportsSwayInputs()
                    ? QByteArrayLiteral("[\"workspace\",\"window\",\"mode\",\"input\"]")
                    : QByteArrayLiteral("[\"workspace\",\"window\",\"mode\"]"));
}

void I3IpcClient::sendMessage(QLocalSocket *socket, MessageType type, const QByteArray &payload)
{
    if (socket->state() != QLocalSocket::ConnectedState) {
        return;
    }

    QByteArray message;
    message.append(magic, 6);
    appendUint32(&message, static_cast<quint32>(payload.size()));
    appendUint32(&message, static_cast<quint32>(type));
    message.append(payload);
    socket->write(message);
    socket->flush();
}

void I3IpcClient::flushPendingCommands()
{
    while (!m_pendingCommands.isEmpty() && m_commandSocket.state() == QLocalSocket::ConnectedState) {
        const QString command = m_pendingCommands.takeFirst();
        sendMessage(&m_commandSocket, RunCommand, command.toUtf8());
    }
}

void I3IpcClient::requestTreeSnapshot()
{
    if (m_commandSocket.state() == QLocalSocket::ConnectedState) {
        sendMessage(&m_commandSocket, GetTree);
    }
}

void I3IpcClient::consumeMessages(QLocalSocket *socket, QByteArray *buffer, bool eventStream)
{
    buffer->append(socket->readAll());

    while (buffer->size() >= headerSize) {
        if (buffer->left(6) != QByteArray(magic, 6)) {
            buffer->clear();
            return;
        }

        const quint32 length = readUint32(*buffer, 6);
        const quint32 type = readUint32(*buffer, 10);
        if (buffer->size() < headerSize + static_cast<int>(length)) {
            return;
        }

        const QByteArray payload = buffer->mid(headerSize, length);
        buffer->remove(0, headerSize + static_cast<int>(length));
        handleMessage(type, payload, eventStream);
    }
}

void I3IpcClient::handleMessage(quint32 type, const QByteArray &payload, bool eventStream)
{
    if (eventStream) {
        const auto document = QJsonDocument::fromJson(payload);
        const QJsonObject event = document.object();
        const QString change = event.value(QStringLiteral("change")).toString();

        if (type == workspaceEvent) {
            if (change == QStringLiteral("focus")) {
                emit workspaceFocusEvent();
            }
            requestWorkspaces();
            requestTreeSnapshot();
        } else if (type == modeEvent) {
            setBindingMode(change);
        } else if (type == windowEvent) {
            const auto container = event.value(QStringLiteral("container")).toObject();
            if (change == QStringLiteral("focus")) {
                const qint64 id = containerId(container);
                if (id >= 0) {
                    emit containerFocusEvent(id);
                    setFocusedContainerId(id);
                }
            }

            const QString title = nodeTitle(container);
            if (!title.isEmpty()) {
                setCurrentWindowTitle(title);
            }
            // Any window event (new/close/move/title/focus/urgent) can change the
            // taskbar list, so refresh the tree snapshot to repopulate it.
            requestTreeSnapshot();
        } else if (type == inputEvent) {
            requestInputs();
        }
        return;
    }

    if (type == GetTree) {
        const auto document = QJsonDocument::fromJson(payload);
        if (document.isObject()) {
            const QJsonObject tree = document.object();
            setCurrentWindowTitle(focusedWindowTitle(tree));
            setFocusedContainerId(focusedContainerNodeId(tree));
            QList<WindowModel::Window> windows;
            collectWindows(tree, {}, {}, windows);
            m_windows.replace(std::move(windows));
            const qint64 id = findQbarNode(tree, QCoreApplication::applicationPid());
            if (id >= 0) {
                qDebug() << "QBar sway node id:" << id;
                emit qbarNodeFound(id);
            } else {
                qDebug() << "QBar sway node was not found in GET_TREE";
            }
        }
        return;
    }

    if (type == GetInputs) {
        const auto document = QJsonDocument::fromJson(payload);
        if (document.isArray()) {
            setCurrentKeyboardLayout(activeKeyboardLayout(document.array()));
        }
        return;
    }

    if (type != GetWorkspaces) {
        if (type == RunCommand) {
            const auto document = QJsonDocument::fromJson(payload);
            if (document.isArray()) {
                for (const auto &value : document.array()) {
                    const auto object = value.toObject();
                    if (!object.value(QStringLiteral("success")).toBool(true)) {
                        qWarning() << "i3/sway IPC command failed:" << object;
                        requestTreeSnapshot();
                    }
                }
            }
        }
        return;
    }

    const auto document = QJsonDocument::fromJson(payload);
    if (document.isArray()) {
        m_workspaceModel.replaceFromI3Json(document.array());
    }
}

void I3IpcClient::setCurrentWindowTitle(const QString &title)
{
    if (m_currentWindowTitle == title) {
        return;
    }

    m_currentWindowTitle = title;
    emit currentWindowTitleChanged();
}

void I3IpcClient::setCurrentKeyboardLayout(const QString &layout)
{
    if (m_currentKeyboardLayout == layout) {
        return;
    }

    m_currentKeyboardLayout = layout;
    emit currentKeyboardLayoutChanged();
}

void I3IpcClient::setFocusedContainerId(qint64 containerId)
{
    if (containerId < 0 || m_focusedContainerId == containerId) {
        return;
    }

    m_focusedContainerId = containerId;
    emit focusedContainerChanged(containerId);
}

void I3IpcClient::setBindingMode(const QString &mode)
{
    const QString newMode = mode.isEmpty() ? QStringLiteral("default") : mode;
    if (m_bindingMode == newMode) {
        return;
    }

    m_bindingMode = newMode;
    emit bindingModeChanged();
}

bool I3IpcClient::supportsSwayInputs() const
{
    const auto environment = QProcessEnvironment::systemEnvironment();
    const QString swaySocket = environment.value(QStringLiteral("SWAYSOCK"));
    return !swaySocket.isEmpty() && QFileInfo::exists(swaySocket);
}

QString I3IpcClient::socketPath() const
{
    const auto environment = QProcessEnvironment::systemEnvironment();
    const QString swaySocket = environment.value(QStringLiteral("SWAYSOCK"));
    if (!swaySocket.isEmpty() && QFileInfo::exists(swaySocket)) {
        return swaySocket;
    }

    const QString i3Socket = environment.value(QStringLiteral("I3SOCK"));
    if (!i3Socket.isEmpty() && QFileInfo::exists(i3Socket)) {
        return i3Socket;
    }

    return {};
}
