#include "qbarlayershellintegration.h"

#include <QtWaylandClient/private/qwaylanddisplay_p.h>
#include <QtWaylandClient/private/qwaylandscreen_p.h>
#include <QtWaylandClient/private/qwaylandwindow_p.h>

#include <QCoreApplication>
#include <qpa/qwindowsysteminterface.h>
#include <QDebug>
#include <QDynamicPropertyChangeEvent>
#include <QEvent>
#include <QGuiApplication>
#include <QRect>
#include <QScreen>
#include <QString>
#include <QWindow>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <wayland-client.h>

#include "xdg-shell-client-protocol.h"

namespace {

constexpr int protocolVersion = 4;

struct RegistryState {
    zwlr_layer_shell_v1 *layerShell = nullptr;
    xdg_wm_base *xdgWmBase = nullptr;
};

int envInt(const char *name, int fallback)
{
    const char *value = std::getenv(name);
    if (value == nullptr || *value == '\0') {
        return fallback;
    }

    return std::atoi(value);
}

bool hasEnv(const char *name)
{
    const char *value = std::getenv(name);
    return value != nullptr && *value != '\0';
}

QString envString(const char *name)
{
    const char *value = std::getenv(name);
    return value != nullptr ? QString::fromLocal8Bit(value) : QString();
}

bool envBool(const char *name, bool fallback)
{
    const char *value = std::getenv(name);
    if (value == nullptr || *value == '\0') {
        return fallback;
    }

    return std::strcmp(value, "0") != 0 && std::strcmp(value, "false") != 0;
}

bool isBottom()
{
    const char *value = std::getenv("QBAR_LAYER_POSITION");
    return value != nullptr && std::strcmp(value, "bottom") == 0;
}

void registryGlobal(void *data, wl_registry *registry, uint32_t name, const char *interface, uint32_t version)
{
    auto *state = static_cast<RegistryState *>(data);
    if (std::strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
        state->layerShell = static_cast<zwlr_layer_shell_v1 *>(
            wl_registry_bind(registry, name, &zwlr_layer_shell_v1_interface, std::min(version, static_cast<uint32_t>(protocolVersion))));
    } else if (std::strcmp(interface, xdg_wm_base_interface.name) == 0) {
        state->xdgWmBase = static_cast<xdg_wm_base *>(
            wl_registry_bind(registry, name, &xdg_wm_base_interface, std::min(version, static_cast<uint32_t>(6))));
    }
}

void registryGlobalRemove(void *, wl_registry *, uint32_t)
{
}

const wl_registry_listener registryListener = {
    registryGlobal,
    registryGlobalRemove,
};

const zwlr_layer_surface_v1_listener layerSurfaceListener = {
    QBarLayerShellSurface::handleConfigure,
    QBarLayerShellSurface::handleClosed,
};

void handleXdgWmBasePing(void *, xdg_wm_base *wmBase, uint32_t serial)
{
    xdg_wm_base_pong(wmBase, serial);
}

const xdg_wm_base_listener xdgWmBaseListener = {
    handleXdgWmBasePing,
};

const xdg_surface_listener xdgSurfaceListener = {
    QBarXdgPopupSurface::handleXdgSurfaceConfigure,
};

const xdg_surface_listener xdgToplevelSurfaceListener = {
    QBarXdgToplevelSurface::handleXdgSurfaceConfigure,
};

const xdg_popup_listener xdgPopupListener = {
    QBarXdgPopupSurface::handlePopupConfigure,
    QBarXdgPopupSurface::handlePopupDone,
    QBarXdgPopupSurface::handlePopupRepositioned,
};

const xdg_toplevel_listener xdgToplevelListener = {
    QBarXdgToplevelSurface::handleToplevelConfigure,
    QBarXdgToplevelSurface::handleToplevelClose,
    QBarXdgToplevelSurface::handleConfigureBounds,
    QBarXdgToplevelSurface::handleWmCapabilities,
};

} // namespace

bool QBarLayerShellIntegration::initialize(QtWaylandClient::QWaylandDisplay *display)
{
    qDebug() << "QBar layer-shell integration initialize";
    RegistryState state;
    wl_display *wlDisplay = display->wl_display();
    wl_registry *registry = wl_display_get_registry(wlDisplay);
    wl_registry_add_listener(registry, &registryListener, &state);
    wl_display_roundtrip(wlDisplay);
    wl_registry_destroy(registry);

    m_layerShell = state.layerShell;
    m_xdgWmBase = state.xdgWmBase;
    if (m_xdgWmBase != nullptr) {
        xdg_wm_base_add_listener(m_xdgWmBase, &xdgWmBaseListener, this);
    }
    qWarning() << "QBar layer-shell available:" << (m_layerShell != nullptr);
    qWarning() << "QBar xdg-shell available:" << (m_xdgWmBase != nullptr);
    return m_layerShell != nullptr && m_xdgWmBase != nullptr;
}

QtWaylandClient::QWaylandShellSurface *QBarLayerShellIntegration::createShellSurface(QtWaylandClient::QWaylandWindow *window)
{
    // The backdrop overlay must be a full-output layer surface. It carries
    // Qt::Tool flags (which include the Qt::Popup bit), so it must be checked
    // BEFORE the popup test below, otherwise it would be misclassified as an
    // xdg_popup and never get its layer geometry.
    const bool overlay = window != nullptr && window->window() != nullptr
        && window->window()->property("qbarOverlay").toBool();
    const bool detached = window != nullptr && window->window() != nullptr
        && window->window()->property("qbarDetachedPopup").toBool();
    // The dock is also a Qt::Tool window (→ carries the Qt::Popup bit) but must be a
    // sized layer surface, not an xdg_popup — exclude it from the popup test too.
    const bool dock = window != nullptr && window->window() != nullptr
        && window->window()->property("qbarDock").toBool();
    // Same for the notification toast surface.
    const bool notifications = window != nullptr && window->window() != nullptr
        && window->window()->property("qbarNotifications").toBool();
    if (detached) {
        qDebug() << "QBar layer-shell create detached xdg_toplevel surface";
        return new QBarXdgToplevelSurface(this, window);
    }
    if (!overlay && !dock && !notifications && window != nullptr && window->window() != nullptr
        && (window->window()->flags() & Qt::Popup) == Qt::Popup) {
        qDebug() << "QBar layer-shell create popup surface";
        return new QBarXdgPopupSurface(this, window);
    }

    return new QBarLayerShellSurface(this, window);
}

