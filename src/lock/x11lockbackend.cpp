#include "x11lockbackend.h"

#include <QByteArray>
#include <QGuiApplication>

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
    return QStringLiteral("X11 display is not available");
#else
    return QStringLiteral("qbar-lock was built without X11/xlock support");
#endif
}

void X11LockBackend::setGrabWindow(quintptr windowId)
{
    m_grabWindow = windowId;
}

void X11LockBackend::lock()
{
    if (!isAvailable()) {
        emit lockFailed(unavailableReason());
        return;
    }
    if (!grab()) {
        emit lockFailed(QStringLiteral("Failed to grab X11 keyboard and pointer"));
        return;
    }
    emit locked();
}

void X11LockBackend::unlock()
{
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
