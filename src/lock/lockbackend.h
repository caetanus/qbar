#pragma once

#include <QObject>
#include <QString>

class LockBackend : public QObject {
    Q_OBJECT

public:
    explicit LockBackend(QObject *parent = nullptr);
    ~LockBackend() override;

    virtual QString name() const = 0;
    virtual bool isAvailable() const = 0;
    virtual QString unavailableReason() const = 0;

public slots:
    virtual void lock() = 0;
    virtual void unlock() = 0;

signals:
    void locked();
    void lockFailed(const QString &reason);
    void unlocked();
};