zwlr_layer_shell_v1 *QBarLayerShellIntegration::layerShell() const
{
    return m_layerShell;
}

xdg_wm_base *QBarLayerShellIntegration::xdgWmBase() const
{
    return m_xdgWmBase;
}

QBarLayerShellSurface::QBarLayerShellSurface(QBarLayerShellIntegration *integration, QtWaylandClient::QWaylandWindow *window)
    : QtWaylandClient::QWaylandShellSurface(window)
{
    // Bind the surface to the window's target wl_output so each bar lands on its
    // own monitor (main.cpp resolves BarConfig::output → QScreen before show()).
    // A null output lets the compositor pick, which is right for the single bar.
    ::wl_output *output = nullptr;
    if (window != nullptr) {
        if (auto *screen = window->waylandScreen()) {
            output = screen->output();
        }
    }
    // Everything (bar, dock, popup backdrop) lives on the TOP layer, NOT OVERLAY:
    // OVERLAY draws above fullscreen surfaces, so a dock on it would cover e.g. a
    // fullscreen image viewer. The dock's layer surface is created lazily, after its
    // bar's, so within TOP it stacks above the bar — its magnified icons still draw
    // over the bar, while a fullscreen window now correctly covers the dock.
    //
    // The namespace defaults to "qbar"; a window may claim its own via the
    // qbarLayerNamespace property (the notification surface uses "qbar-notifications")
    // so compositor layer rules — e.g. Hyprland `layerrule = blur, qbar-notifications`
    // — can target it without touching the bar.
    QByteArray layerNamespace("qbar");
    if (window != nullptr && window->window() != nullptr) {
        const QVariant ns = window->window()->property("qbarLayerNamespace");
        if (ns.isValid() && !ns.toString().isEmpty()) {
            layerNamespace = ns.toString().toUtf8();
        }
    }
    m_layerSurface = zwlr_layer_shell_v1_get_layer_surface(
        integration->layerShell(),
        wlSurface(),
        output,
        ZWLR_LAYER_SHELL_V1_LAYER_TOP,
        layerNamespace.constData());
    zwlr_layer_surface_v1_add_listener(m_layerSurface, &layerSurfaceListener, this);
    applyLayerState();
    wl_surface_commit(wlSurface());
    qDebug() << "QBar layer-shell surface committed";

    // Watch the bar window for live CSS edge-gap changes. barwindow re-derives
    // qbarBarMarginTop/Bottom from the theme on hot-reload (setProperty), which posts a
    // QDynamicPropertyChangeEvent; re-apply + commit so a floating margin updates without a
    // restart. (Initial construction sets the properties BEFORE this surface exists, so the
    // first apply above already has them — the filter only matters for later reloads.)
    if (window != nullptr && window->window() != nullptr) {
        m_filteredWindow = window->window();
        m_filteredWindow->installEventFilter(this);
    }
}

QBarLayerShellSurface::~QBarLayerShellSurface()
{
    // Remove ourselves from the window's event-filter list before we die, or a later event to
    // a still-alive window would dispatch through a dangling filter and crash
    // (sendThroughObjectEventFilters → null).
    if (m_filteredWindow != nullptr) {
        m_filteredWindow->removeEventFilter(this);
    }
    if (m_layerSurface != nullptr) {
        zwlr_layer_surface_v1_destroy(m_layerSurface);
    }
}

bool QBarLayerShellSurface::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::DynamicPropertyChange && m_layerSurface != nullptr) {
        const QByteArray name = static_cast<QDynamicPropertyChangeEvent *>(event)->propertyName();
        if (name == "qbarBarMarginTop" || name == "qbarBarMarginBottom"
            || name == "qbarOverlayKeyboard" || name == "qbarOverlayInputPassthrough"
            || name == "qbarDockX" || name == "qbarDockWidth" || name == "qbarDockHeight"
            || name == "qbarDockInputX" || name == "qbarDockInputY"
            || name == "qbarDockInputWidth" || name == "qbarDockInputHeight"
            || name.startsWith("qbarNotif")) {
            applyLayerState();
            // Do NOT wl_surface_commit here: this runs on the GUI thread, and the render
            // thread owns the surface's attach/damage/commit cycle — an interleaved commit
            // can publish a half-built frame (a visible flicker on every toast entry/exit,
            // each of which bumps the input region). The layer state set above is double-
            // buffered; scheduling a render pass makes its commit carry it atomically.
            if (m_filteredWindow != nullptr) {
                QMetaObject::invokeMethod(
                    m_filteredWindow,
                    [w = QPointer<QWindow>(m_filteredWindow)]() {
                        if (w) {
                            w->requestUpdate();
                        }
                    },
                    Qt::QueuedConnection);
            }
        }
    }
    return QtWaylandClient::QWaylandShellSurface::eventFilter(watched, event);
}

bool QBarLayerShellSurface::isExposed() const
{
    return m_configured;
}

void QBarLayerShellSurface::applyConfigure()
{
    if (m_configuredSize.isValid()) {
        qDebug() << "QBar layer-shell apply configure:" << m_configuredSize;
        resizeFromApplyConfigure(m_configuredSize);
    }
}

