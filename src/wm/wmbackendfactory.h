#pragma once

#include <QString>

class QObject;
class WindowManagerBackend;

WindowManagerBackend *createWindowManagerBackend(const QString &backendName, QObject *parent);
