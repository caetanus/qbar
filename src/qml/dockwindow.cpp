#include "dockwindow.h"

#include <QGuiApplication>
#include <QQmlContext>
#include <QQmlEngine>
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

    if (onX11()) {
        // The dock behaves like an in-bar applet: the surface covers the bar's slot
        // (so base icons sit IN the bar, vertically centred like any applet) and
        // extends away from the bar by `headroom` so the magnification can overflow
        // ON TOP of the bar without clipping.
        const int top = bottom ? (m_slot.bottom() + 1 - surfaceH) : m_slot.top();
        m_view->setGeometry(QRect(m_slot.x(), top, m_slot.width(), surfaceH));
        return;
    }

    // Wayland: the layer-shell integration reads these and anchors a fixed-size
    // surface to the bar edge + left, offset by qbarDockX. Setting them after the
    // surface exists posts a dynamic-property change the integration re-applies.
    QScreen *screen = m_view->screen();
    const int outputX = screen != nullptr ? screen->geometry().x() : 0;
    m_view->setProperty("qbarDockX", m_slot.x() - outputX);
    m_view->setProperty("qbarDockWidth", m_slot.width());
    m_view->setProperty("qbarDockHeight", surfaceH);
    m_view->resize(m_slot.width(), surfaceH);
}
