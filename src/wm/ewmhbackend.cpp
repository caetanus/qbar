#include "ewmhbackend.h"

#include <QGuiApplication>

#ifdef QBAR_HAVE_X11
#include <QByteArray>
#include <QDebug>
#include <QScopeGuard>
#include <xcb/xcb.h>
#endif

namespace {

#ifdef QBAR_HAVE_X11

xcb_atom_t internAtom(xcb_connection_t *connection, const QByteArray &name)
{
    xcb_intern_atom_cookie_t cookie = xcb_intern_atom(connection, false, name.size(), name.constData());
    xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply(connection, cookie, nullptr);
    if (reply == nullptr) {
        return XCB_ATOM_NONE;
    }

    const auto guard = qScopeGuard([reply]() {
        free(reply);
    });
    return reply->atom;
}

QByteArray propertyBytes(xcb_connection_t *connection, xcb_window_t window, xcb_atom_t property, xcb_atom_t type)
{
    xcb_get_property_cookie_t cookie = xcb_get_property(connection, false, window, property, type, 0, 4096);
    xcb_get_property_reply_t *reply = xcb_get_property_reply(connection, cookie, nullptr);
    if (reply == nullptr) {
        return {};
    }

    const auto guard = qScopeGuard([reply]() {
        free(reply);
    });
    const int length = xcb_get_property_value_length(reply);
    if (length <= 0) {
        return {};
    }

    return QByteArray(static_cast<const char *>(xcb_get_property_value(reply)), length);
}

quint32 cardinalProperty(xcb_connection_t *connection, xcb_window_t window, xcb_atom_t property, quint32 fallback = 0)
{
    const QByteArray bytes = propertyBytes(connection, window, property, XCB_ATOM_CARDINAL);
    if (bytes.size() < static_cast<int>(sizeof(quint32))) {
        return fallback;
    }

    return *reinterpret_cast<const quint32 *>(bytes.constData());
}

QStringList desktopNames(xcb_connection_t *connection, xcb_window_t root, xcb_atom_t desktopNamesAtom, int count)
{
    const xcb_atom_t utf8String = internAtom(connection, QByteArrayLiteral("UTF8_STRING"));
    const QByteArray bytes = propertyBytes(connection, root, desktopNamesAtom, utf8String);
    QStringList names;
    int start = 0;
    for (int i = 0; i <= bytes.size(); ++i) {
        if (i == bytes.size() || bytes.at(i) == '\0') {
            names.append(QString::fromUtf8(bytes.constData() + start, i - start));
            start = i + 1;
        }
    }

    while (names.size() < count) {
        names.append(QString::number(names.size() + 1));
    }
    return names.mid(0, count);
}

QString windowTitle(xcb_connection_t *connection, xcb_window_t window)
{
    if (window == XCB_WINDOW_NONE) {
        return {};
    }

    const xcb_atom_t netWmName = internAtom(connection, QByteArrayLiteral("_NET_WM_NAME"));
    const xcb_atom_t utf8String = internAtom(connection, QByteArrayLiteral("UTF8_STRING"));
    QString title = QString::fromUtf8(propertyBytes(connection, window, netWmName, utf8String));
    if (!title.isEmpty()) {
        return title;
    }

    title = QString::fromUtf8(propertyBytes(connection, window, XCB_ATOM_WM_NAME, XCB_ATOM_STRING));
    return title;
}

QList<xcb_window_t> windowList(xcb_connection_t *connection, xcb_window_t root, xcb_atom_t clientListAtom)
{
    const QByteArray bytes = propertyBytes(connection, root, clientListAtom, XCB_ATOM_WINDOW);
    QList<xcb_window_t> windows;
    const int count = bytes.size() / static_cast<int>(sizeof(xcb_window_t));
    const auto *data = reinterpret_cast<const xcb_window_t *>(bytes.constData());
    windows.reserve(count);
    for (int i = 0; i < count; ++i) {
        windows.append(data[i]);
    }
    return windows;
}

// WM_CLASS is "instance\0class\0"; use the class (second field) as the app id,
// falling back to the instance.
QString windowClass(xcb_connection_t *connection, xcb_window_t window)
{
    const QByteArray bytes = propertyBytes(connection, window, XCB_ATOM_WM_CLASS, XCB_ATOM_STRING);
    if (bytes.isEmpty()) {
        return {};
    }
    const int nul = bytes.indexOf('\0');
    if (nul < 0) {
        return QString::fromUtf8(bytes);
    }
    const QByteArray klass = bytes.mid(nul + 1);
    const int end = klass.indexOf('\0');
    const QByteArray value = end < 0 ? klass : klass.left(end);
    return QString::fromUtf8(value.isEmpty() ? bytes.left(nul) : value);
}

bool windowDemandsAttention(xcb_connection_t *connection, xcb_window_t window, xcb_atom_t demandsAttentionAtom)
{
    const xcb_atom_t stateAtom = internAtom(connection, QByteArrayLiteral("_NET_WM_STATE"));
    const QByteArray bytes = propertyBytes(connection, window, stateAtom, XCB_ATOM_ATOM);
    const int count = bytes.size() / static_cast<int>(sizeof(xcb_atom_t));
    const auto *data = reinterpret_cast<const xcb_atom_t *>(bytes.constData());
    for (int i = 0; i < count; ++i) {
        if (data[i] == demandsAttentionAtom) {
            return true;
        }
    }
    return false;
}

void sendWindowClientMessage(xcb_connection_t *connection, xcb_window_t root, xcb_window_t window,
                             xcb_atom_t messageType, quint32 firstData)
{
    xcb_client_message_event_t event {};
    event.response_type = XCB_CLIENT_MESSAGE;
    event.format = 32;
    event.window = window;
    event.type = messageType;
    event.data.data32[0] = firstData;
    event.data.data32[1] = XCB_CURRENT_TIME;
    xcb_send_event(connection, false, root,
                   XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY,
                   reinterpret_cast<const char *>(&event));
    xcb_flush(connection);
}

#endif

} // namespace

