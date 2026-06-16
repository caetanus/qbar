#include "qbarlayershellintegration.h"

#include <QtWaylandClient/6.11.1/QtWaylandClient/private/qwaylanddisplay_p.h>
#include <QtWaylandClient/6.11.1/QtWaylandClient/private/qwaylandscreen_p.h>
#include <QtWaylandClient/6.11.1/QtWaylandClient/private/qwaylandwindow_p.h>

#include <QCoreApplication>
#include <QDebug>
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

const xdg_popup_listener xdgPopupListener = {
    QBarXdgPopupSurface::handlePopupConfigure,
    QBarXdgPopupSurface::handlePopupDone,
    QBarXdgPopupSurface::handlePopupRepositioned,
};

} // namespace

bool QBarLayerShellIntegration::initialize(QtWaylandClient::QWaylandDisplay *display)
{
    qWarning() << "QBar layer-shell integration initialize";
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
    if (!overlay && window != nullptr && window->window() != nullptr
        && (window->window()->flags() & Qt::Popup) == Qt::Popup) {
        qWarning() << "QBar layer-shell create popup surface";
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
    m_layerSurface = zwlr_layer_shell_v1_get_layer_surface(
        integration->layerShell(),
        wlSurface(),
        output,
        ZWLR_LAYER_SHELL_V1_LAYER_TOP,
        "qbar");
    zwlr_layer_surface_v1_add_listener(m_layerSurface, &layerSurfaceListener, this);
    applyLayerState();
    wl_surface_commit(wlSurface());
    qWarning() << "QBar layer-shell surface committed";
}

QBarLayerShellSurface::~QBarLayerShellSurface()
{
    if (m_layerSurface != nullptr) {
        zwlr_layer_surface_v1_destroy(m_layerSurface);
    }
}

bool QBarLayerShellSurface::isExposed() const
{
    return m_configured;
}

void QBarLayerShellSurface::applyConfigure()
{
    if (m_configuredSize.isValid()) {
        qWarning() << "QBar layer-shell apply configure:" << m_configuredSize;
        resizeFromApplyConfigure(m_configuredSize);
    }
}

void QBarLayerShellSurface::setWindowGeometry(const QRect &rect)
{
    Q_UNUSED(rect);
    applyLayerState();
}

void QBarLayerShellSurface::setWindowSize(const QSize &size)
{
    Q_UNUSED(size);
    applyLayerState();
}

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
    qWarning() << "QBar layer-shell configure:" << m_configuredSize;
    resizeFromApplyConfigure(m_configuredSize);
    if (window() != nullptr) {
        window()->updateExposure();
        window()->requestUpdate();
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
        const int barInset = exclusive ? barHeight : 0;
        const int topMargin = bottom ? 0 : barInset;
        const int bottomMargin = bottom ? barInset : 0;
        zwlr_layer_surface_v1_set_anchor(
            m_layerSurface,
            ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP
                | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM
                | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT
                | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
        zwlr_layer_surface_v1_set_size(m_layerSurface, 0, 0);
        zwlr_layer_surface_v1_set_exclusive_zone(m_layerSurface, -1);
        zwlr_layer_surface_v1_set_margin(m_layerSurface, topMargin, 0, bottomMargin, 0);
        // Grab the keyboard (for Escape-to-close) only when the user opted in via
        // BarConfig::popupKeyboardFocus; otherwise stay focusless.
        const bool grabKeyboard = window()->window()->property("qbarOverlayKeyboard").toBool();
        zwlr_layer_surface_v1_set_keyboard_interactivity(
            m_layerSurface,
            grabKeyboard
                ? ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE
                : ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);
        return;
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
    zwlr_layer_surface_v1_set_size(m_layerSurface, 0, static_cast<uint32_t>(height));
    zwlr_layer_surface_v1_set_exclusive_zone(m_layerSurface, exclusive ? height : 0);
    zwlr_layer_surface_v1_set_margin(m_layerSurface, 0, 0, 0, 0);
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

    m_xdgSurface = xdg_wm_base_get_xdg_surface(m_integration->xdgWmBase(), wlSurface());
    xdg_surface_add_listener(m_xdgSurface, &xdgSurfaceListener, this);
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

void QBarXdgPopupSurface::setWindowSize(const QSize &size)
{
    if (size.isValid()) {
        m_geometry.setSize(size);
        reposition();
    }
}

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
        const bool bottom = popupBarIsBottom();
        xdg_positioner_set_anchor_rect(positioner, relativePosition.x(), relativePosition.y(), 1, 1);
        xdg_positioner_set_anchor(positioner, bottom ? XDG_POSITIONER_ANCHOR_BOTTOM_LEFT : XDG_POSITIONER_ANCHOR_TOP_LEFT);
        xdg_positioner_set_gravity(positioner, bottom ? XDG_POSITIONER_GRAVITY_TOP_RIGHT : XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT);
    }
    xdg_positioner_set_constraint_adjustment(
        positioner,
        XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_X
            | XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_Y
            | XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_FLIP_X
            | XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_FLIP_Y);

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
        popup->window()->updateExposure();
        popup->window()->requestUpdate();
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
        popup->window()->window()->setVisible(false);
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
