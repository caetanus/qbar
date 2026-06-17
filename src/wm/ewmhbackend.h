#pragma once

#include "windowmanagerbackend.h"
#include "workspacemodel.h"

class EwmhBackend final : public WindowManagerBackend {
    Q_OBJECT

public:
    explicit EwmhBackend(QObject *parent = nullptr);

    QString name() const override;
    WorkspaceModel *workspaceModel() override;
    QString currentWindowTitle() const override;
    QString currentKeyboardLayout() const override;
    qint64 focusedContainerId() const override;

    static bool isAvailable();

public slots:
    void start() override;
    void runCommand(const QString &command) override;
    void activateWorkspace(const QString &workspaceName) override;
    void activateRelativeWorkspace(int direction) override;
    void activateWindow(qint64 id) override;
    void closeWindow(qint64 id) override;
    void cycleKeyboardLayout() override;
    void requestTreeSnapshot() override;

private:
    WorkspaceModel m_workspaceModel;
    QString m_currentWindowTitle;
};
