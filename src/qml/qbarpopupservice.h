#pragma once

#include <QObject>
#include <QEvent>
#include <QHash>
#include <QPoint>
#include <QPointer>
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
                              QObject *trayModel,
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
    void tooltipClosed(const QString &id);
    void tooltipHoveredChanged(const QString &id, bool hovered);
    void menuTriggered(const QString &id, int index, const QVariantMap &item);

private:
    struct Popup {
        QPointer<QQuickView> view;
    };

    QString nextId(const QString &requestedId);
    QWidget *popupParent() const;
    QWindow *popupTransientParent() const;
    void applyPopupContext(QQmlContext *context);
    int popupAnimationDuration() const;
    void forceClosePopup(const QString &id);
    void forceCloseTooltip(const QString &id);
    void ensureDismissOverlay();
    void destroyDismissOverlay();
    void refreshBarGeometry();
    QUrl popupShellSource() const;
    QQuickView *createPopupView(const QUrl &source,
                                const QVariantMap &properties,
                                const QString &id,
                                Qt::WindowFlags flags,
                                const QString &titlePrefix);
    void populateMenu(QMenu *menu, const QVariantList &items, const QString &id, int *index);

    QQmlEngine *m_engine = nullptr;
    QVariantMap m_theme;
    QObject *m_workspaceModel = nullptr;
    QObject *m_ipcClient = nullptr;
    QObject *m_trayModel = nullptr;
    QObject *m_cssTheme = nullptr;
    // Popups are QML items drawn inside the single backdrop overlay window
    // (m_dismissOverlay). Tooltips remain ordinary standalone windows.
    QHash<QString, QPointer<QQuickItem>> m_popups;
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
