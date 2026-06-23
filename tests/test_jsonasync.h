#pragma once

#include <QObject>

class JsonAsyncTests final : public QObject {
    Q_OBJECT

private slots:
    void stringifiesAndParsesOffThread();
};
