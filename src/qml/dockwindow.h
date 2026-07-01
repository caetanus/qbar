#pragma once

#include <QObject>
#include <QPointer>
#include <QRect>
#include <QVariantMap>
#include <QWindow>

class QQmlEngine;
class QQuickView;

// Controller for the macOS-style Dock.
//
// The Dock is its OWN top-level window — a sized layer-shell surface on Wayland, an
// override-redirect window on X11 — NOT a bar applet. A transparent "Dock" proxy
// applet sits in the bar layout, reserves the dock's footprint (so a theme can keep
// the bar centre transparent and still "have" the dock there), and reports its
// on-screen rectangle here via setSlotGeometry(). This controller then floats the
// real dock window over that slot, but TALLER, so the cursor magnification (and other
// effects) can overflow the bar's bounds without being clipped.
//
// The controller is cheap to construct and creates no window until the proxy first
// reports a non-empty slot, so bars without a Dock applet pay nothing.
class DockWindow final : public QObject {
    Q_OBJECT

public:
    DockWindow(QQmlEngine *engine,
               QVariantMap theme,
               QVariantMap dock,
               QObject *windowModel,
               QObject *wm,
               QObject *cssTheme,
               QObject *parent = nullptr);
    ~DockWindow() override;

    // The bar this dock belongs to; the dock mirrors its edge (top/bottom), height
    // and floating margins, and lands on its screen.
    void setBarWindow(QWindow *bar) { m_barWindow = bar; }

    // Called by the proxy applet whenever its on-screen rectangle changes (global
    // coordinates). Lazily builds the dock window and positions it over the slot.
    Q_INVOKABLE void setSlotGeometry(int gx, int gy, int gw, int gh);
    // Proxy gone / reserved width collapsed to 0 → hide the dock window.
    Q_INVOKABLE void hideDock();

    // Live config reload: swap the dock options (magnify/indicator/heights/coverflow…)
    // and re-forward them to the running surface so changes take effect without a restart.
    void setDockConfig(const QVariantMap &dock);

private:
    void ensureView();
    void applyGeometry();   // push slot rect → window placement (layer-shell props / X11 geometry)

    QQmlEngine *m_engine = nullptr;
    QVariantMap m_theme;
    QVariantMap m_dock;     // dock animation options (magnify/indicator/heights)
    QObject *m_windowModel = nullptr;
    QObject *m_wm = nullptr;
    QObject *m_cssTheme = nullptr;
    QPointer<QWindow> m_barWindow;
    QPointer<QQuickView> m_view;
    QRect m_slot;   // proxy rectangle in global coordinates
};
