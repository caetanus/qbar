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

#include <utility>

namespace {
// On X11 we want popup/tooltip/overlay windows to be override-redirect — i3 then
// never manages them: no taskbar entry, no title leaking into the focused-window
// readout, no focus stealing, and (crucially) it never repositions them, so they
// land exactly where we place them. Qt::BypassWindowManagerHint is that flag.
bool onX11()
{
    return QGuiApplication::platformName().startsWith(QLatin1String("xcb"));
}
} // namespace

QBarPopupService::QBarPopupService(QQmlEngine *engine,
                                   QVariantMap theme,
                                   QObject *workspaceModel,
                                   QObject *ipcClient,
                                   QObject *cssTheme,
                                   QObject *parent)
    : QObject(parent)
    , m_engine(engine)
    , m_theme(std::move(theme))
    , m_workspaceModel(workspaceModel)
    , m_ipcClient(ipcClient)
    , m_cssTheme(cssTheme)
{
    // Dismissal is owned by the overlay's backdrop (DismissOverlay → closeAll)
    // and by WM workspace-focus changes (wired in BarWindow). No global event
    // filter or app-focus tracking: the overlay is a plain focusless window.
}

QBarPopupService::~QBarPopupService()
{
    // Real teardown: parking must not keep the overlay window alive past the
    // service (it has no QObject parent). Parked shells die with the view.
    m_reuseEnabled = false;
    m_parkedShells.clear();
    closeAll();
    if (m_dismissOverlay != nullptr) {
        auto *view = m_dismissOverlay.data();
        m_dismissOverlay = nullptr;
        view->close();
        view->deleteLater();
    }
    const auto detached = m_detachedPopups;
    m_detachedPopups.clear();
    for (QQuickView *view : detached) {
        if (view != nullptr) {
            view->close();
            view->deleteLater();
        }
    }
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

    // One popup at a time: opening a popup dismisses any other open popup
    // (including a previous open under this same id). In reuse mode they are
    // parked via forceClosePopup (synchronous, emits popupClosed); otherwise
    // the destroyed handler removes them from m_popups and emits popupClosed.
    // This runs BEFORE ensureDismissOverlay, and m_switchingPopup keeps the
    // synchronous park path from hiding the overlay in between: m_popups is
    // momentarily empty here, and a popup revived into a hidden overlay would
    // be invisible while the applet believes it is open.
    m_switchingPopup = true;
    forceClosePopup(id);
    const auto existingIds = m_popups.keys();
    for (const QString &existingId : existingIds) {
        if (m_reuseEnabled) {
            forceClosePopup(existingId);
        } else if (QQuickItem *old = m_popups.value(existingId)) {
            old->deleteLater();
        }
    }
    m_switchingPopup = false;

    ensureDismissOverlay();

    if (m_overlayPopupLayer == nullptr) {
        destroyDismissOverlay();
        return {};
    }

    if (m_dismissOverlay != nullptr) {
        m_dismissOverlay->setProperty(
            "qbarOverlayKeyboard",
            m_overlayKeyboardFocus || properties.value(QStringLiteral("keyboardFocus")).toBool());
    }

    // Reuse mode: revive the parked shell for this popup id instead of building
    // a new one. Same items in the same living window means the renderer reuses
    // the pipelines it already cached instead of growing the cache every open.
    QQuickItem *shell = m_reuseEnabled ? takeParkedShell(id, source) : nullptr;
    if (shell != nullptr) {
        // The per-popup context was parented to the shell at creation; refresh
        // the per-open payload on it, then push it onto the loaded content.
        if (auto *context = shell->findChild<QQmlContext *>(QString(), Qt::FindDirectChildrenOnly)) {
            context->setContextProperty(QStringLiteral("popupData"), properties);
        }
        shell->setParentItem(m_overlayPopupLayer);
        shell->setVisible(true);
        QMetaObject::invokeMethod(shell, "reapplyPopupData");
    } else {
        // Build the popup content (PopupShell + its loaded applet) as a child item
        // of the overlay's popup layer, with a dedicated context carrying the
        // per-popup ids/data on top of the shared theme/model context properties.
        auto *context = new QQmlContext(m_engine->rootContext());
        applyPopupContext(context);
        context->setContextProperty(QStringLiteral("popupId"), id);
        context->setContextProperty(QStringLiteral("popupData"), properties);
        context->setContextProperty(QStringLiteral("contentSource"), source);
        context->setContextProperty(QStringLiteral("detachedWindow"), false);

        QQmlComponent component(m_engine, popupShellSource());
        shell = qobject_cast<QQuickItem *>(component.create(context));
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

        connect(shell, &QObject::destroyed, this, [this, id, shell]() {
            // Died while parked (reuse flag turned off, overlay teardown): just
            // drop the parking entry — popupClosed was already emitted at park.
            auto parkedIt = m_parkedShells.find(id);
            if (parkedIt != m_parkedShells.end()
                && (parkedIt->shell.isNull() || parkedIt->shell.data() == shell)) {
                m_parkedShells.erase(parkedIt);
                return;
            }
            if (m_detachingPopups.remove(id)) {
                // Let the scene graph retire the old popup item before tearing down its
                // Wayland overlay surface. Closing both in the same event-loop turn can
                // leave a frame callback pointing at the retired shell surface.
                QTimer::singleShot(50, this, [this]() {
                    if (m_popups.isEmpty() && m_openMenu == nullptr)
                        destroyDismissOverlay();
                });
                return;
            }
            if (m_popups.value(id) == shell) {
                m_popups.remove(id);
                m_popupSpecs.remove(id);
            }
            emit popupClosed(id);
        });
    }

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
    // x,y are global screen coordinates. Compute the target in global coords —
    // clamped to the usable area (the screen minus THIS bar's reserved strip) and
    // to the screen edges — then convert to overlay-local by subtracting the
    // overlay origin. This is robust on i3/X11 (where availableGeometry doesn't
    // exclude the bar) and on Wayland alike, since the bar gap is derived from the
    // known bar height instead of the WM's work area.
    QScreen *screen = m_barWindow != nullptr ? m_barWindow->screen() : nullptr;
    const QRect screenRect = screen != nullptr ? screen->geometry() : QRect(0, 0, popupWidth, popupHeight);
    const int usableTop = m_barBottom ? screenRect.top() : screenRect.top() + m_barHeight;
    const int usableBottom = m_barBottom ? screenRect.bottom() + 1 - m_barHeight : screenRect.bottom() + 1;

    const int gx = qBound(screenRect.left(),
                          x,
                          qMax(screenRect.left(), screenRect.left() + screenRect.width() - popupWidth));
    // A bottom bar grows its popups upward, flush against the bar's top edge; a top
    // bar drops them down from the anchor. Either way they stay inside the usable band.
    int gy = m_barBottom ? usableBottom - popupHeight
                         : qBound(usableTop, y, qMax(usableTop, usableBottom - popupHeight));
    gy = qBound(usableTop, gy, qMax(usableTop, usableBottom - popupHeight));

    shell->setX(gx - m_overlayOrigin.x());
    shell->setY(gy - m_overlayOrigin.y());

    m_popups.insert(id, shell);
    m_popupSpecs.insert(id, PopupSpec{source, properties, QSize(popupWidth, popupHeight), QPoint(x, y)});

    shell->setProperty("animateBounds", true);
    shell->setProperty("targetWidth", popupWidth);
    shell->setProperty("targetHeight", popupHeight);
    shell->setProperty("_qbarClosing", false);
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

    Qt::WindowFlags tooltipFlags =
        Qt::ToolTip | Qt::WindowStaysOnTopHint | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint;
    if (onX11()) {
        tooltipFlags |= Qt::BypassWindowManagerHint;  // i3 must not manage/list/move it
    }
    auto *view = createPopupView(source,
                                 properties,
                                 id,
                                 tooltipFlags,
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

    // Tooltips are standalone windows placed in global coordinates. Keep them inside
    // the screen and clear of THIS bar: under a top bar they hang below it, under a
    // bottom bar they sit fully above it (bottom edge at the bar's top — the old code
    // anchored the *top* there, so the tooltip landed on the bar).
    QScreen *tipScreen = m_barWindow != nullptr ? m_barWindow->screen() : QGuiApplication::primaryScreen();
    if (tipScreen != nullptr) {
        const QRect s = tipScreen->geometry();
        const int usableTop = m_barBottom ? s.top() : s.top() + m_barHeight;
        const int usableBottom = m_barBottom ? s.bottom() + 1 - m_barHeight : s.bottom() + 1;
        x = qBound(s.left(), x, qMax(s.left(), s.left() + s.width() - tooltipWidth));
        if (m_barBottom) {
            y = usableBottom - tooltipHeight;
        }
        y = qBound(usableTop, y, qMax(usableTop, usableBottom - tooltipHeight));
    }

    view->setPosition(x, y);

    m_tooltips.insert(id, Popup{view, QPoint(x, y)});
    m_tooltipHovered.insert(id, false);
    connect(view, &QObject::destroyed, this, [this, id]() {
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
    return id;
}

void QBarPopupService::updatePopup(const QString &id, const QVariantMap &properties)
{
    if (QQuickView *view = m_detachedPopups.value(id)) {
        QObject *target = view->rootObject();
        if (target != nullptr) {
            if (QObject *loader = target->findChild<QObject *>(QStringLiteral("qbarPopupLoader"))) {
                if (QObject *loaded = loader->property("item").value<QObject *>())
                    target = loaded;
            }
            for (auto it = properties.cbegin(); it != properties.cend(); ++it)
                target->setProperty(it.key().toUtf8().constData(), it.value());
        }
        if (m_popupSpecs.contains(id))
            m_popupSpecs[id].properties = properties;
        return;
    }

    QQuickItem *shell = m_popups.value(id);
    if (shell == nullptr) {
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
    if (m_popupSpecs.contains(id))
        m_popupSpecs[id].properties = properties;

    const int width = qMax(1, target->property("implicitWidth").toInt());
    const int height = qMax(1, target->property("implicitHeight").toInt());
    shell->setProperty("targetWidth", width);
    shell->setProperty("targetHeight", height);
}

void QBarPopupService::updateTooltip(const QString &id, const QVariantMap &properties)
{
    if (!m_tooltips.contains(id)) {
        return;
    }

    const auto tooltip = m_tooltips.value(id);
    if (tooltip.view == nullptr) {
        return;
    }

    auto *root = tooltip.view->rootObject();
    if (root == nullptr) {
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
    // Re-anchor to the STABLE creation origin first: resizing recreates the xdg_popup and
    // re-runs the positioner, and on a bottom bar (gravity grows upward) anchoring off the
    // already-flipped window position would make the tooltip climb on every rewrite.
    tooltip.view->setPosition(tooltip.anchor);
    tooltip.view->resize(width, height);
}

void QBarPopupService::closePopup(const QString &id)
{
    if (QQuickView *view = m_detachedPopups.value(id)) {
        view->close();
        view->deleteLater();
        return;
    }
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
        // Popup ids are stable across opens, so this deferred close can fire
        // AFTER the popup was already reopened (reuse revives it under the
        // same id). Only finish the close if it is still closing.
        QQuickItem *current = m_popups.value(id);
        if (current != nullptr && !current->property("_qbarClosing").toBool()) {
            return;
        }
        forceClosePopup(id);
    });
}

bool QBarPopupService::detachPopup(const QString &id)
{
    QQuickItem *shell = m_popups.value(id);
    if (shell == nullptr || !m_popupSpecs.contains(id) || m_engine == nullptr)
        return m_detachedPopups.contains(id);

    const PopupSpec spec = m_popupSpecs.value(id);
    const bool wayland = QGuiApplication::platformName().contains(QStringLiteral("wayland"), Qt::CaseInsensitive);
    const Qt::WindowFlags detachedType = wayland ? Qt::Window : Qt::Tool;
    auto *view = createPopupView(
        spec.source,
        spec.properties,
        id,
        detachedType | Qt::WindowTitleHint | Qt::WindowSystemMenuHint
            | Qt::WindowMinMaxButtonsHint | Qt::WindowCloseButtonHint,
        QStringLiteral("QBar Detached"),
        true);
    if (view == nullptr)
        return false;

    // A widget can supply a friendlier detached-window title via QBar.Popup.windowTitle
    // (carried in the payload); otherwise the generic "QBar Detached <id>" set above stands.
    const QString detachedTitle = spec.properties.value(QStringLiteral("windowTitle")).toString();
    if (!detachedTitle.isEmpty())
        view->setTitle(detachedTitle);

    view->setTransientParent(nullptr);
    view->setResizeMode(QQuickView::SizeRootObjectToView);
    view->resize(qMax(1, spec.size.width()), qMax(1, spec.size.height()));
    QPoint position = spec.position;
    if (QScreen *screen = m_barWindow != nullptr ? m_barWindow->screen() : QGuiApplication::primaryScreen()) {
        const QRect area = screen->availableGeometry();
        position.setX(area.x() + qMax(0, (area.width() - view->width()) / 2));
        position.setY(area.y() + qMax(0, (area.height() - view->height()) / 2));
    }
    view->setPosition(position);

    if (QObject *root = view->rootObject()) {
        root->setProperty("animateBounds", false);
        root->setProperty("targetWidth", spec.size.width());
        root->setProperty("targetHeight", spec.size.height());
        root->setProperty("popupClosing", false);
    }

    m_detachedPopups.insert(id, view);
    connect(view, &QQuickWindow::closing, view, [view](QQuickCloseEvent *) {
        view->deleteLater();
    });
    connect(view, &QObject::destroyed, this, [this, id, view]() {
        if (m_detachedPopups.value(id) == view) {
            m_detachedPopups.remove(id);
            m_popupSpecs.remove(id);
        }
    });

    m_detachingPopups.insert(id);
    m_popups.take(id);
    shell->deleteLater();

    view->show();
    view->raise();
    view->requestActivate();
    emit popupDetached(id);
    // The anchored popup lifecycle ends at detach time. The independent window
    // keeps its own internal id but no longer occupies QBar.Popup.popupId.
    emit popupClosed(id);
    return true;
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
        return;
    }

    const bool previous = m_tooltipHovered.value(id, false);
    if (previous == hovered) {
        return;
    }

    m_tooltipHovered.insert(id, hovered);
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
        if (m_reuseEnabled) {
            parkShell(id, shell);
            m_popupSpecs.remove(id);
            // The shell stays alive, so its destroyed handler won't fire —
            // announce the close ourselves.
            emit popupClosed(id);
        } else {
            shell->deleteLater();
        }
    }
    if (m_popups.isEmpty() && m_openMenu == nullptr) {
        destroyDismissOverlay();
    }
}

void QBarPopupService::setReuseEnabled(bool on)
{
    if (m_reuseEnabled == on) {
        return;
    }
    m_reuseEnabled = on;
    if (!on) {
        flushParkedShells();
        if (m_popups.isEmpty() && m_openMenu == nullptr) {
            destroyDismissOverlay();
        }
    }
}

void QBarPopupService::parkShell(const QString &id, QQuickItem *shell)
{
    // A parked shell keeps its items — and therefore the graphics pipelines the
    // renderer cached for them — alive, so the next open creates nothing new.
    // Hidden items cost no per-frame work. popupClosing puts the shell in its
    // visually-closed rest state so reviving replays the grow-in animation.
    shell->setProperty("_qbarClosing", false);
    shell->setProperty("popupClosing", true);
    shell->setVisible(false);
    const ParkedShell previous = m_parkedShells.take(id);
    if (previous.shell != nullptr && previous.shell.data() != shell) {
        disconnect(previous.shell.data(), &QObject::destroyed, this, nullptr);
        previous.shell->deleteLater();
    }
    m_parkedShells.insert(id, ParkedShell{shell, m_popupSpecs.value(id).source});
    // Safety valve: parked shells are live item trees. Ids are stable per
    // popup source, so this map should stay small — if some caller cycles
    // through generated ids anyway, evict rather than accumulate.
    while (m_parkedShells.size() > 8) {
        auto victim = m_parkedShells.end();
        for (auto it = m_parkedShells.begin(); it != m_parkedShells.end(); ++it) {
            if (it->shell.data() != shell) {
                victim = it;
                break;
            }
        }
        if (victim == m_parkedShells.end()) {
            break;
        }
        if (victim->shell != nullptr) {
            disconnect(victim->shell.data(), &QObject::destroyed, this, nullptr);
            victim->shell->deleteLater();
        }
        m_parkedShells.erase(victim);
    }
}

QQuickItem *QBarPopupService::takeParkedShell(const QString &id, const QUrl &source)
{
    auto it = m_parkedShells.find(id);
    if (it == m_parkedShells.end()) {
        return nullptr;
    }
    const ParkedShell parked = *it;
    m_parkedShells.erase(it);
    if (parked.shell == nullptr) {
        return nullptr;
    }
    if (parked.source != source) {
        // Same popup id reopened with different content — rebuild from scratch.
        // Disconnect first: the destroyed handler would otherwise emit a
        // spurious popupClosed for the replacement popup now holding this id.
        disconnect(parked.shell.data(), &QObject::destroyed, this, nullptr);
        parked.shell->deleteLater();
        return nullptr;
    }
    return parked.shell.data();
}

void QBarPopupService::flushParkedShells()
{
    const auto parked = std::exchange(m_parkedShells, {});
    for (const ParkedShell &entry : parked) {
        if (entry.shell != nullptr) {
            // popupClosed was already emitted when the shell was parked;
            // disconnect so its destruction stays silent.
            disconnect(entry.shell.data(), &QObject::destroyed, this, nullptr);
            entry.shell->deleteLater();
        }
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
    if (m_engine == nullptr) {
        return;
    }
    if (m_dismissOverlay != nullptr) {
        if (!m_dismissOverlay->isVisible()) {
            // Parked overlay (reuse mode): revive it. Re-derive screen/geometry —
            // both can have changed while it sat hidden.
            QScreen *screen = m_barWindow != nullptr ? m_barWindow->screen()
                                                     : QGuiApplication::primaryScreen();
            if (screen != nullptr && m_dismissOverlay->screen() != screen) {
                // The bar moved to another output; the parked scene targets the
                // old one. Drop everything and rebuild below.
                flushParkedShells();
                auto *stale = m_dismissOverlay.data();
                m_dismissOverlay = nullptr;
                m_overlayPopupLayer = nullptr;
                stale->close();
                stale->deleteLater();
            } else {
                applyOverlayGeometry(m_dismissOverlay.data());
                m_dismissOverlay->show();
                m_dismissOverlay->raise();
                return;
            }
        } else {
            return;
        }
    }

    auto *view = new QQuickView(m_engine, nullptr);
    view->setTitle(QStringLiteral("QBar Popup Overlay"));
    Qt::WindowFlags overlayFlags =
        Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::NoDropShadowWindowHint;
    if (onX11()) {
        // Override-redirect: i3 won't manage, list, or reposition the overlay.
        overlayFlags |= Qt::BypassWindowManagerHint;
    }
    view->setFlags(overlayFlags);
    view->setColor(Qt::transparent);
    view->setResizeMode(QQuickView::SizeRootObjectToView);
    // Marks this window for the layer-shell integration so it spans the whole
    // output below the bar (not the bar's own 28px geometry). Popups are drawn
    // as child items inside it — no separate per-popup window.
    view->setProperty("qbarOverlay", true);
    view->setProperty("qbarOverlayKeyboard", m_overlayKeyboardFocus);

    if (m_reuseEnabled) {
        // Parked shells live inside this window's scene graph; hide/show must
        // not tear it down or their cached nodes/pipelines would be recreated
        // (and the pipeline cache regrown) on every reopen.
        view->setPersistentSceneGraph(true);
        view->setPersistentGraphics(true);
    }

    applyOverlayGeometry(view);

    applyPopupContext(view->rootContext());
    view->setSource(QUrl(QStringLiteral("qrc:/qbar/DismissOverlay.qml")));

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

// Mirror the owning bar's geometry onto the overlay so the layer-shell
// integration sizes/anchors the backdrop per-bar (multi-monitor, top+bottom),
// falling back to the QBAR_LAYER_* env when there is no bar window. Runs at
// creation and again whenever a parked overlay is revived.
void QBarPopupService::applyOverlayGeometry(QQuickView *view)
{
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

    // Put the overlay on the same screen as its bar so the layer surface targets
    // the right output; fall back to the primary screen.
    QScreen *screen = m_barWindow != nullptr ? m_barWindow->screen() : nullptr;
    if (screen == nullptr) {
        screen = QGuiApplication::primaryScreen();
    }
    if (screen != nullptr) {
        view->setScreen(screen);
        if (onX11()) {
            // i3's _NET_WORKAREA doesn't reliably exclude the bar (unlike a Wayland
            // layer-shell exclusive zone), so availableGeometry() ≈ the full screen.
            // Cover the whole screen and place popups in plain global coordinates;
            // the bar gap is enforced per-popup against the known bar height instead.
            const QRect full = screen->geometry();
            view->setGeometry(full);
            m_overlayOrigin = full.topLeft();
        } else {
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
    }
}

void QBarPopupService::destroyDismissOverlay()
{
    if (m_dismissOverlay == nullptr) {
        trimQmlCacheWhenIdle();
        return;
    }

    if (m_reuseEnabled) {
        // Mid-switch (openPopup parking the previous popup before showing the
        // next): keep the overlay up — hiding would strand the incoming popup
        // in an invisible window and churn the layer surface for nothing.
        if (m_switchingPopup) {
            return;
        }
        // Park the overlay instead of destroying it. Destroying wouldn't return
        // the pipeline-cache memory anyway (measured — the retention survives
        // window death), and keeping the window alive is what lets the parked
        // shells reuse their scene-graph resources on the next open.
        m_dismissOverlay->hide();
        return;
    }

    auto *view = m_dismissOverlay.data();
    m_dismissOverlay = nullptr;
    view->releaseResources();
    view->close();
    view->deleteLater();
    trimQmlCacheWhenIdle();
}

void QBarPopupService::trimQmlCacheWhenIdle()
{
    if (m_engine == nullptr || !m_popups.isEmpty() || m_openMenu != nullptr) {
        return;
    }

    QTimer::singleShot(0, this, [this]() {
        if (m_engine != nullptr && m_popups.isEmpty() && m_openMenu == nullptr) {
            m_engine->collectGarbage();
            m_engine->clearComponentCache();
        }
    });
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
    // A tooltip belongs to its bar — the applet that triggered it lives there — so parent it to
    // the bar window (its layer surface). NOT the focused window: a detached popup calls
    // requestActivate() on detach and becomes the focus window, so using focusWindow() here
    // made the tooltip an xdg-popup of the detached window and rendered into it, replacing its
    // content. Fall back to focus/parent only when there is no bar (e.g. a standalone process).
    if (m_barWindow != nullptr) {
        return m_barWindow;
    }
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
    if (m_cssTheme != nullptr) {
        context->setContextProperty(QStringLiteral("cssTheme"), m_cssTheme);
    }
}

QQuickView *QBarPopupService::createPopupView(const QUrl &source,
                                              const QVariantMap &properties,
                                              const QString &id,
                                              Qt::WindowFlags flags,
                                              const QString &titlePrefix,
                                              bool detached)
{
    if (m_engine == nullptr) {
        return nullptr;
    }

    auto *view = new QQuickView(m_engine, nullptr);
    view->setFlags(flags);
    view->setProperty("qbarDetachedPopup", detached);
    view->setTitle(QStringLiteral("%1 %2").arg(titlePrefix, id));
    view->setColor(Qt::transparent);
    view->setResizeMode(QQuickView::SizeViewToRootObject);

    // Each popup view needs its OWN context for the per-popup properties. A QQuickView built on a
    // SHARED QQmlEngine uses engine->rootContext() AS its root context, so setting contentSource/
    // popupData on view->rootContext() clobbers the GLOBAL context: opening a tooltip would
    // overwrite the contentSource a detached popup window is bound to, swapping the detached
    // window's content for the tooltip's (confirmed by screenshot). Instantiate the shell into a
    // dedicated child context (like openPopup does) so each view's properties are isolated.
    auto *context = new QQmlContext(m_engine->rootContext(), view);
    applyPopupContext(context);
    context->setContextProperty(QStringLiteral("popupId"), id);
    context->setContextProperty(QStringLiteral("popupData"), properties);
    context->setContextProperty(QStringLiteral("contentSource"), source);
    context->setContextProperty(QStringLiteral("detachedWindow"), detached);

    auto *component = new QQmlComponent(m_engine, popupShellSource(), view);
    auto *rootItem = qobject_cast<QQuickItem *>(component->create(context));
    if (rootItem == nullptr) {
        qWarning() << "[popup] failed to create view shell" << id << component->errorString();
        delete view;
        return nullptr;
    }
    view->setContent(popupShellSource(), component, rootItem);

    if (!detached) {
        if (auto *transient = popupTransientParent()) {
            view->setTransientParent(transient);
        }
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
