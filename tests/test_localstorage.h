#pragma once

#include <QObject>

class LocalStorageTests final : public QObject {
    Q_OBJECT

private slots:
    void storesUpdatesAndRemovesValues();
    void persistsAcrossInstances();
    void listsAndClearsKeys();
};
