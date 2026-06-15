#include "nullbackend.h"

NullBackend::NullBackend(QObject *parent)
    : WindowManagerBackend(parent)
{
}

QString NullBackend::name() const
{
    return QStringLiteral("none");
}

WorkspaceModel *NullBackend::workspaceModel()
{
    return &m_workspaceModel;
}

QString NullBackend::currentWindowTitle() const
{
    return {};
}

QString NullBackend::currentKeyboardLayout() const
{
    return {};
}

qint64 NullBackend::focusedContainerId() const
{
    return -1;
}

void NullBackend::start()
{
}

void NullBackend::runCommand(const QString &command)
{
    Q_UNUSED(command)
}

void NullBackend::activateWorkspace(const QString &workspaceName)
{
    Q_UNUSED(workspaceName)
}

void NullBackend::activateRelativeWorkspace(int direction)
{
    Q_UNUSED(direction)
}

void NullBackend::cycleKeyboardLayout()
{
}
