#include "notificationwindow.h"
#include "notificationmodel.h"
#include "notificationserver.h"
#include "../css/csstheme.h"

#include <QGuiApplication>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickView>
#include <QRegion>
#include <QScreen>
#include <QTimer>

namespace {

bool onX11()
{
    return QGuiApplication::platformName().startsWith(QLatin1String("xcb"));
}

} // namespace

NotificationWindow::NotificationWindow(QQmlEngine *engine,
                                       QVariantMap theme,
                                       QVariantMap config,
                                       NotificationModel *model,
                                       NotificationServer *server,
                                       QObject *cssTheme,
                                       QObject *parent)
    : QObject(parent)
    , m_engine(engine)
    , m_theme(std::move(theme))
    , m_config(std::move(config))
    , m_model(model)
    , m_server(server)
    , m_cssTheme(cssTheme)
{
    connect(m_model, &NotificationModel::countChanged, this, &NotificationWindow::onCountChanged);
    // CSS hot-reload: width/margins live in the theme, so re-derive them on every load.
    if (auto *css = qobject_cast<CssTheme *>(m_cssTheme)) {
        connect(css, &CssTheme::loadedChanged, this, [this]() { applyGeometry(); });
    }
}

NotificationWindow::~NotificationWindow()
{
    if (m_view != nullptr) {
        m_view->close();
        m_view->deleteLater();
    }
}

void NotificationWindow::setBarWindow(QWindow *window)
{
    m_barWindow = window;
}

void NotificationWindow::setNotifConfig(const QVariantMap &config)
{
    if (config == m_config) {
        return;
    }
    m_config = config;
    if (m_view != nullptr) {
        m_view->rootContext()->setContextProperty(QStringLiteral("notifConfig"), m_config);
        applyGeometry();
    }
}

void NotificationWindow::notificationArrived()
{
    ensureView();
    if (m_view != nullptr && !m_view->isVisible()) {
        applyGeometry();
        m_view->show();
    }
}

void NotificationWindow::onCountChanged()
{
    if (m_model->count() > 0) {
        notificationArrived();
        return;
    }
    // Grace period: the last card's exit animation plays inside the (input-free)
    // surface before it hides.
    QTimer::singleShot(700, this, [this]() {
        if (m_model->count() == 0 && m_view != nullptr && m_view->isVisible()) {
            m_view->hide();
        }
    });
}

void NotificationWindow::updateInputRegion()
{
    if (m_view == nullptr) {
        return;
    }
    QQuickItem *root = m_view->rootObject();
    if (root == nullptr) {
        return;
    }
    const int stackHeight = qMax(0, static_cast<int>(root->property("stackHeight").toReal()));
    const int surfaceHeight = qMax(1, m_view->height());
    const bool bottom = m_config.value(QStringLiteral("corner"), QStringLiteral("top-right"))
                            .toString()
                            .startsWith(QLatin1String("bottom"));
    const int inputY = bottom ? surfaceHeight - stackHeight : 0;

    if (onX11()) {
        m_view->setMask(stackHeight > 0 ? QRegion(0, inputY, m_view->width(), stackHeight) : QRegion());
        return;
    }
    // The layer-shell plugin re-applies the wl input region when these change.
    if (m_view->property("qbarNotifInputY").toInt() != inputY
        || m_view->property("qbarNotifInputHeight").toInt() != stackHeight) {
        m_view->setProperty("qbarNotifInputX", 0);
        m_view->setProperty("qbarNotifInputY", inputY);
        m_view->setProperty("qbarNotifInputWidth", m_view->width());
        m_view->setProperty("qbarNotifInputHeight", stackHeight);
    }
}

