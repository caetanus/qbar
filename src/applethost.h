#pragma once

#include "config.h"

#include <QQuickWidget>
#include <QString>

class AppletHost final : public QQuickWidget {
    Q_OBJECT

public:
    explicit AppletHost(QString appletName, const BarConfig &config, QWidget *parent = nullptr);

    QString appletName() const;

signals:
    void activated(const QString &appletName);

private slots:
    void connectRootObject();
    void forwardActivation();

private:
    QString m_appletName;
};