EwmhBackend::EwmhBackend(QObject *parent)
    : WindowManagerBackend(parent)
{
}

QString EwmhBackend::name() const
{
    return QStringLiteral("ewmh");
}

WorkspaceModel *EwmhBackend::workspaceModel()
{
    return &m_workspaceModel;
}

QString EwmhBackend::currentWindowTitle() const
{
    return m_currentWindowTitle;
}

QString EwmhBackend::currentKeyboardLayout() const
{
    return {};
}

qint64 EwmhBackend::focusedContainerId() const
{
    return -1;
}

bool EwmhBackend::isAvailable()
{
#ifdef QBAR_HAVE_X11
    return QGuiApplication::platformName().contains(QStringLiteral("xcb"));
#else
    return false;
#endif
}

void EwmhBackend::start()
{
    requestTreeSnapshot();
}

void EwmhBackend::runCommand(const QString &command)
{
    Q_UNUSED(command)
}

void EwmhBackend::activateWorkspace(const QString &workspaceName)
{
#ifdef QBAR_HAVE_X11
    bool ok = false;
    const quint32 desktop = static_cast<quint32>(workspaceName.toUInt(&ok));
    if (!ok || desktop == 0) {
        return;
    }

    xcb_connection_t *connection = xcb_connect(nullptr, nullptr);
    if (connection == nullptr || xcb_connection_has_error(connection)) {
        return;
    }
    const auto guard = qScopeGuard([connection]() {
        xcb_disconnect(connection);
    });

    const xcb_setup_t *setup = xcb_get_setup(connection);
    xcb_screen_iterator_t iterator = xcb_setup_roots_iterator(setup);
    if (iterator.rem == 0) {
        return;
    }

    const xcb_window_t root = iterator.data->root;
    const xcb_atom_t currentDesktop = internAtom(connection, QByteArrayLiteral("_NET_CURRENT_DESKTOP"));
    xcb_client_message_event_t event {};
    event.response_type = XCB_CLIENT_MESSAGE;
    event.format = 32;
    event.window = root;
    event.type = currentDesktop;
    event.data.data32[0] = desktop - 1;
    event.data.data32[1] = XCB_CURRENT_TIME;

    xcb_send_event(connection, false, root, XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY, reinterpret_cast<const char *>(&event));
    xcb_flush(connection);
#else
    Q_UNUSED(workspaceName)
#endif
}

void EwmhBackend::activateRelativeWorkspace(int direction)
{
    if (direction == 0 || m_workspaceModel.rowCount() == 0) {
        return;
    }

    const auto workspaces = m_workspaceModel.workspaces();
    int focused = -1;
    for (int i = 0; i < workspaces.size(); ++i) {
        if (workspaces.at(i).focused) {
            focused = i;
            break;
        }
    }
    if (focused < 0) {
        return;
    }

    const int next = (focused + (direction > 0 ? 1 : -1) + workspaces.size()) % workspaces.size();
    activateWorkspace(QString::number(next + 1));
}

void EwmhBackend::cycleKeyboardLayout()
{
}

