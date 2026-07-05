#pragma once

#include <QObject>
#include <QEvent>
#include <QHash>
#include <QPoint>
#include <QPointer>
#include <QSet>
#include <QSize>
#include <QUrl>
#include <QWindow>
#include <QVariantList>
#include <QVariantMap>

class QQmlEngine;
class QQmlContext;
class QWidget;
class QMenu;
class QQuickItem;
class QQuickView;

class QBarPopupService final : public QObject {
    Q_OBJECT

public:
    explicit QBarPopupService(QQmlEngine *engine,
                              QVariantMap theme,
                              QObject *workspaceModel,
                              QObject *ipcClient,
                              QObject *cssTheme = nullptr,
                              QObject *parent = nullptr);
    ~QBarPopupService() override;

    // When true, the backdrop overlay grabs the keyboard while open so it can
    // close popups on Escape (BarConfig::popupKeyboardFocus).
    void setOverlayKeyboardFocus(bool on) { m_overlayKeyboardFocus = on; }

    // The bar window this service belongs to. The backdrop overlay derives its
    // screen and below-the-bar inset from it (per-bar geometry via the
    // qbarBar* window properties), so popups land correctly in multi-bar setups.
    void setBarWindow(QWindow *window) { m_barWindow = window; }

    Q_INVOKABLE QString openPopup(const QUrl &source,
                                  const QVariantMap &properties,
                                  int x,
                                  int y,
                                  int width,
                                  int height,
                                  const QString &requestedId = QString());
    Q_INVOKABLE QString openTooltip(const QUrl &source,
                                    const QVariantMap &properties,
                                    int x,
                                    int y,
                                    int width,
                                    int height,
                                    const QString &requestedId = QString());
    Q_INVOKABLE void updatePopup(const QString &id, const QVariantMap &properties);
    Q_INVOKABLE void updateTooltip(const QString &id, const QVariantMap &properties);
    Q_INVOKABLE void closePopup(const QString &id);
    Q_INVOKABLE bool detachPopup(const QString &id);
    Q_INVOKABLE void closeTooltip(const QString &id);
    Q_INVOKABLE void setTooltipHovered(const QString &id, bool hovered);
    Q_INVOKABLE QString openMenu(const QVariantList &items,
                                 int x,
                                 int y,
                                 const QString &requestedId = QString());

public slots:
    Q_INVOKABLE void closeAll();

signals:
    void popupClosed(const QString &id);
    void popupDetached(const QString &id);
    void tooltipClosed(const QString &id);
    void tooltipHoveredChanged(const QString &id, bool hovered);
    void menuTriggered(const QString &id, int index, const QVariantMap &item);

private:
    struct Popup {
        QPointer<QQuickView> view;
        // The position the tooltip was created at (for a bottom bar, the bar's top edge).
        // Resizing a Wayland xdg_popup recreates its surface + re-runs the positioner; on a
        // bottom bar the gravity grows it upward, so re-anchoring must use this STABLE origin
        // rather than the already-flipped window position — otherwise it climbs on each update.
        QPoint anchor;
    };
    struct PopupSpec {
        QUrl source;
        QVariantMap properties;
        QSize size;
        QPoint position;
    };

    struct ParkedShell {
        QPointer<QQuickItem> shell;
        QUrl source;
    };

    QString nextId(const QString &requestedId);
    // Reuse-mode helpers: park a closing shell / revive it on reopen / drop all
    // parked shells (flag turned off at runtime, or overlay must really die).
    void parkShell(const QString &id, QQuickItem *shell);
    QQuickItem *takeParkedShell(const QString &id, const QUrl &source);
    void flushParkedShells();
    QWidget *popupParent() const;
    QWindow *popupTransientParent() const;
    void applyPopupContext(QQmlContext *context);
    int popupAnimationDuration() const;
    void forceClosePopup(const QString &id);
    void forceCloseTooltip(const QString &id);
    void ensureDismissOverlay();
    void applyOverlayGeometry(QQuickView *view);
    void destroyDismissOverlay();
    void trimQmlCacheWhenIdle();
    void refreshBarGeometry();
    QUrl popupShellSource() const;
    QQuickView *createPopupView(const QUrl &source,
                                const QVariantMap &properties,
                                const QString &id,
                                Qt::WindowFlags flags,
                                const QString &titlePrefix,
                                bool detached = false);
    void populateMenu(QMenu *menu, const QVariantList &items, const QString &id, int *index);

    QQmlEngine *m_engine = nullptr;
    QVariantMap m_theme;
    QObject *m_workspaceModel = nullptr;
    QObject *m_ipcClient = nullptr;
    QObject *m_cssTheme = nullptr;
    // Popups are QML items drawn inside the single backdrop overlay window
    // (m_dismissOverlay). Tooltips remain ordinary standalone windows.
    QHash<QString, QPointer<QQuickItem>> m_popups;
    QHash<QString, PopupSpec> m_popupSpecs;
    QHash<QString, ParkedShell> m_parkedShells;
    // Closing a popup parks its shell (hidden) and reopening revives it, and
    // the backdrop overlay is hidden rather than destroyed while shells are
    // parked. This is what stops Qt Quick's per-open graphics pipeline growth:
    // the window-level pipeline cache never evicts pipelines created for layer
    // render targets, so destroy-and-rebuild leaked ~4MB per open/close cycle.
    // Cleared only during teardown, where parking must not outlive the service.
    bool m_reuseEnabled = true;
    // True while openPopup dismisses the previous popup(s) to make room for the
    // incoming one; keeps the synchronous park path from hiding the overlay.
    bool m_switchingPopup = false;
    QHash<QString, QPointer<QQuickView>> m_detachedPopups;
    QSet<QString> m_detachingPopups;
    QHash<QString, Popup> m_tooltips;
    QHash<QString, bool> m_tooltipHovered;
    QPointer<QMenu> m_openMenu;
    QPointer<QQuickView> m_dismissOverlay;
    QPointer<QQuickItem> m_overlayPopupLayer;
    QPoint m_overlayOrigin;
    QPointer<QWindow> m_barWindow;
    bool m_barBottom = false;
    int m_barHeight = 0;
    bool m_overlayKeyboardFocus = false;
    int m_nextId = 1;
};
