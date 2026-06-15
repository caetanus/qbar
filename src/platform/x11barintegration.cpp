#include "platformbarintegration.h"

#include <QGuiApplication>
#include <QScreen>
#include <QWindow>
#include <qpa/qplatformnativeinterface.h>
#include <cstdlib>
#include <cstring>
#include <xcb/xcb.h>

namespace {

xcb_atom_t internAtom(xcb_connection_t *connection, const char *name)
{
    const xcb_intern_atom_cookie_t cookie = xcb_intern_atom(connection, false, std::strlen(name), name);
    xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply(connection, cookie, nullptr);
    if (reply == nullptr) {
        return XCB_ATOM_NONE;
    }

    const xcb_atom_t atom = reply->atom;
    std::free(reply);
    return atom;
}

void replaceCardinals(xcb_connection_t *connection, xcb_window_t window, xcb_atom_t property, const uint32_t *values, int count)
{
    xcb_change_property(connection,
                        XCB_PROP_MODE_REPLACE,
                        window,
                        property,
                        XCB_ATOM_CARDINAL,
                        32,
                        count,
                        values);
}

void replaceAtom(xcb_connection_t *connection, xcb_window_t window, xcb_atom_t property, xcb_atom_t value)
{
    xcb_change_property(connection,
                        XCB_PROP_MODE_REPLACE,
                        window,
                        property,
                        XCB_ATOM_ATOM,
                        32,
                        1,
                        &value);
}

} // namespace

bool applyX11BarIntegration(QWindow *window, const BarConfig &config)
{
    auto *native = QGuiApplication::platformNativeInterface();
    auto *connection = native != nullptr
        ? static_cast<xcb_connection_t *>(native->nativeResourceForIntegration(QByteArrayLiteral("connection")))
        : nullptr;
    if (connection == nullptr) {
        return false;
    }

    window->create();
    const auto xid = static_cast<xcb_window_t>(window->winId());

    const xcb_atom_t netWindowType = internAtom(connection, "_NET_WM_WINDOW_TYPE");
    const xcb_atom_t netWindowTypeDock = internAtom(connection, "_NET_WM_WINDOW_TYPE_DOCK");
    const xcb_atom_t netWmState = internAtom(connection, "_NET_WM_STATE");
    const xcb_atom_t netWmStateAbove = internAtom(connection, "_NET_WM_STATE_ABOVE");
    const xcb_atom_t netWmStrut = internAtom(connection, "_NET_WM_STRUT");
    const xcb_atom_t netWmStrutPartial = internAtom(connection, "_NET_WM_STRUT_PARTIAL");

    if (netWindowType != XCB_ATOM_NONE && netWindowTypeDock != XCB_ATOM_NONE) {
        replaceAtom(connection, xid, netWindowType, netWindowTypeDock);
    }

    if (netWmState != XCB_ATOM_NONE && netWmStateAbove != XCB_ATOM_NONE) {
        replaceAtom(connection, xid, netWmState, netWmStateAbove);
    }

    const QRect geometry = window->screen() != nullptr ? window->screen()->geometry() : window->geometry();
    const uint32_t reservedHeight = static_cast<uint32_t>(config.height);
    const bool bottom = config.position == BarPosition::Bottom;
    const uint32_t effectiveHeight = config.exclusiveZone ? reservedHeight : 0;
    const uint32_t strut[4] = {
        0,
        0,
        bottom ? 0 : effectiveHeight,
        bottom ? effectiveHeight : 0,
    };
    const uint32_t partial[12] = {
        0,
        0,
        bottom ? 0 : effectiveHeight,
        bottom ? effectiveHeight : 0,
        0,
        0,
        0,
        0,
        bottom ? 0 : static_cast<uint32_t>(geometry.x()),
        bottom ? 0 : static_cast<uint32_t>(geometry.x() + geometry.width() - 1),
        bottom ? static_cast<uint32_t>(geometry.x()) : 0,
        bottom ? static_cast<uint32_t>(geometry.x() + geometry.width() - 1) : 0,
    };

    if (netWmStrut != XCB_ATOM_NONE) {
        replaceCardinals(connection, xid, netWmStrut, strut, 4);
    }

    if (netWmStrutPartial != XCB_ATOM_NONE) {
        replaceCardinals(connection, xid, netWmStrutPartial, partial, 12);
    }

    xcb_flush(connection);
    return true;
}
