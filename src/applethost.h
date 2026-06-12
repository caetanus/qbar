#pragma once

#include "config.h"

#include <QQuickWidget>
#include <QVariantMap>
#include <QString>

class QBarPopupService;

class AppletHost final : public QQuickWidget {
    Q_OBJECT

public:
    explicit AppletHost(QString appletName,
                        const BarConfig &config,
                        QObject *workspaceModel = nullptr,
                        QObject *cpuModel = nullptr,
                        QObject *temperatureModel = nullptr,
                        QObject *networkModel = nullptr,
                        QObject *networkManagerModel = nullptr,
                        QObject *brightnessModel = nullptr,
                        QObject *caffeineModel = nullptr,
                        QObject *soundModel = nullptr,
                        QObject *ipcClient = nullptr,
                        QObject *trayModel = nullptr,
                        QObject *batteryModel = nullptr,
                        QWidget *parent = nullptr);

    QString appletName() const;

signals:
    void activated(const QString &appletName);
    void workspaceActivated(const QString &workspaceName);
    void workspaceScrolled(int direction);
    void preferredWidthChanged();

private slots:
    void onStatusChanged(QQuickWidget::Status status);
    void connectRootObject();
    void forwardActivation();
    void forwardWorkspaceActivation(const QString &workspaceName);
    void forwardWorkspaceScroll(int direction);
    void applyPreferredWidth(int width);

private:
    QString m_appletName;
    QBarPopupService *m_popupService = nullptr;
    int m_animationDuration = 200;
    QEasingCurve m_animationEasing = QEasingCurve::InOutQuad;
};
