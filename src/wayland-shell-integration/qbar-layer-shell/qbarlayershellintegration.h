#pragma once

#include <QtWaylandClient/private/qwaylandshellintegration_p.h>
#include <QtWaylandClient/private/qwaylandshellsurface_p.h>
#include <QtWaylandClient/private/qwaylandshellintegrationplugin_p.h>

#include <QPoint>
#include <QPointer>
#include <QRect>
#include <QSize>

#include <cstdint>

#include "wlr-layer-shell-unstable-v1-client-protocol.h"

class QWindow;
struct xdg_popup;
struct xdg_surface;
struct xdg_toplevel;
struct xdg_wm_base;

class QBarLayerShellIntegration final : public QtWaylandClient::QWaylandShellIntegration {
public:
    bool initialize(QtWaylandClient::QWaylandDisplay *display) override;
    QtWaylandClient::QWaylandShellSurface *createShellSurface(QtWaylandClient::QWaylandWindow *window) override;

    zwlr_layer_shell_v1 *layerShell() const;
    xdg_wm_base *xdgWmBase() const;

private:
    zwlr_layer_shell_v1 *m_layerShell = nullptr;
    xdg_wm_base *m_xdgWmBase = nullptr;
};

class QBarLayerShellSurface final : public QtWaylandClient::QWaylandShellSurface {
    Q_OBJECT

public:
    QBarLayerShellSurface(QBarLayerShellIntegration *integration, QtWaylandClient::QWaylandWindow *window);
    ~QBarLayerShellSurface() override;

    bool isExposed() const override;
    void applyConfigure() override;
    void setWindowGeometry(const QRect &rect) override;
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    // QWaylandShellSurface::setWindowSize(QSize) is a ~6.9 addition; older Qt
    // (Debian stable ships 6.8) only has the setWindowGeometry(QRect) virtual.
    void setWindowSize(const QSize &size) override;
#endif
    std::any surfaceRole() const override;
    void attachPopup(QtWaylandClient::QWaylandShellSurface *popup) override;
    void detachPopup(QtWaylandClient::QWaylandShellSurface *popup) override;

    static void handleConfigure(void *data,
                                zwlr_layer_surface_v1 *surface,
                                uint32_t serial,
                                uint32_t width,
                                uint32_t height);
    static void handleClosed(void *data, zwlr_layer_surface_v1 *surface);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void configure(uint32_t serial, uint32_t width, uint32_t height);
    void closeFromCompositor();
    void applyLayerState();
    // Coalesce a burst of property changes (the toast strip sets four input-region
    // properties per animation frame) into ONE applyLayerState + frame per event-loop turn.
    void scheduleApplyLayerState();

    // Change-tracked layer-state setters. wlroots flags EVERY set_* request as a pending
    // state change, and on commit the compositor then re-arranges ALL layer surfaces of
    // the output (sway: arrange_layers → a configure to every bar/dock/strip). Re-sending
    // an unchanged state on each toast frame produced exactly that storm, and the bar's
    // relayouts behind it froze the GUI thread for hundreds of ms per notification.
    void sendAnchor(uint32_t anchor);
    void sendSize(uint32_t width, uint32_t height);
    void sendExclusiveZone(int32_t zone);
    void sendMargin(int32_t top, int32_t right, int32_t bottom, int32_t left);
    void sendKeyboard(uint32_t mode);
    // whole=true: the entire surface (nullptr region); else exactly `rect` (an invalid
    // rect = empty region, input passes through).
    void sendInputRegion(bool whole, const QRect &rect);

    struct SentState {
        uint32_t anchor = ~0u;
        uint32_t width = ~0u;
        uint32_t height = ~0u;
        int32_t zone = INT32_MIN;
        int32_t marginTop = INT32_MIN;
        int32_t marginRight = INT32_MIN;
        int32_t marginBottom = INT32_MIN;
        int32_t marginLeft = INT32_MIN;
        uint32_t keyboard = ~0u;
        int inputMode = -1; // -1 unsent, 0 whole surface, 1 rect
        QRect inputRect;
    } m_sent;
    bool m_applyPending = false;

    zwlr_layer_surface_v1 *m_layerSurface = nullptr;
    QSize m_configuredSize;
    bool m_configured = false;
    // The QWindow we installed our margin event filter on. A QPointer so that if the window is
    // destroyed first it auto-nulls (and Qt removes the filter); if WE are destroyed first the
    // dtor removes the filter so the window's filter list never holds a dangling pointer.
    QPointer<QWindow> m_filteredWindow;
};

