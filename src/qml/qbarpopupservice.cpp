#include "qbarpopupservice.h"

#include <QAction>
#include <QApplication>
#include <QCursor>
#include <QFrame>
#include <QDebug>
#include <QMenu>
#include <QQuickItem>
#include <QQuickWidget>
#include <QQmlContext>
#include <QQmlEngine>
#include <QVBoxLayout>
#include <QWidget>

QBarPopupService::QBarPopupService(QQmlEngine *engine,
                                   QVariantMap theme,
                                   QObject *workspaceModel,
                                   QObject *ipcClient,
                                   QObject *trayModel,
                                   QWidget *parent)
    : QObject(parent)
    , m_engine(engine)
    , m_theme(std::move(theme))
    , m_workspaceModel(workspaceModel)
    , m_ipcClient(ipcClient)
    , m_trayModel(trayModel)
{
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
    closePopup(id);

    auto *frame = new QFrame(popupParent(), Qt::ToolTip | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    frame->setObjectName(QStringLiteral("QBarPopup"));
    frame->setWindowTitle(QStringLiteral("QBar Popup %1").arg(id));
    frame->setAttribute(Qt::WA_TranslucentBackground, true);
    frame->setFocusPolicy(Qt::StrongFocus);
    frame->setStyleSheet(QStringLiteral(
        "QFrame#QBarPopup {"
        "background: %1;"
        "border: 1px solid rgba(255, 255, 255, 48);"
        "border-radius: 2px;"
        "}").arg(m_theme.value(QStringLiteral("background")).toString()));

    auto *layout = new QVBoxLayout(frame);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *view = new QQuickWidget(m_engine, frame);
    applyPopupContext(view);
    view->setResizeMode(QQuickWidget::SizeRootObjectToView);
    view->setClearColor(Qt::transparent);
    view->rootContext()->setContextProperty(QStringLiteral("popupId"), id);
    view->rootContext()->setContextProperty(QStringLiteral("popupData"), properties);
    view->setSource(source);
    layout->addWidget(view);

    if (view->rootObject() != nullptr) {
        for (auto it = properties.cbegin(); it != properties.cend(); ++it) {
            view->rootObject()->setProperty(it.key().toUtf8().constData(), it.value());
        }
    }

    const int popupWidth = width > 0 ? width : (view->rootObject() != nullptr ? view->rootObject()->implicitWidth() : 240);
    const int popupHeight = height > 0 ? height : (view->rootObject() != nullptr ? view->rootObject()->implicitHeight() : 160);
    frame->resize(qMax(1, popupWidth), qMax(1, popupHeight));

    if (x == 0 && y == 0) {
        const QPoint position = QCursor::pos();
        x = position.x();
        y = position.y();
    }
    frame->move(x, y);

    m_popups.insert(id, Popup{frame, view});
    connect(frame, &QObject::destroyed, this, [this, id]() {
        m_popups.remove(id);
        emit popupClosed(id);
    });

    frame->show();
    frame->raise();
    frame->setFocus(Qt::PopupFocusReason);
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
    closeTooltip(id);
    qWarning() << "[tooltip] open request" << id << source << properties << "pos:" << x << y << "size:" << width << height;

    auto *frame = new QFrame(popupParent(), Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    frame->setObjectName(QStringLiteral("QBarTooltip"));
    frame->setWindowTitle(QStringLiteral("QBar Tooltip %1").arg(id));
    frame->setAttribute(Qt::WA_TranslucentBackground, true);
    frame->setAttribute(Qt::WA_ShowWithoutActivating, true);
    frame->setFocusPolicy(Qt::NoFocus);
    frame->setStyleSheet(QStringLiteral(
        "QFrame#QBarTooltip {"
        "background: %1;"
        "border: 1px solid rgba(255, 255, 255, 48);"
        "border-radius: 2px;"
        "}").arg(m_theme.value(QStringLiteral("background")).toString()));

    auto *layout = new QVBoxLayout(frame);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *view = new QQuickWidget(m_engine, frame);
    applyPopupContext(view);
    view->setResizeMode(QQuickWidget::SizeRootObjectToView);
    view->setClearColor(Qt::transparent);
    view->rootContext()->setContextProperty(QStringLiteral("popupId"), id);
    view->rootContext()->setContextProperty(QStringLiteral("popupData"), properties);
    view->setSource(source);
    layout->addWidget(view);

    if (view->rootObject() != nullptr) {
        for (auto it = properties.cbegin(); it != properties.cend(); ++it) {
            view->rootObject()->setProperty(it.key().toUtf8().constData(), it.value());
        }
    }

    const int tooltipWidth = width > 0 ? width : (view->rootObject() != nullptr ? view->rootObject()->implicitWidth() : 240);
    const int tooltipHeight = height > 0 ? height : (view->rootObject() != nullptr ? view->rootObject()->implicitHeight() : 160);
    frame->resize(qMax(1, tooltipWidth), qMax(1, tooltipHeight));

    if (x == 0 && y == 0) {
        const QPoint position = QCursor::pos();
        x = position.x();
        y = position.y();
    }
    frame->move(x, y);

    m_tooltips.insert(id, Popup{frame, view});
    m_tooltipHovered.insert(id, false);
    qWarning() << "[tooltip] created" << id << "frame:" << frame << "view:" << view;
    connect(frame, &QObject::destroyed, this, [this, id]() {
        qWarning() << "[tooltip] destroyed" << id;
        m_tooltips.remove(id);
        m_tooltipHovered.remove(id);
        emit tooltipClosed(id);
    });

    frame->show();
    frame->raise();
    qWarning() << "[tooltip] shown" << id << frame->pos();
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

    for (auto it = properties.cbegin(); it != properties.cend(); ++it) {
        root->setProperty(it.key().toUtf8().constData(), it.value());
    }

    const int width = qMax(1, root->property("implicitWidth").toInt());
    const int height = qMax(1, root->property("implicitHeight").toInt());
    if (popup.frame != nullptr) {
        popup.frame->resize(width, height);
    }

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

    for (auto it = properties.cbegin(); it != properties.cend(); ++it) {
        root->setProperty(it.key().toUtf8().constData(), it.value());
    }

    const int width = qMax(1, root->property("implicitWidth").toInt());
    const int height = qMax(1, root->property("implicitHeight").toInt());
    if (tooltip.frame != nullptr) {
        tooltip.frame->resize(width, height);
    }

    qWarning() << "[tooltip] updated" << id << properties << "size:" << width << height;
}

void QBarPopupService::closePopup(const QString &id)
{
    const auto popup = m_popups.take(id);
    if (popup.frame != nullptr) {
        popup.frame->close();
        popup.frame->deleteLater();
    }
}

void QBarPopupService::closeTooltip(const QString &id)
{
    const auto tooltip = m_tooltips.take(id);
    m_tooltipHovered.remove(id);
    if (tooltip.frame != nullptr) {
        qWarning() << "[tooltip] close" << id;
        tooltip.frame->close();
        tooltip.frame->deleteLater();
    }
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
        closePopup(id);
    }

    const auto tooltipIds = m_tooltips.keys();
    for (const QString &id : tooltipIds) {
        closeTooltip(id);
    }

    m_tooltipHovered.clear();

    if (m_openMenu != nullptr) {
        m_openMenu->close();
        m_openMenu->deleteLater();
        m_openMenu = nullptr;
    }
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

    connect(menu, &QMenu::aboutToHide, menu, &QMenu::deleteLater);
    connect(menu, &QObject::destroyed, this, [this, menu]() {
        if (m_openMenu == menu) {
            m_openMenu = nullptr;
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

QWidget *QBarPopupService::popupParent() const
{
    auto *active = qobject_cast<QWidget *>(QApplication::activeWindow());
    if (active != nullptr) {
        return active;
    }

    return qobject_cast<QWidget *>(parent());
}

void QBarPopupService::applyPopupContext(QQuickWidget *view)
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