void NotificationWindow::ensureView()
{
    if (m_view != nullptr || m_engine == nullptr) {
        return;
    }

    auto *view = new QQuickView(m_engine, nullptr);
    view->setTitle(QStringLiteral("QBar Notifications"));
    Qt::WindowFlags flags =
        Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::NoDropShadowWindowHint;
    if (onX11()) {
        flags |= Qt::BypassWindowManagerHint;
    }
    view->setFlags(flags);
    view->setColor(Qt::transparent);
    view->setResizeMode(QQuickView::SizeRootObjectToView);

    // Marks the window for the layer-shell integration (a corner-anchored sized
    // surface), and names its own namespace so compositor rules can target it
    // (e.g. Hyprland `layerrule = blur, qbar-notifications`).
    view->setProperty("qbarNotifications", true);
    view->setProperty("qbarLayerNamespace", QStringLiteral("qbar-notifications"));

    QQmlContext *ctx = view->rootContext();
    ctx->setContextProperty(QStringLiteral("theme"), m_theme);
    ctx->setContextProperty(QStringLiteral("cssTheme"), m_cssTheme);
    ctx->setContextProperty(QStringLiteral("notificationModel"), m_model);
    ctx->setContextProperty(QStringLiteral("notificationServer"), m_server);
    ctx->setContextProperty(QStringLiteral("notifConfig"), m_config);

    QScreen *screen = m_barWindow != nullptr ? m_barWindow->screen() : nullptr;
    if (screen == nullptr) {
        screen = QGuiApplication::primaryScreen();
    }
    if (screen != nullptr) {
        view->setScreen(screen);
    }

    view->setSource(QUrl(QStringLiteral("qrc:/qbar/NotificationSurface.qml")));
    m_view = view;

    if (QQuickItem *root = view->rootObject()) {
        connect(root, SIGNAL(stackHeightChanged()), this, SLOT(updateInputRegion()));
    }
    applyGeometry();
}

void NotificationWindow::applyGeometry()
{
    if (m_view == nullptr) {
        return;
    }

    // CSS `#notifications { width; margin-*; }` wins over the JSON config.
    QVariantMap style;
    if (auto *css = qobject_cast<CssTheme *>(m_cssTheme)) {
        style = css->resolve(QStringLiteral("notifications"));
    }
    const int configWidth = m_config.value(QStringLiteral("width"), 380).toInt();
    const int configMargin = m_config.value(QStringLiteral("margin"), 12).toInt();
    const int width = cssLength(style, QStringLiteral("width"), configWidth);
    const int marginTop = cssLength(style, QStringLiteral("margin-top"), configMargin);
    const int marginBottom = cssLength(style, QStringLiteral("margin-bottom"), configMargin);
    const int marginLeft = cssLength(style, QStringLiteral("margin-left"), configMargin);
    const int marginRight = cssLength(style, QStringLiteral("margin-right"), configMargin);

    const QString corner = m_config.value(QStringLiteral("corner"), QStringLiteral("top-right")).toString();
    const bool bottom = corner.startsWith(QLatin1String("bottom"));
    const bool left = corner.endsWith(QLatin1String("left"));
    const bool center = corner.endsWith(QLatin1String("center"));

    QScreen *screen = m_view->screen();
    if (screen == nullptr) {
        screen = QGuiApplication::primaryScreen();
    }
    const QRect area = screen != nullptr ? screen->availableGeometry() : QRect(0, 0, width, 600);
    const int surfaceHeight = qMax(1, area.height() - marginTop - marginBottom);

    if (onX11()) {
        const int x = center ? area.x() + (area.width() - width) / 2
                             : (left ? area.x() + marginLeft : area.x() + area.width() - width - marginRight);
        m_view->setGeometry(QRect(x, area.y() + marginTop, width, surfaceHeight));
        updateInputRegion();
        return;
    }

    m_view->setProperty("qbarNotifCornerBottom", bottom);
    m_view->setProperty("qbarNotifCornerLeft", left);
    m_view->setProperty("qbarNotifCornerCenter", center);
    m_view->setProperty("qbarNotifWidth", width);
    m_view->setProperty("qbarNotifMarginTop", marginTop);
    m_view->setProperty("qbarNotifMarginRight", marginRight);
    m_view->setProperty("qbarNotifMarginBottom", marginBottom);
    m_view->setProperty("qbarNotifMarginLeft", marginLeft);
    if (m_view->size() != QSize(width, surfaceHeight)) {
        m_view->resize(width, surfaceHeight);
    }
    updateInputRegion();
}

int NotificationWindow::cssLength(const QVariantMap &style, const QString &property, int fallback) const
{
    if (!style.contains(property)) {
        return fallback;
    }
    if (auto *css = qobject_cast<CssTheme *>(m_cssTheme)) {
        return static_cast<int>(css->parseLength(style.value(property).toString(), fallback));
    }
    return fallback;
}
