#include "qbarpopupservice.h"

#include <QAction>
#include <QApplication>
#include <QCursor>
#include <QDebug>
#include <QMouseEvent>
#include <QGuiApplication>
#include <QMenu>
#include <QQuickItem>
#include <QQuickView>
#include <QScreen>
#include <QQmlContext>
#include <QQmlEngine>
#include <QWindow>
#include <QWidget>
#include <QTimer>

namespace {

bool isInObjectTree(QObject *candidate, QObject *root)
{
    for (QObject *object = candidate; object != nullptr; object = object->parent()) {
        if (object == root) {
            return true;
        }
    }
    return false;
}

} // namespace

QBarPopupService::QBarPopupService(QQmlEngine *engine,
                                   QVariantMap theme,
                                   QObject *workspaceModel,
                                   QObject *ipcClient,
                                   QObject *trayModel,
                                   QObject *parent)
    : QObject(parent)
    , m_engine(engine)
    , m_theme(std::move(theme))
    , m_workspaceModel(workspaceModel)
    , m_ipcClient(ipcClient)
    , m_trayModel(trayModel)
{
    qApp->installEventFilter(this);
    connect(qApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
        if (state != Qt::ApplicationActive) {
            closeAll();
        }
    });
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

    auto *view = createPopupView(source,
                                 properties,
                                 id,
                                 Qt::Tool | Qt::WindowStaysOnTopHint | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint,
                                 QStringLiteral("QBar Popup"));
    if (view == nullptr) {
        destroyDismissOverlay();
        return {};
    }

    QObject *popupTarget = view->rootObject();
    if (popupTarget != nullptr) {
        if (QObject *loader = popupTarget->findChild<QObject *>(QStringLiteral("qbarPopupLoader"))) {
            QObject *loaded = loader->property("item").value<QObject *>();
            if (loaded != nullptr) {
                popupTarget = loaded;
            }
        }
    }
    const int popupWidth = width > 0 ? width : (popupTarget != nullptr ? popupTarget->property("implicitWidth").toInt() : 240);
    const int popupHeight = height > 0 ? height : (popupTarget != nullptr ? popupTarget->property("implicitHeight").toInt() : 160);
    view->resize(1, 1);

    if (x == 0 && y == 0) {
        const QPoint position = QCursor::pos();
        x = position.x();
        y = position.y();
    }
    view->setPosition(x, y);

    m_popups.insert(id, Popup{view});
    m_popupFocusBaseline = m_ipcClient != nullptr
        ? m_ipcClient->property("focusedContainerId").toLongLong()
        : -1;
    m_lastPopupFocusEvent = -1;
    m_armedForContainerActivation = false;
    connect(view, &QObject::destroyed, this, [this, id]() {
        m_popups.remove(id);
        if (m_popups.isEmpty()) {
            m_popupFocusBaseline = -1;
            m_lastPopupFocusEvent = -1;
            m_armedForContainerActivation = false;
        }
        emit popupClosed(id);
    });

    view->show();
    view->raise();
    if (auto *root = view->rootObject()) {
        root->setProperty("animateBounds", true);
        root->setProperty("targetWidth", popupWidth);
        root->setProperty("targetHeight", popupHeight);
        root->setProperty("popupClosing", false);
    }
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
    if (!m_popups.contains(id)) {
        qWarning() << "[popup] update ignored (missing id)" << id << properties;
        return;
    }

    const auto popup = m_popups.value(id);
    if (popup.view == nullptr) {
        qWarning() << "[popup] update ignored (missing view)" << id << properties;
        return;
    }

    auto *root = popup.view->rootObject();
    if (root == nullptr) {
        qWarning() << "[popup] update ignored (missing root)" << id << properties;
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
    popup.view->resize(width, height);

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
    const auto popup = m_popups.value(id);
    if (popup.view == nullptr) {
        return;
    }

    if (popup.view->property("_qbarClosing").toBool()) {
        return;
    }

    popup.view->setProperty("_qbarClosing", true);
    if (auto *root = popup.view->rootObject()) {
        root->setProperty("popupClosing", true);
    }
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

    m_popupFocusBaseline = -1;
    m_lastPopupFocusEvent = -1;
    m_armedForContainerActivation = false;
    destroyDismissOverlay();
}

