#pragma once

#include <QObject>
#include <QPointer>
#include <QVariantMap>

class QQmlEngine;
class QQuickView;
class QWindow;
class NotificationModel;
class NotificationServer;

// The toast surface: a lazily-created, full-height layer-shell strip anchored to a
// screen corner (Wayland; a masked frameless window on X11). Cards are stacked inside
// by NotificationSurface.qml; only the stack's bounding box accepts input, the empty
// rest of the strip passes clicks through. Created by NotificationServer; shown while
// the model has rows (plus a grace period so exit animations finish off-screen).
//
// Geometry is CSS-first: `#notifications { width; margin-*; }` wins over the JSON
// config, mirroring how the bar's floating margin works.
class NotificationWindow final : public QObject {
    Q_OBJECT

public:
    NotificationWindow(QQmlEngine *engine,
                       QVariantMap theme,
                       QVariantMap config,
                       NotificationModel *model,
                       NotificationServer *server,
                       QObject *cssTheme,
                       QObject *parent = nullptr);
    ~NotificationWindow() override;

    void setBarWindow(QWindow *window);
    void setNotifConfig(const QVariantMap &config);

    // Called on every Notify so the surface exists (and re-shows) before the model row
    // lands — the view is created lazily to keep bars without notifications free.
    void notificationArrived();

private slots:
    void onCountChanged();
    void updateInputRegion();
    void updateKeyboard();

private:
    void ensureView();
    void applyGeometry();
    int cssLength(const QVariantMap &style, const QString &property, int fallback) const;

    QQmlEngine *m_engine = nullptr;
    QVariantMap m_theme;
    QVariantMap m_config;
    NotificationModel *m_model = nullptr;
    NotificationServer *m_server = nullptr;
    QObject *m_cssTheme = nullptr;
    QPointer<QQuickView> m_view;
    QPointer<QWindow> m_barWindow;
};
