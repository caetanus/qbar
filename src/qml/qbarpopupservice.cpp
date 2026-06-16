#include "qbarpopupservice.h"

#include <QAction>
#include <QApplication>
#include <QCursor>
#include <QDebug>
#include <QMouseEvent>
#include <QGuiApplication>
#include <QMenu>
#include <QQmlComponent>
#include <QQuickItem>
#include <QQuickView>
#include <QQuickWindow>
#include <QScreen>
#include <QQmlContext>
#include <QQmlEngine>
#include <QWindow>
#include <QWidget>
#include <QTimer>

QBarPopupService::QBarPopupService(QQmlEngine *engine,
                                   QVariantMap theme,
                                   QObject *workspaceModel,
                                   QObject *ipcClient,
                                   QObject *trayModel,
                                   QObject *cssTheme,
                                   QObject *parent)
    : QObject(parent)
    , m_engine(engine)
    , m_theme(std::move(theme))
    , m_workspaceModel(workspaceModel)
    , m_ipcClient(ipcClient)
    , m_trayModel(trayModel)
    , m_cssTheme(cssTheme)
{
    // Dismissal is owned by the overlay's backdrop (DismissOverlay → closeAll)
    // and by WM workspace-focus changes (wired in BarWindow). No global event
    // filter or app-focus tracking: the overlay is a plain focusless window.
}

QBarPopupService::~QBarPopupService()
{
    closeAll();
}

QString QBarPopupService::openPopup(const QUrl &source,
                                    const QVariantMap &properties,
                                    int x,
                                    int y,
                                    int width,
                                    int height,
                                    const QString &requestedId)
{
    if (!source.isValid() || m_engine == nullptr) {
        return {};
    }

    const QString id = nextId(requestedId);
    forceClosePopup(id);
    ensureDismissOverlay();

    if (m_overlayPopupLayer == nullptr) {
        destroyDismissOverlay();
        return {};
    }

    // One popup at a time: opening a popup dismisses any other open popup. The
    // destroyed handler removes them from m_popups and emits popupClosed; we
    // keep the overlay alive since a new popup is taking their place.
    const auto existingIds = m_popups.keys();
    for (const QString &existingId : existingIds) {
        if (QQuickItem *old = m_popups.value(existingId)) {
            old->deleteLater();
        }
    }

    // Build the popup content (PopupShell + its loaded applet) as a child item
    // of the overlay's popup layer, with a dedicated context carrying the
    // per-popup ids/data on top of the shared theme/model context properties.
    auto *context = new QQmlContext(m_engine->rootContext());
    applyPopupContext(context);
    context->setContextProperty(QStringLiteral("popupId"), id);
    context->setContextProperty(QStringLiteral("popupData"), properties);
    context->setContextProperty(QStringLiteral("contentSource"), source);

    QQmlComponent component(m_engine, popupShellSource());
    auto *shell = qobject_cast<QQuickItem *>(component.create(context));
    if (shell == nullptr) {
        qWarning() << "[popup] failed to create shell" << id << component.errorString();
        delete context;
        if (m_popups.isEmpty()) {
            destroyDismissOverlay();
        }
        return {};
    }
    context->setParent(shell);
    shell->setParentItem(m_overlayPopupLayer);

    QObject *popupTarget = shell;
    if (QObject *loader = shell->findChild<QObject *>(QStringLiteral("qbarPopupLoader"))) {
        if (QObject *loaded = loader->property("item").value<QObject *>()) {
            popupTarget = loaded;
        }
    }
    const int popupWidth = width > 0 ? width : popupTarget->property("implicitWidth").toInt();
    const int popupHeight = height > 0 ? height : popupTarget->property("implicitHeight").toInt();

    if (x == 0 && y == 0) {
        const QPoint position = QCursor::pos();
        x = position.x();
        y = position.y();
    }
    // Args are global screen coordinates; the overlay surface starts at the
    // usable area's top-left (below a top bar), so subtract that origin.
    shell->setX(x - m_overlayOrigin.x());
    if (m_barBottom && m_barWindow != nullptr && m_barWindow->screen() != nullptr) {
        // A bottom bar's window has no reliable global Y on Wayland, so the
        // anchor Y would land the popup near the top of the screen. Pin it to the
        // bar's edge instead: the overlay spans the screen down to the bar's top,
        // so place the popup flush against that bottom edge, growing upward.
        const int overlayHeight = m_barWindow->screen()->geometry().height() - m_barHeight;
        shell->setY(overlayHeight - popupHeight);
    } else {
        shell->setY(y - m_overlayOrigin.y());
    }

    m_popups.insert(id, shell);
    connect(shell, &QObject::destroyed, this, [this, id]() {
        m_popups.remove(id);
        emit popupClosed(id);
    });

    shell->setProperty("animateBounds", true);
    shell->setProperty("targetWidth", popupWidth);
    shell->setProperty("targetHeight", popupHeight);
    shell->setProperty("popupClosing", false);
    return id;
}

