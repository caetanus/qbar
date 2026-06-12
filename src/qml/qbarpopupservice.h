#pragma once

#include <QObject>
#include <QHash>
#include <QPointer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

class QQmlEngine;
class QWidget;
class QMenu;
class QQuickWidget;

class QBarPopupService final : public QObject {
    Q_OBJECT

public:
    explicit QBarPopupService(QQmlEngine *engine,
                              QVariantMap theme,
                              QObject *workspaceModel,
                              QObject *ipcClient,
                              QObject *trayModel,
                              QWidget *parent = nullptr);
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
    Q_INVOKABLE void closeAll();
    Q_INVOKABLE QString openMenu(const QVariantList &items,
                                 int x,
                                 int y,
                                 const QString &requestedId = QString());

signals:
    void popupClosed(const QString &id);
    void tooltipClosed(const QString &id);
    void tooltipHoveredChanged(const QString &id, bool hovered);
    void menuTriggered(const QString &id, int index, const QVariantMap &item);

private:
    struct Popup {
        QPointer<QWidget> frame;
        QPointer<QQuickWidget> view;
    };

    QString nextId(const QString &requestedId);
    QWidget *popupParent() const;
    void applyPopupContext(QQuickWidget *view);
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
    int m_nextId = 1;
};
