#include "x11lockbackend.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QGuiApplication>

#include <cstdlib>

#ifdef QBAR_LOCK_HAVE_X11
#include <qguiapplication_platform.h>
#include <xcb/xcb.h>
#endif

X11LockBackend::X11LockBackend(QObject *parent)
    : LockBackend(parent)
{
#ifdef QBAR_LOCK_HAVE_X11
    auto *native = qGuiApp != nullptr
        ? qGuiApp->nativeInterface<QNativeInterface::QX11Application>()
        : nullptr;
    m_connection = native != nullptr ? native->connection() : nullptr;
    if (m_connection != nullptr) {
        auto *connection = static_cast<xcb_connection_t *>(m_connection);
        xcb_font_t cursorFont = xcb_generate_id(connection);
        xcb_open_font(connection, cursorFont, 6, "cursor");
        m_cursor = xcb_generate_id(connection);
        xcb_create_glyph_cursor(
            connection,
            static_cast<xcb_cursor_t>(m_cursor),
            cursorFont,
            cursorFont,
            68,
            69,
            0,
            0,
            0,
            0xffff,
            0xffff,
            0xffff);
        xcb_close_font(connection, cursorFont);
        xcb_flush(connection);
    }
#endif
}

X11LockBackend::~X11LockBackend()
{
    stopKeepAbove();
    ungrab();
#ifdef QBAR_LOCK_HAVE_X11
    if (m_connection != nullptr) {
        if (m_cursor != 0) {
            xcb_free_cursor(static_cast<xcb_connection_t *>(m_connection), static_cast<xcb_cursor_t>(m_cursor));
            m_cursor = 0;
        }
        m_connection = nullptr;
    }
#endif
}

bool X11LockBackend::isAvailable() const
{
#ifdef QBAR_LOCK_HAVE_X11
    return m_connection != nullptr && !qgetenv("DISPLAY").isEmpty();
#else
    return false;
#endif
}

QString X11LockBackend::unavailableReason() const
{
#ifdef QBAR_LOCK_HAVE_X11
    return tr("X11 display is not available");
#else
    return tr("qbar-lock was built without X11/xlock support");
#endif
}

void X11LockBackend::setGrabWindow(quintptr windowId)
{
    m_grabWindow = windowId;
}

void X11LockBackend::setLockWindows(const QList<quintptr> &windowIds)
{
    m_lockWindows = windowIds;
}

#ifdef QBAR_LOCK_HAVE_X11
namespace {
xcb_window_t rootWindowOf(xcb_connection_t *connection)
{
    return xcb_setup_roots_iterator(xcb_get_setup(connection)).data->root;
}
} // namespace
#endif

void X11LockBackend::raiseLockWindows()
{
#ifdef QBAR_LOCK_HAVE_X11
    auto *connection = static_cast<xcb_connection_t *>(m_connection);
    const uint32_t values[] = {XCB_STACK_MODE_ABOVE};
    for (quintptr id : m_lockWindows) {
        xcb_configure_window(connection, static_cast<xcb_window_t>(id),
                             XCB_CONFIG_WINDOW_STACK_MODE, values);
    }
    xcb_flush(connection);
#endif
}

void X11LockBackend::startKeepAbove()
{
#ifdef QBAR_LOCK_HAVE_X11
    if (m_keepingAbove || m_connection == nullptr || m_lockWindows.isEmpty()) {
        return;
    }
    auto *connection = static_cast<xcb_connection_t *>(m_connection);
    const xcb_window_t root = rootWindowOf(connection);

    // The event mask is per-client and CW_EVENT_MASK replaces it wholesale —
    // preserve Qt's own selection on the root window (we share its connection).
    auto *attributes = xcb_get_window_attributes_reply(
        connection, xcb_get_window_attributes(connection, root), nullptr);
    m_rootPrevEventMask = attributes != nullptr ? attributes->your_event_mask : 0;
    std::free(attributes);

    const uint32_t mask = m_rootPrevEventMask | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY;
    xcb_change_window_attributes(connection, root, XCB_CW_EVENT_MASK, &mask);
    xcb_flush(connection);

    QCoreApplication::instance()->installNativeEventFilter(this);
    m_keepingAbove = true;
    raiseLockWindows();
#endif
}

void X11LockBackend::stopKeepAbove()
{
#ifdef QBAR_LOCK_HAVE_X11
    if (!m_keepingAbove) {
        return;
    }
    QCoreApplication::instance()->removeNativeEventFilter(this);
    if (m_connection != nullptr) {
        auto *connection = static_cast<xcb_connection_t *>(m_connection);
        xcb_change_window_attributes(connection, rootWindowOf(connection),
                                     XCB_CW_EVENT_MASK, &m_rootPrevEventMask);
        xcb_flush(connection);
    }
    m_keepingAbove = false;
#endif
}