QString QBarPopupService::openTooltip(const QUrl &source,
                                      const QVariantMap &properties,
                                      int x,
                                      int y,
                                      int width,
                                      int height,
                                      const QString &requestedId)
{
    if (!source.isValid() || m_engine == nullptr) {
        qWarning() << "[tooltip] open rejected" << source << "engine:" << (m_engine != nullptr);
        return {};
    }

    const QString id = nextId(requestedId);
    forceCloseTooltip(id);
    qWarning() << "[tooltip] open request" << id << source << properties << "pos:" << x << y << "size:" << width << height;

    auto *view = createPopupView(source,
                                 properties,
                                 id,
                                 Qt::ToolTip | Qt::WindowStaysOnTopHint | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint,
                                 QStringLiteral("QBar Tooltip"));
    if (view == nullptr) {
        return {};
    }

    QObject *tooltipTarget = view->rootObject();
    if (tooltipTarget != nullptr) {
        if (QObject *loader = tooltipTarget->findChild<QObject *>(QStringLiteral("qbarPopupLoader"))) {
            QObject *loaded = loader->property("item").value<QObject *>();
            if (loaded != nullptr) {
                tooltipTarget = loaded;
            }
        }
    }
    const int tooltipWidth = width > 0 ? width : (tooltipTarget != nullptr ? tooltipTarget->property("implicitWidth").toInt() : 240);
    const int tooltipHeight = height > 0 ? height : (tooltipTarget != nullptr ? tooltipTarget->property("implicitHeight").toInt() : 160);
    view->resize(tooltipWidth, tooltipHeight);

    if (x == 0 && y == 0) {
        const QPoint position = QCursor::pos();
        x = position.x();
        y = position.y();
    }

    // A tooltip must never sit below the mouse. With a bottom bar the QML anchor
    // places it below the bar — off the usable area, under the cursor — and the
    // bar window has no reliable global Y on Wayland anyway. When it cannot be
    // pushed down, flip it up: anchor it to the bar's top edge and let the popup
    // grow upward (the Wayland positioner flips its gravity for a bottom bar).
    refreshBarGeometry();
    // The Wayland positioner reads these off the popup window to decide which way
    // to grow (a bottom bar grows up); the env alone misreads a config-driven bar.
    view->setProperty("qbarBarPosition", QString::fromLatin1(m_barBottom ? "bottom" : "top"));
    view->setProperty("qbarBarHeight", m_barHeight);
    if (m_barBottom && m_barWindow != nullptr && m_barWindow->screen() != nullptr) {
        const QRect screenGeom = m_barWindow->screen()->geometry();
        const int barTop = screenGeom.y() + screenGeom.height() - m_barHeight;
        y = barTop;
    }

    view->setPosition(x, y);

    m_tooltips.insert(id, Popup{view});
    m_tooltipHovered.insert(id, false);
    qWarning() << "[tooltip] created" << id << "view:" << view;
    connect(view, &QObject::destroyed, this, [this, id]() {
        qWarning() << "[tooltip] destroyed" << id;
        m_tooltips.remove(id);
        m_tooltipHovered.remove(id);
        emit tooltipClosed(id);
    });

    if (auto *root = view->rootObject()) {
        root->setProperty("animateBounds", false);
        root->setProperty("targetWidth", tooltipWidth);
        root->setProperty("targetHeight", tooltipHeight);
        root->setProperty("popupClosing", false);
    }
    view->show();
    view->raise();
    qWarning() << "[tooltip] shown" << id << view->position();
    return id;
}

