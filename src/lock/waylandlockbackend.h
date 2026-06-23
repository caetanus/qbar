#pragma once

#include "lockbackend.h"

class WaylandLockBackend final : public LockBackend {
    Q_OBJECT

public:
    explicit WaylandLockBackend(QObject *parent = nullptr);

    QString name() const override { return QStringLiteral("wayland"); }
    bool isAvailable() const override;
    QString unavailableReason() const override;

public slots:
    void lock() override;
    void unlock() override;
};