void EwmhBackend::requestTreeSnapshot()
{
#ifdef QBAR_HAVE_X11
    xcb_connection_t *connection = xcb_connect(nullptr, nullptr);
    if (connection == nullptr || xcb_connection_has_error(connection)) {
        return;
    }
    const auto guard = qScopeGuard([connection]() {
        xcb_disconnect(connection);
    });

    const xcb_setup_t *setup = xcb_get_setup(connection);
    xcb_screen_iterator_t iterator = xcb_setup_roots_iterator(setup);
    if (iterator.rem == 0) {
        return;
    }

    const xcb_window_t root = iterator.data->root;
    const xcb_atom_t numberOfDesktopsAtom = internAtom(connection, QByteArrayLiteral("_NET_NUMBER_OF_DESKTOPS"));
    const xcb_atom_t currentDesktopAtom = internAtom(connection, QByteArrayLiteral("_NET_CURRENT_DESKTOP"));
    const xcb_atom_t desktopNamesAtom = internAtom(connection, QByteArrayLiteral("_NET_DESKTOP_NAMES"));
    const xcb_atom_t activeWindowAtom = internAtom(connection, QByteArrayLiteral("_NET_ACTIVE_WINDOW"));

    const int count = static_cast<int>(cardinalProperty(connection, root, numberOfDesktopsAtom, 0));
    const int current = static_cast<int>(cardinalProperty(connection, root, currentDesktopAtom, 0));
    const QStringList names = desktopNames(connection, root, desktopNamesAtom, count);

    QList<WorkspaceModel::Workspace> workspaces;
    for (int i = 0; i < names.size(); ++i) {
        WorkspaceModel::Workspace workspace;
        workspace.name = names.at(i);
        workspace.number = i + 1;
        workspace.focused = i == current;
        workspace.visible = workspace.focused;
        workspaces.append(workspace);
    }
    m_workspaceModel.replace(std::move(workspaces));

    const quint32 activeWindow = cardinalProperty(connection, root, activeWindowAtom, XCB_WINDOW_NONE);
    const QString title = windowTitle(connection, activeWindow);
    if (m_currentWindowTitle != title) {
        m_currentWindowTitle = title;
        emit currentWindowTitleChanged();
    }

    // Window list for the taskbar from _NET_CLIENT_LIST.
    const xcb_atom_t clientListAtom = internAtom(connection, QByteArrayLiteral("_NET_CLIENT_LIST"));
    const xcb_atom_t wmDesktopAtom = internAtom(connection, QByteArrayLiteral("_NET_WM_DESKTOP"));
    const xcb_atom_t demandsAttentionAtom = internAtom(connection, QByteArrayLiteral("_NET_WM_STATE_DEMANDS_ATTENTION"));
    QList<WindowModel::Window> windows;
    for (const xcb_window_t window : windowList(connection, root, clientListAtom)) {
        WindowModel::Window entry;
        entry.id = static_cast<qint64>(window);
        entry.title = windowTitle(connection, window);
        entry.appId = windowClass(connection, window);
        const quint32 desktop = cardinalProperty(connection, window, wmDesktopAtom, 0xffffffff);
        entry.workspaceName = (desktop != 0xffffffff && static_cast<int>(desktop) < names.size())
            ? names.at(static_cast<int>(desktop))
            : QString();
        entry.focused = (window == activeWindow);
        entry.urgent = windowDemandsAttention(connection, window, demandsAttentionAtom);
        windows.append(entry);
    }
    m_windows.replace(std::move(windows));
#endif
}

void EwmhBackend::activateWindow(qint64 id)
{
#ifdef QBAR_HAVE_X11
    if (id <= 0) {
        return;
    }
    xcb_connection_t *connection = xcb_connect(nullptr, nullptr);
    if (connection == nullptr || xcb_connection_has_error(connection)) {
        return;
    }
    const auto guard = qScopeGuard([connection]() { xcb_disconnect(connection); });
    const xcb_setup_t *setup = xcb_get_setup(connection);
    xcb_screen_iterator_t iterator = xcb_setup_roots_iterator(setup);
    if (iterator.rem == 0) {
        return;
    }
    const xcb_window_t root = iterator.data->root;
    const xcb_atom_t activeWindowAtom = internAtom(connection, QByteArrayLiteral("_NET_ACTIVE_WINDOW"));
    // Source indication 2 = pager (per EWMH), which WMs honour without focus-steal
    // prevention getting in the way.
    sendWindowClientMessage(connection, root, static_cast<xcb_window_t>(id), activeWindowAtom, 2);
#else
    Q_UNUSED(id)
#endif
}

void EwmhBackend::closeWindow(qint64 id)
{
#ifdef QBAR_HAVE_X11
    if (id <= 0) {
        return;
    }
    xcb_connection_t *connection = xcb_connect(nullptr, nullptr);
    if (connection == nullptr || xcb_connection_has_error(connection)) {
        return;
    }
    const auto guard = qScopeGuard([connection]() { xcb_disconnect(connection); });
    const xcb_setup_t *setup = xcb_get_setup(connection);
    xcb_screen_iterator_t iterator = xcb_setup_roots_iterator(setup);
    if (iterator.rem == 0) {
        return;
    }
    const xcb_window_t root = iterator.data->root;
    const xcb_atom_t closeWindowAtom = internAtom(connection, QByteArrayLiteral("_NET_CLOSE_WINDOW"));
    sendWindowClientMessage(connection, root, static_cast<xcb_window_t>(id), closeWindowAtom, 2);
#else
    Q_UNUSED(id)
#endif
}