void QBarPopupService::updatePopup(const QString &id, const QVariantMap &properties)
{
    QQuickItem *shell = m_popups.value(id);
    if (shell == nullptr) {
        qWarning() << "[popup] update ignored (missing id)" << id << properties;
        return;
    }

    QObject *target = shell;
    if (QObject *loader = shell->findChild<QObject *>(QStringLiteral("qbarPopupLoader"))) {
        QObject *loaded = loader->property("item").value<QObject *>();
        if (loaded != nullptr) {
            target = loaded;
        }
    }

    for (auto it = properties.cbegin(); it != properties.cend(); ++it) {
        target->setProperty(it.key().toUtf8().constData(), it.value());
    }

    const int width = qMax(1, target->property("implicitWidth").toInt());
    const int height = qMax(1, target->property("implicitHeight").toInt());
    shell->setProperty("targetWidth", width);
    shell->setProperty("targetHeight", height);

    qWarning() << "[popup] updated" << id << properties << "size:" << width << height;
}

void QBarPopupService::updateTooltip(const QString &id, const QVariantMap &properties)
{
    if (!m_tooltips.contains(id)) {
        qWarning() << "[tooltip] update ignored (missing id)" << id << properties;
        return;
    }

    const auto tooltip = m_tooltips.value(id);
    if (tooltip.view == nullptr) {
        qWarning() << "[tooltip] update ignored (missing view)" << id << properties;
        return;
    }

    auto *root = tooltip.view->rootObject();
    if (root == nullptr) {
        qWarning() << "[tooltip] update ignored (missing root)" << id << properties;
        return;
    }

    QObject *target = root;
    if (QObject *loader = root->findChild<QObject *>(QStringLiteral("qbarPopupLoader"))) {
        QObject *loaded = loader->property("item").value<QObject *>();
        if (loaded != nullptr) {
            target = loaded;
        }
    }

    for (auto it = properties.cbegin(); it != properties.cend(); ++it) {
        target->setProperty(it.key().toUtf8().constData(), it.value());
    }

    const int width = qMax(1, target->property("implicitWidth").toInt());
    const int height = qMax(1, target->property("implicitHeight").toInt());
    tooltip.view->resize(width, height);

    qWarning() << "[tooltip] updated" << id << properties << "size:" << width << height;
}

void QBarPopupService::closePopup(const QString &id)
{
    QQuickItem *shell = m_popups.value(id);
    if (shell == nullptr) {
        return;
    }

    if (shell->property("_qbarClosing").toBool()) {
        return;
    }

    shell->setProperty("_qbarClosing", true);
    shell->setProperty("popupClosing", true);
    QTimer::singleShot(popupAnimationDuration(), this, [this, id]() {
        forceClosePopup(id);
    });
}

void QBarPopupService::closeTooltip(const QString &id)
{
    const auto tooltip = m_tooltips.value(id);
    if (tooltip.view == nullptr) {
        m_tooltipHovered.remove(id);
        return;
    }

    if (tooltip.view->property("_qbarClosing").toBool()) {
        return;
    }

    m_tooltipHovered.remove(id);
    qWarning() << "[tooltip] close" << id;
    tooltip.view->setProperty("_qbarClosing", true);
    if (auto *root = tooltip.view->rootObject()) {
        root->setProperty("popupClosing", true);
    }
    QTimer::singleShot(popupAnimationDuration(), this, [this, id]() {
        forceCloseTooltip(id);
    });
}