bool X11LockBackend::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *)
{
#ifdef QBAR_LOCK_HAVE_X11
    if (!m_keepingAbove || eventType != QByteArrayLiteral("xcb_generic_event_t")) {
        return false;
    }
    auto *event = static_cast<xcb_generic_event_t *>(message);
    xcb_window_t restacked = XCB_WINDOW_NONE;
    switch (event->response_type & 0x7f) {
    case XCB_CONFIGURE_NOTIFY:
        restacked = reinterpret_cast<xcb_configure_notify_event_t *>(event)->window;
        break;
    case XCB_MAP_NOTIFY:
        restacked = reinterpret_cast<xcb_map_notify_event_t *>(event)->window;
        break;
    case XCB_CIRCULATE_NOTIFY:
        restacked = reinterpret_cast<xcb_circulate_notify_event_t *>(event)->window;
        break;
    default:
        return false;
    }
    // Our own raises are reported here too; re-raising on them would loop.
    for (quintptr id : m_lockWindows) {
        if (static_cast<xcb_window_t>(id) == restacked) {
            return false;
        }
    }
    raiseLockWindows();
#else
    Q_UNUSED(eventType)
    Q_UNUSED(message)
#endif
    return false;
}

void X11LockBackend::lock()
{
    if (!isAvailable()) {
        emit lockFailed(unavailableReason());
        return;
    }
    if (!grab()) {
        emit lockFailed(tr("Failed to grab X11 keyboard and pointer"));
        return;
    }
    startKeepAbove();
    emit locked();
}

void X11LockBackend::unlock()
{
    stopKeepAbove();
    ungrab();
    emit unlocked();
}

bool X11LockBackend::grab()
{
#ifndef QBAR_LOCK_HAVE_X11
    return false;
#else
    if (m_connection == nullptr) {
        return false;
    }

    auto *connection = static_cast<xcb_connection_t *>(m_connection);
    const xcb_setup_t *setup = xcb_get_setup(connection);
    xcb_screen_iterator_t iter = xcb_setup_roots_iterator(setup);
    if (iter.rem == 0 || iter.data == nullptr) {
        return false;
    }

    const xcb_window_t root = iter.data->root;
    const xcb_window_t grabWindow = m_grabWindow != 0 ? static_cast<xcb_window_t>(m_grabWindow) : root;
    xcb_set_input_focus(connection, XCB_INPUT_FOCUS_POINTER_ROOT, grabWindow, XCB_CURRENT_TIME);
    xcb_flush(connection);

    xcb_grab_keyboard_cookie_t keyboardCookie = xcb_grab_keyboard(
        connection, 1, grabWindow, XCB_CURRENT_TIME, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);
    xcb_grab_keyboard_reply_t *keyboardReply = xcb_grab_keyboard_reply(connection, keyboardCookie, nullptr);
    const bool keyboardOk = keyboardReply != nullptr && keyboardReply->status == XCB_GRAB_STATUS_SUCCESS;
    free(keyboardReply);
    if (!keyboardOk) {
        return false;
    }

    xcb_grab_pointer_cookie_t pointerCookie = xcb_grab_pointer(
        connection,
        1,
        grabWindow,
        XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_POINTER_MOTION,
        XCB_GRAB_MODE_ASYNC,
        XCB_GRAB_MODE_ASYNC,
        grabWindow,
        static_cast<xcb_cursor_t>(m_cursor),
        XCB_CURRENT_TIME);
    xcb_grab_pointer_reply_t *pointerReply = xcb_grab_pointer_reply(connection, pointerCookie, nullptr);
    const bool pointerOk = pointerReply != nullptr && pointerReply->status == XCB_GRAB_STATUS_SUCCESS;
    free(pointerReply);
    if (!pointerOk) {
        xcb_ungrab_keyboard(connection, XCB_CURRENT_TIME);
        xcb_flush(connection);
        return false;
    }

    xcb_flush(connection);
    m_grabbed = true;
    return true;
#endif
}

void X11LockBackend::ungrab()
{
#ifdef QBAR_LOCK_HAVE_X11
    if (!m_grabbed || m_connection == nullptr) {
        return;
    }
    auto *connection = static_cast<xcb_connection_t *>(m_connection);
    xcb_ungrab_pointer(connection, XCB_CURRENT_TIME);
    xcb_ungrab_keyboard(connection, XCB_CURRENT_TIME);
    xcb_flush(connection);
    m_grabbed = false;
#endif
}
