#include "x11gamma.h"

#ifdef QBAR_HAVE_XCB_RANDR

#include <QDebug>
#include <xcb/randr.h>
#include <xcb/xcb.h>

#include <cstdlib>
#include <vector>

X11GammaBackend::X11GammaBackend(QObject *parent)
    : GammaBackend(parent)
{
}

X11GammaBackend::~X11GammaBackend()
{
    if (m_conn != nullptr) {
        if (m_touched) {
            setAll({});  // identity — Xorg keeps ramps after the client goes away
        }
        xcb_disconnect(m_conn);
    }
}

bool X11GammaBackend::init()
{
    m_conn = xcb_connect(nullptr, nullptr);
    if (m_conn == nullptr || xcb_connection_has_error(m_conn) != 0) {
        if (m_conn != nullptr) {
            xcb_disconnect(m_conn);
            m_conn = nullptr;
        }
        return false;
    }
    const xcb_query_extension_reply_t *ext = xcb_get_extension_data(m_conn, &xcb_randr_id);
    if (ext == nullptr || ext->present == 0) {
        qInfo() << "[redshift] RandR extension unavailable";
        xcb_disconnect(m_conn);
        m_conn = nullptr;
        return false;
    }
    xcb_randr_query_version_reply_t *ver = xcb_randr_query_version_reply(
        m_conn, xcb_randr_query_version(m_conn, 1, 3), nullptr);
    const bool ok = ver != nullptr && (ver->major_version > 1 || (ver->major_version == 1 && ver->minor_version >= 3));
    std::free(ver);
    if (!ok) {
        qInfo() << "[redshift] RandR 1.3+ required";
        xcb_disconnect(m_conn);
        m_conn = nullptr;
        return false;
    }
    // Probe the CRTC count without touching anything.
    int count = 0;
    for (auto it = xcb_setup_roots_iterator(xcb_get_setup(m_conn)); it.rem != 0; xcb_screen_next(&it)) {
        auto *res = xcb_randr_get_screen_resources_current_reply(
            m_conn, xcb_randr_get_screen_resources_current(m_conn, it.data->root), nullptr);
        if (res == nullptr) {
            continue;
        }
        count += res->num_crtcs;
        std::free(res);
    }
    m_crtcCount = count;
    return true;
}

QString X11GammaBackend::name() const
{
    return QStringLiteral("xrandr");
}

bool X11GammaBackend::available() const
{
    return m_conn != nullptr && m_crtcCount > 0;
}

QString X11GammaBackend::status() const
{
    if (m_conn == nullptr) {
        return tr("RandR unavailable");
    }
    return tr("%n CRTC(s)", nullptr, m_crtcCount);
}

void X11GammaBackend::setAll(const colortemp::Whitepoint &wp)
{
    if (m_conn == nullptr) {
        return;
    }
    int count = 0;
    for (auto it = xcb_setup_roots_iterator(xcb_get_setup(m_conn)); it.rem != 0; xcb_screen_next(&it)) {
        auto *res = xcb_randr_get_screen_resources_current_reply(
            m_conn, xcb_randr_get_screen_resources_current(m_conn, it.data->root), nullptr);
        if (res == nullptr) {
            continue;
        }
        const xcb_randr_crtc_t *crtcs = xcb_randr_get_screen_resources_current_crtcs(res);
        for (int i = 0; i < res->num_crtcs; ++i) {
            auto *info = xcb_randr_get_crtc_info_reply(
                m_conn, xcb_randr_get_crtc_info(m_conn, crtcs[i], res->config_timestamp), nullptr);
            const bool enabled = info != nullptr && info->mode != XCB_NONE;
            std::free(info);
            if (!enabled) {
                continue;
            }
            auto *sizeReply = xcb_randr_get_crtc_gamma_size_reply(
                m_conn, xcb_randr_get_crtc_gamma_size(m_conn, crtcs[i]), nullptr);
            const int size = sizeReply != nullptr ? sizeReply->size : 0;
            std::free(sizeReply);
            if (size <= 0) {
                continue;
            }
            std::vector<uint16_t> table(static_cast<size_t>(size) * 3);
            fillGammaRamps(table.data(), static_cast<size_t>(size), wp);
            xcb_randr_set_crtc_gamma(m_conn, crtcs[i], static_cast<uint16_t>(size),
                                     table.data(), table.data() + size, table.data() + 2 * size);
            ++count;
        }
        std::free(res);
    }
    xcb_flush(m_conn);
    if (count != m_crtcCount) {
        m_crtcCount = count;
        emit availabilityChanged();
    }
}

void X11GammaBackend::apply(const colortemp::Whitepoint &wp)
{
    m_touched = true;
    setAll(wp);
}

void X11GammaBackend::release()
{
    if (m_touched) {
        setAll({});
    }
}

#endif // QBAR_HAVE_XCB_RANDR