void QBarPopupService::setTooltipHovered(const QString &id, bool hovered)
{
    if (!m_tooltips.contains(id)) {
        qWarning() << "[tooltip] hover ignored (missing id)" << id << hovered;
        return;
    }

    const bool previous = m_tooltipHovered.value(id, false);
    if (previous == hovered) {
        qWarning() << "[tooltip] hover unchanged" << id << hovered;
        return;
    }

    m_tooltipHovered.insert(id, hovered);
    qWarning() << "[tooltip] hover changed" << id << hovered;
    emit tooltipHoveredChanged(id, hovered);
}

void QBarPopupService::closeAll()
{
    const auto ids = m_popups.keys();
    for (const QString &id : ids) {
        forceClosePopup(id);
    }

    const auto tooltipIds = m_tooltips.keys();
    for (const QString &id : tooltipIds) {
        forceCloseTooltip(id);
    }

    m_tooltipHovered.clear();

    if (m_openMenu != nullptr) {
        m_openMenu->close();
        m_openMenu->deleteLater();
        m_openMenu = nullptr;
    }

    destroyDismissOverlay();
}

QString QBarPopupService::openMenu(const QVariantList &items, int x, int y, const QString &requestedId)
{
    const QString id = nextId(requestedId);

    if (m_openMenu != nullptr) {
        m_openMenu->close();
        m_openMenu->deleteLater();
    }

    auto *menu = new QMenu(popupParent());
    int index = 0;
    populateMenu(menu, items, id, &index);
    if (menu->actions().isEmpty()) {
        menu->deleteLater();
        return {};
    }

    ensureDismissOverlay();

    connect(menu, &QMenu::aboutToHide, menu, &QMenu::deleteLater);
    connect(menu, &QObject::destroyed, this, [this, menu]() {
        if (m_openMenu == menu) {
            m_openMenu = nullptr;
            if (m_popups.isEmpty()) {
                destroyDismissOverlay();
            }
        }
    });

    if (x == 0 && y == 0) {
        const QPoint position = QCursor::pos();
        x = position.x();
        y = position.y();
    }

    m_openMenu = menu;
    menu->popup(QPoint(x, y));
    return id;
}

QString QBarPopupService::nextId(const QString &requestedId)
{
    if (!requestedId.isEmpty()) {
        return requestedId;
    }

    return QStringLiteral("popup-%1").arg(m_nextId++);
}

int QBarPopupService::popupAnimationDuration() const
{
    const int duration = m_theme.value(QStringLiteral("animationDuration"), 160).toInt();
    return duration > 0 ? duration : 160;
}

void QBarPopupService::forceClosePopup(const QString &id)
{
    QQuickItem *shell = m_popups.take(id);
    if (shell != nullptr) {
        shell->deleteLater();
    }
    if (m_popups.isEmpty() && m_openMenu == nullptr) {
        destroyDismissOverlay();
    }
}

void QBarPopupService::forceCloseTooltip(const QString &id)
{
    const auto tooltip = m_tooltips.take(id);
    m_tooltipHovered.remove(id);
    if (tooltip.view != nullptr) {
        tooltip.view->close();
        tooltip.view->deleteLater();
    }
}

