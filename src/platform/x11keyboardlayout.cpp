#include "x11keyboardlayout.h"

#include <cstdlib>
#include <cstring>

#include <xcb/xcb.h>
// xcb/xkb.h declares a struct field literally named `explicit` (a C++ keyword);
// rename it across the include so this translation unit compiles as C++. We never
// touch that field, so the rename is harmless.
#define explicit explicit_xkb_field
#include <xcb/xkb.h>
#undef explicit

X11KeyboardLayout::X11KeyboardLayout(QObject *parent)
    : QObject(parent)
{
    m_conn = xcb_connect(nullptr, nullptr);
    if (m_conn == nullptr || xcb_connection_has_error(m_conn) != 0) {
        if (m_conn != nullptr) {
            xcb_disconnect(m_conn);
            m_conn = nullptr;
        }
        return;
    }

    // XKB must be initialised on this connection before xkb requests work.
    xcb_xkb_use_extension_cookie_t useCookie =
        xcb_xkb_use_extension(m_conn, XCB_XKB_MAJOR_VERSION, XCB_XKB_MINOR_VERSION);
    xcb_xkb_use_extension_reply_t *useReply =
        xcb_xkb_use_extension_reply(m_conn, useCookie, nullptr);
    m_xkbReady = useReply != nullptr && useReply->supported != 0;
    std::free(useReply);

    const xcb_setup_t *setup = xcb_get_setup(m_conn);
    if (setup != nullptr) {
        m_root = xcb_setup_roots_iterator(setup).data->root;
    }

    const char *name = "_XKB_RULES_NAMES";
    xcb_intern_atom_cookie_t atomCookie =
        xcb_intern_atom(m_conn, 1, static_cast<uint16_t>(std::strlen(name)), name);
    xcb_intern_atom_reply_t *atomReply = xcb_intern_atom_reply(m_conn, atomCookie, nullptr);
    if (atomReply != nullptr) {
        m_rulesAtom = atomReply->atom;
        std::free(atomReply);
    }

    if (!m_xkbReady) {
        return;
    }

    // Poll like CapsLockMonitor: layout changes are user-driven, so ~400ms latency
    // is imperceptible and avoids wiring up XkbStateNotify event delivery.
    connect(&m_timer, &QTimer::timeout, this, &X11KeyboardLayout::poll);
    m_timer.start(400);
    poll();
}

X11KeyboardLayout::~X11KeyboardLayout()
{
    if (m_conn != nullptr) {
        xcb_disconnect(m_conn);
    }
}

QStringList X11KeyboardLayout::readLayoutCodes() const
{
    if (m_conn == nullptr || m_rulesAtom == 0) {
        return {};
    }

    // _XKB_RULES_NAMES is a STRING of NUL-separated fields:
    //   rules, model, layout, variant, options
    // The layout field is a comma-separated list of codes (e.g. "us,br").
    xcb_get_property_cookie_t cookie =
        xcb_get_property(m_conn, 0, m_root, m_rulesAtom, XCB_ATOM_STRING, 0, 1024);
    xcb_get_property_reply_t *reply = xcb_get_property_reply(m_conn, cookie, nullptr);
    if (reply == nullptr) {
        return {};
    }

    const int len = xcb_get_property_value_length(reply);
    const char *data = static_cast<const char *>(xcb_get_property_value(reply));
    QStringList fields;
    int start = 0;
    for (int i = 0; i < len; ++i) {
        if (data[i] == '\0') {
            fields.append(QString::fromUtf8(data + start, i - start));
            start = i + 1;
        }
    }
    if (start < len) {
        fields.append(QString::fromUtf8(data + start, len - start));
    }
    std::free(reply);

    if (fields.size() < 3) {
        return {};
    }
    return fields.at(2).split(QChar::fromLatin1(','), Qt::SkipEmptyParts);
}

void X11KeyboardLayout::poll()
{
    if (m_conn == nullptr || !m_xkbReady) {
        return;
    }

    xcb_xkb_get_state_cookie_t cookie = xcb_xkb_get_state(m_conn, XCB_XKB_ID_USE_CORE_KBD);
    xcb_xkb_get_state_reply_t *reply = xcb_xkb_get_state_reply(m_conn, cookie, nullptr);
    if (reply == nullptr) {
        return;
    }
    const int group = reply->group;
    std::free(reply);

    const QStringList codes = readLayoutCodes();
    QString code;
    if (group >= 0 && group < codes.size()) {
        code = codes.at(group);
    } else if (!codes.isEmpty()) {
        code = codes.first();
    }

    if (code != m_layout) {
        m_layout = code;
        emit layoutChanged();
    }
}
