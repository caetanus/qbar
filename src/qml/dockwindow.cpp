#include "dockwindow.h"

#include <QGuiApplication>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickView>
#include <QScreen>
#include <QWindow>

namespace {

bool onX11()
{
    return QGuiApplication::platformName().startsWith(QLatin1String("xcb"));
}

int barHeightOf(QWindow *bar)
{
    const QVariant h = bar != nullptr ? bar->property("qbarBarHeight") : QVariant();
    return h.isValid() ? h.toInt() : 28;
}

bool barIsBottom(QWindow *bar)
{
    const QVariant p = bar != nullptr ? bar->property("qbarBarPosition") : QVariant();
    return p.isValid() && p.toString() == QLatin1String("bottom");
}

// Upward (or downward) overflow room for the magnification/effects, beyond the bar's
// own height. Kept generous and transparent — the surface only paints its icons.
int headroomFor(QWindow *bar)
{
    return std::max(72, barHeightOf(bar) * 2);
}

} // namespace

DockWindow::DockWindow(QQmlEngine *engine,
                       QVariantMap theme,
                       QObject *windowModel,
                       QObject *wm,
                       QObject *cssTheme,
                       QObject *parent)
    : QObject(parent)
    , m_engine(engine)
    , m_theme(std::move(theme))
    , m_windowModel(windowModel)
    , m_wm(wm)
    , m_cssTheme(cssTheme)
{
}

DockWindow::~DockWindow()
{
    if (m_view != nullptr) {
        m_view->close();
        m_view->deleteLater();
    }
}

void DockWindow::setSlotGeometry(int gx, int gy, int gw, int gh)
{
    if (gw <= 0 || gh <= 0) {
        hideDock();
        return;
    }
    m_slot = QRect(gx, gy, gw, gh);
    ensureView();
    applyGeometry();
    if (m_view != nullptr && !m_view->isVisible()) {
        m_view->show();
    }
}

void DockWindow::hideDock()
{
    if (m_view != nullptr && m_view->isVisible()) {
        m_view->hide();
    }
}

void DockWindow::ensureView()
{
    if (m_view != nullptr || m_engine == nullptr) {
        return;
    }

    auto *view = new QQuickView(m_engine, nullptr);
    view->setTitle(QStringLiteral("QBar Dock"));
    Qt::WindowFlags flags =
        Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::NoDropShadowWindowHint;
    if (onX11()) {
        // Override-redirect: the WM won't manage, list, or reposition the dock; we
        // place it ourselves over the reserved slot.
        flags |= Qt::BypassWindowManagerHint;
    }
    view->setFlags(flags);
    view->setColor(Qt::transparent);
    view->setResizeMode(QQuickView::SizeRootObjectToView);

    // Marks this window for the layer-shell integration so it becomes a sized surface
    // anchored over the slot (on the overlay layer, above the bar) instead of a bar
    // strip or an xdg_popup.
    view->setProperty("qbarDock", true);
    // Mirror the owning bar's edge + floating margin so the dock aligns with it.
    if (m_barWindow != nullptr) {
        view->setProperty("qbarBarPosition", m_barWindow->property("qbarBarPosition"));
        view->setProperty("qbarBarHeight", m_barWindow->property("qbarBarHeight"));
        view->setProperty("qbarBarMarginTop", m_barWindow->property("qbarBarMarginTop"));
        view->setProperty("qbarBarMarginBottom", m_barWindow->property("qbarBarMarginBottom"));
    }

    QQmlContext *ctx = view->rootContext();
    ctx->setContextProperty(QStringLiteral("theme"), m_theme);
    ctx->setContextProperty(QStringLiteral("cssTheme"), m_cssTheme);
    ctx->setContextProperty(QStringLiteral("windowModel"), m_windowModel);
    ctx->setContextProperty(QStringLiteral("wm"), m_wm);

    QScreen *screen = m_barWindow != nullptr ? m_barWindow->screen() : nullptr;
    if (screen == nullptr) {
        screen = QGuiApplication::primaryScreen();
    }
    if (screen != nullptr) {
        view->setScreen(screen);
    }

    view->setSource(QUrl(QStringLiteral("qrc:/qbar/DockSurface.qml")));
    m_view = view;
}

void DockWindow::applyGeometry()
{
    if (m_view == nullptr) {
        return;
    }

    const int barH = barHeightOf(m_barWindow);
    const int headroom = headroomFor(m_barWindow);
    const int surfaceH = barH + headroom;
    const bool bottom = barIsBottom(m_barWindow);
    QScreen *screen = m_view->screen();
    if (screen == nullptr) {
        screen = m_barWindow != nullptr ? m_barWindow->screen() : nullptr;
    }
    const QRect screenGeometry = screen != nullptr ? screen->geometry() : QRect(m_slot.x(), m_slot.y(), m_slot.width(), surfaceH);
    const int dockW = std::max(1, screenGeometry.width());
    const int dockX = screenGeometry.x();
    const qreal slotCenterX = m_slot.x() + (m_slot.width() / 2.0) - dockX;

    if (QQuickItem *root = m_view->rootObject()) {
        root->setProperty("slotCenterX", slotCenterX);
        root->setProperty("slotWidth", m_slot.width());
    }

    if (onX11()) {
        // The dock behaves like an in-bar applet: the surface covers the bar's slot
        // (so base icons sit IN the bar, vertically centred like any applet) and
        // extends away from the bar by `headroom` so the hover-grow can overflow ON
        // TOP of the bar without clipping.
        const int top = bottom ? (m_slot.bottom() + 1 - surfaceH) : m_slot.top();
        const QRect geometry(dockX, top, dockW, surfaceH);
        if (m_view->geometry() != geometry) {
            m_view->setGeometry(geometry);
        }
        return;
    }

    // Wayland: keep the layer-shell surface stable across window-list changes. Moving
    // the icon row inside QML avoids a layer-shell reconfigure on every slot-width
    // animation frame, which otherwise shows up as flicker on some compositors.
    if (m_view->property("qbarDockX").toInt() != 0) {
        m_view->setProperty("qbarDockX", 0);
    }
    if (m_view->property("qbarDockWidth").toInt() != dockW) {
        m_view->setProperty("qbarDockWidth", dockW);
    }
    if (m_view->property("qbarDockHeight").toInt() != surfaceH) {
        m_view->setProperty("qbarDockHeight", surfaceH);
    }
    const int edgePadding = std::max(barH * 2, headroom);
    const int inputW = std::max(1, std::min(dockW, m_slot.width() + (2 * edgePadding)));
    const int inputH = std::min(surfaceH, std::max(barH, static_cast<int>(std::round(barH * 2.8))));
    const int inputX = static_cast<int>(std::round(slotCenterX - inputW / 2.0));
    const int inputY = surfaceH - inputH;
    if (m_view->property("qbarDockInputX").toInt() != inputX) {
        m_view->setProperty("qbarDockInputX", inputX);
    }
    if (m_view->property("qbarDockInputY").toInt() != inputY) {
        m_view->setProperty("qbarDockInputY", inputY);
    }
    if (m_view->property("qbarDockInputWidth").toInt() != inputW) {
        m_view->setProperty("qbarDockInputWidth", inputW);
    }
    if (m_view->property("qbarDockInputHeight").toInt() != inputH) {
        m_view->setProperty("qbarDockInputHeight", inputH);
    }
    if (m_view->size() != QSize(dockW, surfaceH)) {
        m_view->resize(dockW, surfaceH);
    }
}