void QBarPopupService::refreshBarGeometry()
{
    // Read the owning bar's edge/height (multi-monitor, top+bottom), falling back
    // to the QBAR_LAYER_* env when there is no bar window. Tooltips need this too,
    // and they never open the dismiss overlay, so keep it standalone.
    const QVariant heightProp = m_barWindow != nullptr ? m_barWindow->property("qbarBarHeight") : QVariant();
    const QVariant positionProp = m_barWindow != nullptr ? m_barWindow->property("qbarBarPosition") : QVariant();
    m_barBottom = positionProp.isValid()
        ? positionProp.toString() == QLatin1String("bottom")
        : qgetenv("QBAR_LAYER_POSITION") == "bottom";
    const int rawBarHeight = heightProp.isValid()
        ? heightProp.toInt()
        : qEnvironmentVariableIntValue("QBAR_LAYER_HEIGHT");
    m_barHeight = rawBarHeight > 0 ? rawBarHeight : 28;
}

void QBarPopupService::ensureDismissOverlay()
{
    if (m_dismissOverlay != nullptr || m_engine == nullptr) {
        return;
    }

    auto *view = new QQuickView(m_engine, nullptr);
    view->setTitle(QStringLiteral("QBar Popup Overlay"));
    view->setFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::NoDropShadowWindowHint);
    view->setColor(Qt::transparent);
    view->setResizeMode(QQuickView::SizeRootObjectToView);
    // Marks this window for the layer-shell integration so it spans the whole
    // output below the bar (not the bar's own 28px geometry). Popups are drawn
    // as child items inside it — no separate per-popup window.
    view->setProperty("qbarOverlay", true);
    view->setProperty("qbarOverlayKeyboard", m_overlayKeyboardFocus);

    // Mirror the owning bar's geometry onto the overlay so the layer-shell
    // integration sizes/anchors the backdrop per-bar (multi-monitor, top+bottom),
    // falling back to the QBAR_LAYER_* env when there is no bar window.
    refreshBarGeometry();
    const QVariant exclusiveProp = m_barWindow != nullptr ? m_barWindow->property("qbarBarExclusive") : QVariant();
    const bool bottomBar = m_barBottom;
    const bool exclusive = exclusiveProp.isValid()
        ? exclusiveProp.toBool()
        : qgetenv("QBAR_LAYER_EXCLUSIVE") != "0";
    const int barHeight = m_barHeight;
    view->setProperty("qbarBarHeight", barHeight);
    view->setProperty("qbarBarPosition", QString::fromLatin1(bottomBar ? "bottom" : "top"));
    view->setProperty("qbarBarExclusive", exclusive);

    applyPopupContext(view->rootContext());
    view->setSource(QUrl(QStringLiteral("qrc:/qbar/DismissOverlay.qml")));

    // Put the overlay on the same screen as its bar so the layer surface targets
    // the right output; fall back to the primary screen.
    QScreen *screen = m_barWindow != nullptr ? m_barWindow->screen() : nullptr;
    if (screen == nullptr) {
        screen = QGuiApplication::primaryScreen();
    }
    if (screen != nullptr) {
        view->setScreen(screen);
        const QRect area = screen->availableGeometry();
        view->setGeometry(area);
        // The overlay layer-surface starts below the bar — its top margin equals
        // the bar height. availableGeometry on Wayland may report (0,0) instead
        // of (0, barHeight), which would add an extra bar-height gap to every
        // popup, so derive the origin from the same per-bar geometry the layer
        // margin uses.
        const int topInset = (!bottomBar && exclusive) ? barHeight : 0;
        m_overlayOrigin = QPoint(area.left(), topInset);
    }

    if (auto *root = view->rootObject()) {
        connect(root, SIGNAL(dismissed()), this, SLOT(closeAll()));
        m_overlayPopupLayer = root->property("popupLayer").value<QQuickItem *>();
    }

    // No transient parent: this is a standalone full-output layer surface, not
    // an xdg_popup anchored to the bar.
    m_dismissOverlay = view;
    view->show();
    view->raise();
}

void QBarPopupService::destroyDismissOverlay()
{
    if (m_dismissOverlay == nullptr) {
        return;
    }

    auto *view = m_dismissOverlay.data();
    m_dismissOverlay = nullptr;
    view->close();
    view->deleteLater();
}

