#pragma once

#include <QObject>
#include <QEvent>
#include <QHash>
#include <QPointer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

class QQmlEngine;
class QWidget;
class QMenu;
class QQuickView;
class QWindow;

class QBarPopupService final : public QObject {
    Q_OBJECT

public:
    explicit QBarPopupService(QQmlEngine *engine,
                              QVariantMap theme,
                              QObject *workspaceModel,
                              QObject *ipcClient,
                              QObject *trayModel,
                              QObject *parent = nullptr);
    ~QBarPopupService() override;

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
    void handleExternalFocusChanged(qint64 containerId);
    void handleContainerFocusEvent(qint64 containerId);
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

    bool eventFilter(QObject *object, QEvent *event) override;
    QString nextId(const QString &requestedId);
    QWidget *popupParent() const;
    QWindow *popupTransientParent() const;
    bool isManagedPopupObject(QObject *object) const;
    void applyPopupContext(QQuickView *view);
    int popupAnimationDuration() const;
    void forceClosePopup(const QString &id);
    void forceCloseTooltip(const QString &id);
    void ensureDismissOverlay();
    void destroyDismissOverlay();
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
    QHash<QString, Popup> m_popups;
    QHash<QString, Popup> m_tooltips;
    QHash<QString, bool> m_tooltipHovered;
    QPointer<QMenu> m_openMenu;
    QPointer<QQuickView> m_dismissOverlay;
    qint64 m_popupFocusBaseline = -1;
    qint64 m_lastPopupFocusEvent = -1;
    bool m_armedForContainerActivation = false;
    int m_nextId = 1;
};
