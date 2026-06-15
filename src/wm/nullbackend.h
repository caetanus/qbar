#pragma once

#include "windowmanagerbackend.h"
#include "workspacemodel.h"

class NullBackend final : public WindowManagerBackend {
    Q_OBJECT

public:
    explicit NullBackend(QObject *parent = nullptr);

    QString name() const override;
    WorkspaceModel *workspaceModel() override;
    QString currentWindowTitle() const override;
    QString currentKeyboardLayout() const override;
    qint64 focusedContainerId() const override;

public slots:
    void start() override;
    void runCommand(const QString &command) override;
    void activateWorkspace(const QString &workspaceName) override;
    void activateRelativeWorkspace(int direction) override;
    void cycleKeyboardLayout() override;

private:
    WorkspaceModel m_workspaceModel;
};