QWidget *QBarPopupService::popupParent() const
{
    auto *active = qobject_cast<QWidget *>(QApplication::activeWindow());
    if (active != nullptr) {
        return active;
    }

    return qobject_cast<QWidget *>(parent());
}

QWindow *QBarPopupService::popupTransientParent() const
{
    if (auto *window = QGuiApplication::focusWindow()) {
        return window;
    }

    return qobject_cast<QWindow *>(parent());
}

void QBarPopupService::applyPopupContext(QQmlContext *context)
{
    context->setContextProperty(QStringLiteral("theme"), m_theme);
    context->setContextProperty(QStringLiteral("qbarPopups"), this);
    if (m_workspaceModel != nullptr) {
        context->setContextProperty(QStringLiteral("workspaceModel"), m_workspaceModel);
    }
    if (m_ipcClient != nullptr) {
        context->setContextProperty(QStringLiteral("i3Ipc"), m_ipcClient);
    }
    if (m_trayModel != nullptr) {
        context->setContextProperty(QStringLiteral("trayModel"), m_trayModel);
    }
    if (m_cssTheme != nullptr) {
        context->setContextProperty(QStringLiteral("cssTheme"), m_cssTheme);
    }
}

QQuickView *QBarPopupService::createPopupView(const QUrl &source,
                                              const QVariantMap &properties,
                                              const QString &id,
                                              Qt::WindowFlags flags,
                                              const QString &titlePrefix)
{
    if (m_engine == nullptr) {
        return nullptr;
    }

    auto *view = new QQuickView(m_engine, nullptr);
    view->setFlags(flags);
    view->setTitle(QStringLiteral("%1 %2").arg(titlePrefix, id));
    view->setColor(Qt::transparent);
    view->setResizeMode(QQuickView::SizeViewToRootObject);
    applyPopupContext(view->rootContext());
    view->rootContext()->setContextProperty(QStringLiteral("popupId"), id);
    view->rootContext()->setContextProperty(QStringLiteral("popupData"), properties);
    view->rootContext()->setContextProperty(QStringLiteral("contentSource"), source);
    view->setSource(popupShellSource());

    if (auto *transient = popupTransientParent()) {
        view->setTransientParent(transient);
    }

    return view;
}

QUrl QBarPopupService::popupShellSource() const
{
    return QUrl(QStringLiteral("qrc:/qbar/PopupShell.qml"));
}

void QBarPopupService::populateMenu(QMenu *menu, const QVariantList &items, const QString &id, int *index)
{
    for (const QVariant &value : items) {
        const QVariantMap item = value.toMap();
        if (item.value(QStringLiteral("visible"), true).toBool() == false) {
            continue;
        }
        if (item.value(QStringLiteral("separator")).toBool()) {
            menu->addSeparator();
            continue;
        }

        const int currentIndex = (*index)++;
        const QString text = item.value(QStringLiteral("text"), item.value(QStringLiteral("label"))).toString();
        const QVariantList children = item.value(QStringLiteral("children")).toList();

        QAction *action = nullptr;
        if (!children.isEmpty()) {
            QMenu *subMenu = menu->addMenu(text);
            action = subMenu->menuAction();
            populateMenu(subMenu, children, id, index);
        } else {
            action = menu->addAction(text);
            connect(action, &QAction::triggered, this, [this, id, currentIndex, item]() {
                emit menuTriggered(id, currentIndex, item);
            });
        }

        action->setEnabled(item.value(QStringLiteral("enabled"), true).toBool());
        action->setCheckable(item.value(QStringLiteral("checkable")).toBool());
        action->setChecked(item.value(QStringLiteral("checked")).toBool());

        const QString iconName = item.value(QStringLiteral("iconName")).toString();
        if (!iconName.isEmpty()) {
            action->setIcon(QIcon::fromTheme(iconName));
        }
    }
}
