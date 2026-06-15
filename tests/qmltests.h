#pragma once

#include <QObject>

class QQmlEngine;

class QBarQuickTestSetup final : public QObject {
    Q_OBJECT

public:
    QBarQuickTestSetup();

public slots:
    void qmlEngineAvailable(QQmlEngine *engine);
};