class QBarXdgPopupSurface final : public QtWaylandClient::QWaylandShellSurface {
public:
    QBarXdgPopupSurface(QBarLayerShellIntegration *integration, QtWaylandClient::QWaylandWindow *window);
    ~QBarXdgPopupSurface() override;

    bool isExposed() const override;
    void applyConfigure() override;
    void setWindowGeometry(const QRect &rect) override;
    void setWindowPosition(const QPoint &position) override;
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    // QWaylandShellSurface::setWindowSize(QSize) is a ~6.9 addition; older Qt
    // (Debian stable ships 6.8) only has the setWindowGeometry(QRect) virtual.
    void setWindowSize(const QSize &size) override;
#endif
    std::any surfaceRole() const override;
    void attachPopup(QtWaylandClient::QWaylandShellSurface *popup) override;
    void detachPopup(QtWaylandClient::QWaylandShellSurface *popup) override;

    void setLayerParent(zwlr_layer_surface_v1 *layerSurface);
    void setPopupParent(QBarXdgPopupSurface *parent);

    static void handleXdgSurfaceConfigure(void *data, xdg_surface *surface, uint32_t serial);
    static void handlePopupConfigure(void *data,
                                     xdg_popup *popup,
                                     int32_t x,
                                     int32_t y,
                                     int32_t width,
                                     int32_t height);
    static void handlePopupDone(void *data, xdg_popup *popup);
    static void handlePopupRepositioned(void *data, xdg_popup *popup, uint32_t token);

private:
    void createPopup(xdg_surface *parentSurface);
    struct xdg_positioner *createPositioner();
    QPoint parentOrigin();
    bool popupBarIsBottom();
    int popupBarHeight();
    QPoint globalPosition();
    void reposition();

    QBarLayerShellIntegration *m_integration = nullptr;
    zwlr_layer_surface_v1 *m_parentLayerSurface = nullptr;
    QBarXdgPopupSurface *m_parentPopupSurface = nullptr;
    xdg_surface *m_xdgSurface = nullptr;
    xdg_popup *m_xdgPopup = nullptr;
    QRect m_geometry;
    QPoint m_parentOrigin;
    QPoint m_configuredPosition;
    QSize m_configuredSize;
    uint32_t m_repositionToken = 1;
    bool m_configured = false;
    bool m_attachedToLayer = false;
};

class QBarXdgToplevelSurface final : public QtWaylandClient::QWaylandShellSurface {
public:
    QBarXdgToplevelSurface(QBarLayerShellIntegration *integration,
                           QtWaylandClient::QWaylandWindow *window);
    ~QBarXdgToplevelSurface() override;

    bool isExposed() const override;
    void applyConfigure() override;
    void setWindowGeometry(const QRect &rect) override;
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    // QWaylandShellSurface::setWindowSize(QSize) is a ~6.9 addition; older Qt
    // (Debian stable ships 6.8) only has the setWindowGeometry(QRect) virtual.
    void setWindowSize(const QSize &size) override;
#endif
    std::any surfaceRole() const override;
    void attachPopup(QtWaylandClient::QWaylandShellSurface *popup) override;
    void detachPopup(QtWaylandClient::QWaylandShellSurface *popup) override;

    static void handleXdgSurfaceConfigure(void *data, xdg_surface *surface, uint32_t serial);
    static void handleToplevelConfigure(void *data,
                                        xdg_toplevel *toplevel,
                                        int32_t width,
                                        int32_t height,
                                        wl_array *states);
    static void handleToplevelClose(void *data, xdg_toplevel *toplevel);
    static void handleConfigureBounds(void *data,
                                      xdg_toplevel *toplevel,
                                      int32_t width,
                                      int32_t height);
    static void handleWmCapabilities(void *data, xdg_toplevel *toplevel, wl_array *capabilities);

private:
    xdg_surface *m_xdgSurface = nullptr;
    xdg_toplevel *m_xdgToplevel = nullptr;
    QRect m_geometry;
    QSize m_configuredSize;
    bool m_configured = false;
};

class QBarLayerShellPlugin final : public QtWaylandClient::QWaylandShellIntegrationPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.WaylandClient.QWaylandShellIntegrationFactoryInterface.5.3" FILE "qbar-layer-shell.json")

public:
    QtWaylandClient::QWaylandShellIntegration *create(const QString &key, const QStringList &paramList) override;
};
