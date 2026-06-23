#pragma once

#include <QObject>

class TrayTests final : public QObject {
    Q_OBJECT

private slots:
    void watcherExportsOnlyDbusSurface();
};