void QBarLayerShellSurface::setWindowGeometry(const QRect &rect)
{
    Q_UNUSED(rect);
    applyLayerState();
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
void QBarLayerShellSurface::setWindowSize(const QSize &size)
{
    Q_UNUSED(size);
    applyLayerState();
}
#endif

std::any QBarLayerShellSurface::surfaceRole() const
{
    return m_layerSurface;
}

void QBarLayerShellSurface::attachPopup(QtWaylandClient::QWaylandShellSurface *popup)
{
    auto *xdgPopup = dynamic_cast<QBarXdgPopupSurface *>(popup);
    if (xdgPopup != nullptr) {
        xdgPopup->setLayerParent(m_layerSurface);
    }
}

void QBarLayerShellSurface::detachPopup(QtWaylandClient::QWaylandShellSurface *popup)
{
    Q_UNUSED(popup);
}

void QBarLayerShellSurface::configure(uint32_t serial, uint32_t width, uint32_t height)
{
    zwlr_layer_surface_v1_ack_configure(m_layerSurface, serial);
    const int fallbackHeight = envInt("QBAR_LAYER_HEIGHT", 28);
    m_configuredSize = QSize(width > 0 ? static_cast<int>(width) : 1,
                             height > 0 ? static_cast<int>(height) : fallbackHeight);
    m_configured = true;
    qDebug() << "QBar layer-shell configure:" << m_configuredSize;
    resizeFromApplyConfigure(m_configuredSize);
    // Defer the exposure/update kick instead of calling it synchronously from this wl-event
    // dispatch. Qt itself NEVER calls updateExposure() synchronously from a listener — it
    // always marshals it (QueuedConnection). Calling it inline here re-enters Qt's window
    // machinery mid-dispatch and races the frame-callback / shell-surface teardown path
    // (QWaylandWindow::calculateExposure → mShellSurface->isExposed() on a freed surface).
    // Binding the invoke to the window means Qt drops it if the window is being destroyed.
    if (window() != nullptr) {
        auto *w = window();
        QMetaObject::invokeMethod(
            w, [w]() {
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
                w->updateExposure();
#else
                QWindowSystemInterface::handleExposeEvent(
                    w->window(), QRect(QPoint(), w->geometry().size()));
#endif
                w->requestUpdate();
            }, Qt::QueuedConnection);
    }
    applyConfigureWhenPossible();
}

void QBarLayerShellSurface::closeFromCompositor()
{
    if (QCoreApplication::instance() != nullptr) {
        QCoreApplication::quit();
    }
}

void QBarLayerShellSurface::applyLayerState()
{
    if (m_layerSurface == nullptr) {
        return;
    }

    // The popup backdrop overlay (flagged via the "qbarOverlay" window property)
    // spans the whole output except the bar's strip. Rather than rely on the
    // compositor carving the bar's exclusive zone out of an all-anchored surface
    // (which not all compositors do), we ignore exclusive zones (-1) and inset
    // the overlay ourselves with a margin on the bar's edge equal to its height.
    const bool isOverlay = window() != nullptr && window()->window() != nullptr
        && window()->window()->property("qbarOverlay").toBool();
    if (isOverlay) {
        // The overlay mirrors its bar's geometry (set as qbarBar* properties by
        // QBarPopupService), falling back to the process-wide env.
        const QWindow *overlayWin = window()->window();
        const QVariant heightProp = overlayWin->property("qbarBarHeight");
        const QVariant exclusiveProp = overlayWin->property("qbarBarExclusive");
        const QVariant positionProp = overlayWin->property("qbarBarPosition");
        const int barHeight = heightProp.isValid() ? heightProp.toInt() : envInt("QBAR_LAYER_HEIGHT", 28);
        const bool exclusive = exclusiveProp.isValid() ? exclusiveProp.toBool() : envBool("QBAR_LAYER_EXCLUSIVE", true);
        const bool bottom = positionProp.isValid()
            ? positionProp.toString() == QLatin1String("bottom")
            : isBottom();

        const QScreen *outScreen = overlayWin->screen() != nullptr ? overlayWin->screen()
                                                                   : QGuiApplication::primaryScreen();
        const int outputHeight = outScreen != nullptr ? outScreen->geometry().height() : 0;
        const int barInset = exclusive ? barHeight : 0;

        if (barInset > 0 && outputHeight > barInset) {
            // Cover the whole output EXCEPT the bar's strip. Anchoring to all four
            // edges and insetting with a margin works on sway but NOT on Hyprland,
            // which ignores both the margin and a 0 exclusive zone on a top+bottom-
            // anchored surface and lets the backdrop overlap the bar. So drop the bar's
            // edge, anchor the three others, and size the surface explicitly to the
            // remaining height — unambiguous on every compositor. (Width stays anchored
            // to both sides; only the bar-facing vertical edge is freed.)
            const uint32_t vAnchor = bottom ? ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP
                                            : ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM;
            zwlr_layer_surface_v1_set_anchor(
                m_layerSurface,
                vAnchor | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
            zwlr_layer_surface_v1_set_size(m_layerSurface, 0, static_cast<uint32_t>(outputHeight - barInset));
        } else {
            // Non-exclusive bar (or unknown output size): cover the whole output.
            zwlr_layer_surface_v1_set_anchor(
                m_layerSurface,
                ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM
                    | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
            zwlr_layer_surface_v1_set_size(m_layerSurface, 0, 0);
        }
        zwlr_layer_surface_v1_set_exclusive_zone(m_layerSurface, -1);
        zwlr_layer_surface_v1_set_margin(m_layerSurface, 0, 0, 0, 0);
        // Take the keyboard (for Escape-to-close / typing in popups) only when the user
        // opted in via BarConfig::popupKeyboardFocus; otherwise stay focusless.
        //
        // Use ON_DEMAND, not EXCLUSIVE. EXCLUSIVE is a MODAL keyboard grab: while the
        // overlay holds it, Hyprland refuses to move focus, so clicking the bar (or a
        // popup provider) does nothing until the grab is released by clicking elsewhere
        // — the bar appears frozen. ON_DEMAND still lets the popup hold the keyboard,
        // but a click on the bar transfers focus normally and the bar stays responsive
        // (this is how sway already behaved with EXCLUSIVE).
        // Parked overlay (popup reuse): the surface stays MAPPED but inert —
        // fully transparent content, an EMPTY input region so clicks and hover
        // pass through to the windows beneath, and no keyboard. Unmapping
        // instead would run the compositor's layer map/unmap animations and
        // blur re-layout on every popup open/close (a visible flicker on
        // Hyprland). The service flips qbarOverlayInputPassthrough on park
        // and off on the next open.
        const bool passthrough = overlayWin->property("qbarOverlayInputPassthrough").toBool();
        if (window()->display() != nullptr && window()->display()->compositor() != nullptr) {
            if (passthrough) {
                wl_region *region = window()->display()->compositor()->create_region();
                wl_surface_set_input_region(wlSurface(), region); // empty region
                wl_region_destroy(region);
            } else {
                wl_surface_set_input_region(wlSurface(), nullptr); // whole surface
            }
        }
        const bool grabKeyboard = !passthrough
            && window()->window()->property("qbarOverlayKeyboard").toBool();
        zwlr_layer_surface_v1_set_keyboard_interactivity(
            m_layerSurface,
            grabKeyboard
                ? ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND
                : ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);
        return;
    }

    // The dock (flagged via "qbarDock" by DockWindow) is a fixed-size surface floated
    // over the bar's reserved slot: anchored to the bar edge + left, offset by
    // qbarDockX, sized to the slot width × (bar height + magnification headroom). It
    // reserves nothing — the in-bar proxy applet already holds the space — so its
    // exclusive zone is 0.
    {
        const QWindow *dockWin = window() != nullptr ? window()->window() : nullptr;
        if (dockWin != nullptr && dockWin->property("qbarDock").toBool()) {
            const bool bottom = dockWin->property("qbarBarPosition").toString() == QLatin1String("bottom");
            const int dockX = dockWin->property("qbarDockX").toInt();
            const int dockW = dockWin->property("qbarDockWidth").toInt();
            const int dockH = dockWin->property("qbarDockHeight").toInt();
            const int edgeMargin = bottom
                ? dockWin->property("qbarBarMarginBottom").toInt()
                : dockWin->property("qbarBarMarginTop").toInt();
            const uint32_t vAnchor = bottom
                ? ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM
                : ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP;
            zwlr_layer_surface_v1_set_anchor(m_layerSurface, vAnchor | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT);
            zwlr_layer_surface_v1_set_size(
                m_layerSurface, static_cast<uint32_t>(std::max(1, dockW)), static_cast<uint32_t>(std::max(1, dockH)));
            // -1 (not 0): ignore other surfaces' exclusive zones so the dock anchors
            // DIRECTLY to the bar edge and OVERLAPS the bar, instead of being pushed
            // off the top of the bar's reserved strip. The dock sits exactly where the
            // proxy applet would inside the bar; the in-bar proxy already holds the slot.
            zwlr_layer_surface_v1_set_exclusive_zone(m_layerSurface, -1);
            // (top, right, bottom, left) — left positions the surface at the slot's x;
            // the bar-edge margin keeps it flush with a floating bar.
            zwlr_layer_surface_v1_set_margin(
                m_layerSurface, bottom ? 0 : edgeMargin, 0, bottom ? edgeMargin : 0, dockX);
            zwlr_layer_surface_v1_set_keyboard_interactivity(
                m_layerSurface, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);
            if (window() != nullptr && window()->display() != nullptr && window()->display()->compositor() != nullptr) {
                wl_region *region = window()->display()->compositor()->create_region();
                wl_region_add(region,
                              dockWin->property("qbarDockInputX").toInt(),
                              dockWin->property("qbarDockInputY").toInt(),
                              std::max(1, dockWin->property("qbarDockInputWidth").toInt()),
                              std::max(1, dockWin->property("qbarDockInputHeight").toInt()));
                wl_surface_set_input_region(wlSurface(), region);
                wl_region_destroy(region);
            }
            return;
        }
    }

    // The notification surface (flagged via "qbarNotifications" by NotificationWindow)
    // is a fixed-width, full-height strip on a screen corner side (or centered when
    // neither left nor right is set). Anchoring both vertical edges stretches it
    // between them; exclusive zone 0 keeps it inside the area the bar reserved. Only
    // the card stack accepts input (qbarNotifInput*), the empty rest of the strip
    // passes pointer events through.
    {
        const QWindow *notifWin = window() != nullptr ? window()->window() : nullptr;
        if (notifWin != nullptr && notifWin->property("qbarNotifications").toBool()) {
            const bool left = notifWin->property("qbarNotifCornerLeft").toBool();
            const bool center = notifWin->property("qbarNotifCornerCenter").toBool();
            const int notifWidth = std::max(1, notifWin->property("qbarNotifWidth").toInt());
            uint32_t anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM;
            if (!center) {
                anchor |= left ? ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT : ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
            }
            zwlr_layer_surface_v1_set_anchor(m_layerSurface, anchor);
            zwlr_layer_surface_v1_set_size(m_layerSurface, static_cast<uint32_t>(notifWidth), 0);
            zwlr_layer_surface_v1_set_exclusive_zone(m_layerSurface, 0);
            zwlr_layer_surface_v1_set_margin(m_layerSurface,
                                             notifWin->property("qbarNotifMarginTop").toInt(),
                                             notifWin->property("qbarNotifMarginRight").toInt(),
                                             notifWin->property("qbarNotifMarginBottom").toInt(),
                                             notifWin->property("qbarNotifMarginLeft").toInt());
            zwlr_layer_surface_v1_set_keyboard_interactivity(
                m_layerSurface, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);
            if (window() != nullptr && window()->display() != nullptr
                && window()->display()->compositor() != nullptr) {
                wl_region *region = window()->display()->compositor()->create_region();
                const int inputHeight = notifWin->property("qbarNotifInputHeight").toInt();
                if (inputHeight > 0) {
                    wl_region_add(region,
                                  notifWin->property("qbarNotifInputX").toInt(),
                                  notifWin->property("qbarNotifInputY").toInt(),
                                  std::max(1, notifWin->property("qbarNotifInputWidth").toInt()),
                                  inputHeight);
                }
                wl_surface_set_input_region(wlSurface(), region);
                wl_region_destroy(region);
            }
            return;
        }
    }

    // Per-window geometry (set by BarWindow from its BarConfig) wins over the
    // process-wide QBAR_LAYER_* env so several bars in one config can differ.
    const QWindow *win = window() != nullptr ? window()->window() : nullptr;
    const QVariant heightProp = win != nullptr ? win->property("qbarBarHeight") : QVariant();
    const QVariant exclusiveProp = win != nullptr ? win->property("qbarBarExclusive") : QVariant();
    const QVariant positionProp = win != nullptr ? win->property("qbarBarPosition") : QVariant();
    const int height = heightProp.isValid() ? heightProp.toInt() : envInt("QBAR_LAYER_HEIGHT", 28);
    const bool exclusive = exclusiveProp.isValid() ? exclusiveProp.toBool() : envBool("QBAR_LAYER_EXCLUSIVE", true);
    const bool bottom = positionProp.isValid()
        ? positionProp.toString() == QLatin1String("bottom")
        : isBottom();
    const uint32_t verticalAnchor = bottom
        ? ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM
        : ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP;

    zwlr_layer_surface_v1_set_anchor(
        m_layerSurface,
        verticalAnchor
            | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT
            | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
    // Bar CSS margins → a floating bar (barwindow derives the px values). The compositor reserves
    // `exclusive_zone + margin-on-the-anchored-edge`, so the two margins play different roles:
    //   • anchored-edge margin (top for a top bar) — set_margin positions the bar off the screen
    //     edge, and the compositor adds it to the reserved area for us.
    //   • opposite-edge margin (bottom for a top bar) — the INNER gap between the bar and the
    //     tiled windows; it isn't part of set_margin's effect, so it goes into the exclusive zone.
    // Hence exclusive = height + innerGap (NOT height + anchoredGap, which would double-count the
    // anchored margin and reserve a phantom strip).
    const int marginTop = win != nullptr ? win->property("qbarBarMarginTop").toInt() : 0;
    const int marginBottom = win != nullptr ? win->property("qbarBarMarginBottom").toInt() : 0;
    const int innerGap = bottom ? marginTop : marginBottom;
    zwlr_layer_surface_v1_set_size(m_layerSurface, 0, static_cast<uint32_t>(height));
    zwlr_layer_surface_v1_set_exclusive_zone(m_layerSurface, exclusive ? height + innerGap : 0);
    zwlr_layer_surface_v1_set_margin(m_layerSurface, marginTop, 0, marginBottom, 0);
    zwlr_layer_surface_v1_set_keyboard_interactivity(
        m_layerSurface,
        ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);
}

void QBarLayerShellSurface::handleConfigure(void *data,
                                            zwlr_layer_surface_v1 *,
                                            uint32_t serial,
                                            uint32_t width,
                                            uint32_t height)
{
    static_cast<QBarLayerShellSurface *>(data)->configure(serial, width, height);
}

void QBarLayerShellSurface::handleClosed(void *data, zwlr_layer_surface_v1 *)
{
    static_cast<QBarLayerShellSurface *>(data)->closeFromCompositor();
}

QBarXdgToplevelSurface::QBarXdgToplevelSurface(QBarLayerShellIntegration *integration,
                                               QtWaylandClient::QWaylandWindow *window)
    : QtWaylandClient::QWaylandShellSurface(window)
{
    if (window != nullptr && window->window() != nullptr)
        m_geometry = window->window()->geometry();
    if (!m_geometry.isValid())
        m_geometry.setSize(QSize(1, 1));

    m_xdgSurface = xdg_wm_base_get_xdg_surface(integration->xdgWmBase(), wlSurface());
    xdg_surface_add_listener(m_xdgSurface, &xdgToplevelSurfaceListener, this);
    m_xdgToplevel = xdg_surface_get_toplevel(m_xdgSurface);
    xdg_toplevel_add_listener(m_xdgToplevel, &xdgToplevelListener, this);
    const QString title = window != nullptr && window->window() != nullptr
        ? window->window()->title() : QStringLiteral("QBar Detached");
    const QByteArray titleUtf8 = title.toUtf8();
    xdg_toplevel_set_title(m_xdgToplevel, titleUtf8.constData());
    xdg_toplevel_set_app_id(m_xdgToplevel, "qbar-detached");
    wl_surface_commit(wlSurface());
}

QBarXdgToplevelSurface::~QBarXdgToplevelSurface()
{
    if (m_xdgToplevel != nullptr)
        xdg_toplevel_destroy(m_xdgToplevel);
    if (m_xdgSurface != nullptr)
        xdg_surface_destroy(m_xdgSurface);
}

bool QBarXdgToplevelSurface::isExposed() const
{
    return m_configured;
}

void QBarXdgToplevelSurface::applyConfigure()
{
    if (m_configuredSize.isValid())
        resizeFromApplyConfigure(m_configuredSize);
}

void QBarXdgToplevelSurface::setWindowGeometry(const QRect &rect)
{
    if (!rect.isValid() || m_xdgSurface == nullptr)
        return;
    m_geometry = rect;
    xdg_surface_set_window_geometry(m_xdgSurface, 0, 0, rect.width(), rect.height());
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
void QBarXdgToplevelSurface::setWindowSize(const QSize &size)
{
    if (size.isValid())
        m_geometry.setSize(size);
}
#endif

std::any QBarXdgToplevelSurface::surfaceRole() const
{
    return m_xdgToplevel;
}

void QBarXdgToplevelSurface::attachPopup(QtWaylandClient::QWaylandShellSurface *popup)
{
    Q_UNUSED(popup);
}

void QBarXdgToplevelSurface::detachPopup(QtWaylandClient::QWaylandShellSurface *popup)
{
    Q_UNUSED(popup);
}

void QBarXdgToplevelSurface::handleXdgSurfaceConfigure(void *data,
                                                       xdg_surface *surface,
                                                       uint32_t serial)
{
    auto *toplevel = static_cast<QBarXdgToplevelSurface *>(data);
    xdg_surface_ack_configure(surface, serial);
    toplevel->m_configured = true;
    if (!toplevel->m_configuredSize.isValid())
        toplevel->m_configuredSize = toplevel->m_geometry.size();
    if (toplevel->window() != nullptr) {
        auto *window = toplevel->window();
        QMetaObject::invokeMethod(window, [window]() {
        #if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    window->updateExposure();
#else
    QWindowSystemInterface::handleExposeEvent(
        window->window(), QRect(QPoint(), window->geometry().size()));
#endif
            window->requestUpdate();
        }, Qt::QueuedConnection);
    }
    toplevel->applyConfigureWhenPossible();
}

void QBarXdgToplevelSurface::handleToplevelConfigure(void *data,
                                                     xdg_toplevel *,
                                                     int32_t width,
                                                     int32_t height,
                                                     wl_array *)
{
    auto *toplevel = static_cast<QBarXdgToplevelSurface *>(data);
    if (width > 0 && height > 0)
        toplevel->m_configuredSize = QSize(width, height);
}

void QBarXdgToplevelSurface::handleToplevelClose(void *data, xdg_toplevel *)
{
    auto *toplevel = static_cast<QBarXdgToplevelSurface *>(data);
    if (toplevel->window() != nullptr && toplevel->window()->window() != nullptr) {
        QWindow *window = toplevel->window()->window();
        QMetaObject::invokeMethod(window, [window]() { window->close(); }, Qt::QueuedConnection);
    }
}

void QBarXdgToplevelSurface::handleConfigureBounds(void *, xdg_toplevel *, int32_t, int32_t)
{
}

void QBarXdgToplevelSurface::handleWmCapabilities(void *, xdg_toplevel *, wl_array *)
{
}

QBarXdgPopupSurface::QBarXdgPopupSurface(QBarLayerShellIntegration *integration, QtWaylandClient::QWaylandWindow *window)
    : QtWaylandClient::QWaylandShellSurface(window)
    , m_integration(integration)
{
    if (window != nullptr && window->window() != nullptr) {
        m_geometry = window->window()->geometry();
    }
    if (!m_geometry.isValid()) {
        m_geometry.setSize(QSize(1, 1));
    }

    // NOTE: the xdg_surface is deliberately NOT created here. An xdg_surface that is committed
    // before it has a role (xdg_popup) is a fatal protocol error ("xdg_surface must have a role
    // object"), and Qt may commit wlSurface() between this constructor and the later
    // attachPopup/setLayerParent that assigns the role — or a Qt::Popup window may never get a
    // parent attached at all. So we create the xdg_surface and its xdg_popup role together,
    // atomically, in createPopup().
}

QBarXdgPopupSurface::~QBarXdgPopupSurface()
{
    if (m_xdgPopup != nullptr) {
        xdg_popup_destroy(m_xdgPopup);
    }
    if (m_xdgSurface != nullptr) {
        xdg_surface_destroy(m_xdgSurface);
    }
}

bool QBarXdgPopupSurface::isExposed() const
{
    return m_configured;
}

void QBarXdgPopupSurface::applyConfigure()
{
    if (m_configuredSize.isValid()) {
        setGeometryFromApplyConfigure(parentOrigin() + m_configuredPosition, m_configuredSize);
    }
}

void QBarXdgPopupSurface::setWindowGeometry(const QRect &rect)
{
    if (rect.isValid()) {
        m_geometry = rect;
        if (m_parentPopupSurface != nullptr) {
            m_parentOrigin = m_parentPopupSurface->globalPosition();
        }
        reposition();
    }
}

void QBarXdgPopupSurface::setWindowPosition(const QPoint &position)
{
    m_geometry.moveTopLeft(position);
    reposition();
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
void QBarXdgPopupSurface::setWindowSize(const QSize &size)
{
    if (size.isValid()) {
        m_geometry.setSize(size);
        reposition();
    }
}
#endif

std::any QBarXdgPopupSurface::surfaceRole() const
{
    return m_xdgPopup;
}

void QBarXdgPopupSurface::attachPopup(QtWaylandClient::QWaylandShellSurface *popup)
{
    auto *xdgPopup = dynamic_cast<QBarXdgPopupSurface *>(popup);
    if (xdgPopup != nullptr) {
        xdgPopup->setPopupParent(this);
    }
}

void QBarXdgPopupSurface::detachPopup(QtWaylandClient::QWaylandShellSurface *popup)
{
    Q_UNUSED(popup);
}

void QBarXdgPopupSurface::setLayerParent(zwlr_layer_surface_v1 *layerSurface)
{
    if (m_attachedToLayer || layerSurface == nullptr) {
        return;
    }

    m_parentLayerSurface = layerSurface;
    createPopup(nullptr);
    zwlr_layer_surface_v1_get_popup(m_parentLayerSurface, m_xdgPopup);
    m_attachedToLayer = true;
    wl_surface_commit(wlSurface());
}

void QBarXdgPopupSurface::setPopupParent(QBarXdgPopupSurface *parent)
{
    if (parent == nullptr || m_xdgPopup != nullptr) {
        return;
    }

    m_parentPopupSurface = parent;
    m_parentOrigin = parent->globalPosition();
    createPopup(parent->m_xdgSurface);
    m_attachedToLayer = true;
    wl_surface_commit(wlSurface());
}

void QBarXdgPopupSurface::createPopup(xdg_surface *parentSurface)
{
    if (m_xdgPopup != nullptr) {
        return;
    }

    // Create the xdg_surface and its xdg_popup role together (see the constructor note): no
    // event dispatch happens between them, so the surface can never be seen role-less.
    if (m_xdgSurface == nullptr) {
        m_xdgSurface = xdg_wm_base_get_xdg_surface(m_integration->xdgWmBase(), wlSurface());
        xdg_surface_add_listener(m_xdgSurface, &xdgSurfaceListener, this);
    }

    xdg_positioner *positioner = createPositioner();
    m_xdgPopup = xdg_surface_get_popup(m_xdgSurface, parentSurface, positioner);
    xdg_positioner_destroy(positioner);
    xdg_popup_add_listener(m_xdgPopup, &xdgPopupListener, this);
}

bool QBarXdgPopupSurface::popupBarIsBottom()
{
    // The bar edge is authoritative on the window property (set by BarWindow /
    // QBarPopupService); the QBAR_LAYER_POSITION env is only a fallback for the
    // standalone popup process. Reading the env alone misreads a config-driven
    // bottom bar as top, which sends tooltips to the wrong edge.
    const QWindow *win = window() != nullptr ? window()->window() : nullptr;
    QVariant prop = win != nullptr ? win->property("qbarBarPosition") : QVariant();
    if (!prop.isValid() && win != nullptr && win->transientParent() != nullptr) {
        prop = win->transientParent()->property("qbarBarPosition");
    }
    if (prop.isValid()) {
        return prop.toString() == QLatin1String("bottom");
    }
    return isBottom();
}

int QBarXdgPopupSurface::popupBarHeight()
{
    const QWindow *win = window() != nullptr ? window()->window() : nullptr;
    QVariant prop = win != nullptr ? win->property("qbarBarHeight") : QVariant();
    if (!prop.isValid() && win != nullptr && win->transientParent() != nullptr) {
        prop = win->transientParent()->property("qbarBarHeight");
    }
    const int height = prop.isValid() ? prop.toInt() : envInt("QBAR_LAYER_HEIGHT", 28);
    return height > 0 ? height : 28;
}

xdg_positioner *QBarXdgPopupSurface::createPositioner()
{
    xdg_positioner *positioner = xdg_wm_base_create_positioner(m_integration->xdgWmBase());
    const QSize size = m_geometry.size().isValid() ? m_geometry.size() : QSize(1, 1);
    const QPoint relativePosition = m_geometry.topLeft() - parentOrigin();
    const QString targetTitle = envString("QBAR_LAYER_POPUP_ANCHOR_TITLE");
    const QString windowTitle = window() != nullptr && window()->window() != nullptr
        ? window()->window()->title()
        : QString();
    const bool hasAnchor = !targetTitle.isEmpty()
        && windowTitle == targetTitle
        && hasEnv("QBAR_LAYER_POPUP_ANCHOR_X")
        && hasEnv("QBAR_LAYER_POPUP_ANCHOR_Y")
        && hasEnv("QBAR_LAYER_POPUP_ANCHOR_WIDTH")
        && hasEnv("QBAR_LAYER_POPUP_ANCHOR_HEIGHT");

    xdg_positioner_set_size(positioner, std::max(size.width(), 1), std::max(size.height(), 1));
    if (hasAnchor) {
        xdg_positioner_set_anchor_rect(
            positioner,
            envInt("QBAR_LAYER_POPUP_ANCHOR_X", 0),
            envInt("QBAR_LAYER_POPUP_ANCHOR_Y", 0),
            std::max(envInt("QBAR_LAYER_POPUP_ANCHOR_WIDTH", 1), 1),
            std::max(envInt("QBAR_LAYER_POPUP_ANCHOR_HEIGHT", 1), 1));
        xdg_positioner_set_anchor(positioner, isBottom() ? XDG_POSITIONER_ANCHOR_TOP_RIGHT : XDG_POSITIONER_ANCHOR_BOTTOM_RIGHT);
        xdg_positioner_set_gravity(positioner, isBottom() ? XDG_POSITIONER_GRAVITY_TOP_LEFT : XDG_POSITIONER_GRAVITY_BOTTOM_LEFT);
    } else {
        // Tooltips/menus anchor at a point and grow away from the bar's edge: a
        // top bar grows down, a bottom bar grows up — so a tooltip is never left
        // below the cursor at the screen's bottom edge.
        //
        // m_geometry is the intended on-screen rect, so relativePosition is its
        // TOP-left (relative to the parent surface). With an UPWARD gravity (bottom
        // bar) the anchor point becomes the popup's BOTTOM edge, so anchoring at the
        // rect's top would land the popup a full popup-height too high (away from the
        // bar). Anchor at the rect's bottom (top + height) in that case; a top bar
        // grows down from the top, so its anchor stays at the top.
        const bool bottom = popupBarIsBottom();
        const int anchorY = bottom ? relativePosition.y() + std::max(size.height(), 1)
                                   : relativePosition.y();
        xdg_positioner_set_anchor_rect(positioner, relativePosition.x(), anchorY, 1, 1);
        xdg_positioner_set_anchor(positioner, bottom ? XDG_POSITIONER_ANCHOR_BOTTOM_LEFT : XDG_POSITIONER_ANCHOR_TOP_LEFT);
        xdg_positioner_set_gravity(positioner, bottom ? XDG_POSITIONER_GRAVITY_TOP_RIGHT : XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT);
    }
    // Point-anchored tooltips/menus are already positioned (and screen-clamped by
    // QBarPopupService) where we want them, with their edge sitting exactly on the
    // screen boundary. FLIP would treat that boundary-touch as a violation and mirror
    // the popup a FULL width/height to the other side of the anchor — a big jump.
    // Allow only SLIDE (a minimal nudge to fit). The applet-anchored path (hasAnchor)
    // genuinely wants to flip to the other side of its anchor rect, so keep FLIP there.
    uint32_t constraint = XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_X
        | XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_Y;
    if (hasAnchor) {
        constraint |= XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_FLIP_X
            | XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_FLIP_Y;
    }
    xdg_positioner_set_constraint_adjustment(positioner, constraint);

    if (xdg_wm_base_get_version(m_integration->xdgWmBase()) >= XDG_POSITIONER_SET_REACTIVE_SINCE_VERSION) {
        xdg_positioner_set_reactive(positioner);
        if (!hasAnchor && window() != nullptr && window()->window() != nullptr && window()->window()->screen() != nullptr) {
            const QRect screenGeometry = window()->window()->screen()->geometry();
            xdg_positioner_set_parent_size(positioner, screenGeometry.width(), envInt("QBAR_LAYER_HEIGHT", 28));
        }
    }

    return positioner;
}

QPoint QBarXdgPopupSurface::parentOrigin()
{
    if (m_parentPopupSurface != nullptr) {
        return m_parentOrigin;
    }

    QScreen *screen = nullptr;
    if (window() != nullptr && window()->window() != nullptr) {
        screen = window()->window()->screen();
    }
    if (screen == nullptr) {
        screen = QGuiApplication::primaryScreen();
    }
    const QRect screenGeometry = screen != nullptr ? screen->geometry() : QRect();
    const int height = popupBarHeight();
    return QPoint(screenGeometry.x(), popupBarIsBottom() ? screenGeometry.bottom() + 1 - height : screenGeometry.y());
}

QPoint QBarXdgPopupSurface::globalPosition()
{
    if (m_configuredSize.isValid()) {
        return parentOrigin() + m_configuredPosition;
    }

    return m_geometry.topLeft();
}

void QBarXdgPopupSurface::reposition()
{
    if (m_xdgPopup == nullptr || !m_attachedToLayer || xdg_popup_get_version(m_xdgPopup) < XDG_POPUP_REPOSITION_SINCE_VERSION) {
        return;
    }

    xdg_positioner *positioner = createPositioner();
    xdg_popup_reposition(m_xdgPopup, positioner, m_repositionToken++);
    xdg_positioner_destroy(positioner);
    wl_surface_commit(wlSurface());
}

void QBarXdgPopupSurface::handleXdgSurfaceConfigure(void *data, xdg_surface *surface, uint32_t serial)
{
    auto *popup = static_cast<QBarXdgPopupSurface *>(data);
    xdg_surface_ack_configure(surface, serial);
    popup->m_configured = true;
    if (!popup->m_configuredSize.isValid()) {
        popup->m_configuredSize = popup->m_geometry.size().isValid() ? popup->m_geometry.size() : QSize(1, 1);
    }
    if (popup->window() != nullptr) {
        // Defer (don't drive Qt's window machinery synchronously from this wl dispatch).
        auto *w = popup->window();
        QMetaObject::invokeMethod(
            w, [w]() {
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
                w->updateExposure();
#else
                QWindowSystemInterface::handleExposeEvent(
                    w->window(), QRect(QPoint(), w->geometry().size()));
#endif
                w->requestUpdate();
            }, Qt::QueuedConnection);
    }
    popup->applyConfigureWhenPossible();
}

void QBarXdgPopupSurface::handlePopupConfigure(void *data,
                                               xdg_popup *,
                                               int32_t x,
                                               int32_t y,
                                               int32_t width,
                                               int32_t height)
{
    auto *popup = static_cast<QBarXdgPopupSurface *>(data);
    popup->m_configuredPosition = QPoint(x, y);
    popup->m_configuredSize = QSize(width > 0 ? width : std::max(popup->m_geometry.width(), 1),
                                    height > 0 ? height : std::max(popup->m_geometry.height(), 1));
}

void QBarXdgPopupSurface::handlePopupDone(void *data, xdg_popup *)
{
    auto *popup = static_cast<QBarXdgPopupSurface *>(data);
    if (popup->window() != nullptr && popup->window()->window() != nullptr) {
        // CRITICAL: never hide (→ tear down the surface) synchronously from inside this wl
        // event dispatch — that re-enters Qt's window/shell-surface teardown while a frame
        // callback can be in flight (the UAF). Defer to the GUI event loop; bound to the
        // QWindow so Qt drops it if the window is already gone.
        QWindow *win = popup->window()->window();
        QMetaObject::invokeMethod(win, [win]() { win->setVisible(false); }, Qt::QueuedConnection);
    }
}

void QBarXdgPopupSurface::handlePopupRepositioned(void *, xdg_popup *, uint32_t)
{
}

QtWaylandClient::QWaylandShellIntegration *QBarLayerShellPlugin::create(const QString &key, const QStringList &paramList)
{
    Q_UNUSED(paramList);
    if (key == QStringLiteral("qbar-layer-shell")) {
        return new QBarLayerShellIntegration;
    }

    return nullptr;
}