void QBarPopupService::handleExternalFocusChanged(qint64 containerId)
{
    if (m_popups.isEmpty() && m_openMenu == nullptr) {
        m_popupFocusBaseline = containerId;
        return;
    }

    if (containerId < 0) {
        return;
    }

    if (m_popupFocusBaseline < 0) {
        m_popupFocusBaseline = containerId;
        return;
    }

    if (containerId != m_popupFocusBaseline) {
        closeAll();
    }
}

void QBarPopupService::handleContainerFocusEvent(qint64 containerId)
{
    if (m_popups.isEmpty() && m_openMenu == nullptr) {
        m_popupFocusBaseline = containerId;
        m_lastPopupFocusEvent = -1;
        m_armedForContainerActivation = false;
        return;
    }

    if (containerId < 0) {
        return;
    }

    if (!m_armedForContainerActivation) {
        m_lastPopupFocusEvent = containerId;
        m_armedForContainerActivation = true;
        return;
    }

    closeAll();
}

bool QBarPopupService::eventFilter(QObject *object, QEvent *event)
{
    if (event == nullptr) {
        return QObject::eventFilter(object, event);
    }

    switch (event->type()) {
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonDblClick:
    case QEvent::Wheel:
    case QEvent::TouchBegin:
        if (isManagedPopupObject(object)) {
            return QObject::eventFilter(object, event);
        }

        if (!m_popups.isEmpty() || !m_tooltips.isEmpty()) {
            closeAll();
        }
        break;
    default:
        break;
    }

    return QObject::eventFilter(object, event);
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
    const auto popup = m_popups.take(id);
    if (popup.view != nullptr) {
        popup.view->close();
        popup.view->deleteLater();
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

void QBarPopupService::ensureDismissOverlay()
{
    if (m_dismissOverlay != nullptr || m_engine == nullptr) {
        return;
    }

    auto *view = new QQuickView(m_engine, nullptr);
    view->setTitle(QStringLiteral("QBar Popup Dismiss Overlay"));
    view->setFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::NoDropShadowWindowHint);
    view->setColor(Qt::transparent);
    view->setResizeMode(QQuickView::SizeRootObjectToView);
    view->setSource(QUrl(QStringLiteral("qrc:/qbar/DismissOverlay.qml")));

    if (auto *screen = QGuiApplication::primaryScreen()) {
        view->setGeometry(screen->virtualGeometry());
    }

    if (auto *root = view->rootObject()) {
        connect(root, SIGNAL(dismissed()), this, SLOT(closeAll()));
    }

    if (auto *transient = popupTransientParent()) {
        view->setTransientParent(transient);
    }

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

bool QBarPopupService::isManagedPopupObject(QObject *object) const
{
    if (object == nullptr) {
        return false;
    }

    for (const auto &popup : m_popups) {
        if (popup.view != nullptr && (isInObjectTree(object, popup.view) || isInObjectTree(object, popup.view->rootObject()))) {
            return true;
        }
    }

    for (const auto &tooltip : m_tooltips) {
        if (tooltip.view != nullptr && (isInObjectTree(object, tooltip.view) || isInObjectTree(object, tooltip.view->rootObject()))) {
            return true;
        }
    }

    if (m_openMenu != nullptr && isInObjectTree(object, m_openMenu)) {
        return true;
    }

    return false;
}

void QBarPopupService::applyPopupContext(QQuickView *view)
{
    view->rootContext()->setContextProperty(QStringLiteral("theme"), m_theme);
    view->rootContext()->setContextProperty(QStringLiteral("qbarPopups"), this);
    if (m_workspaceModel != nullptr) {
        view->rootContext()->setContextProperty(QStringLiteral("workspaceModel"), m_workspaceModel);
    }
    if (m_ipcClient != nullptr) {
        view->rootContext()->setContextProperty(QStringLiteral("i3Ipc"), m_ipcClient);
    }
    if (m_trayModel != nullptr) {
        view->rootContext()->setContextProperty(QStringLiteral("trayModel"), m_trayModel);
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
    applyPopupContext(view);
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
