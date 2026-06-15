#pragma once

#include "windowmanagerbackend.h"
#include "workspacemodel.h"

#include <QProcess>
#include <QStringList>

class BspwmBackend final : public WindowManagerBackend {
    Q_OBJECT

public:
    explicit BspwmBackend(QObject *parent = nullptr);

    QString name() const override;
    WorkspaceModel *workspaceModel() override;
    QString currentWindowTitle() const override;
    QString currentKeyboardLayout() const override;
    qint64 focusedContainerId() const override;

    static bool isAvailable();
    static QList<WorkspaceModel::Workspace> parseDesktopState(const QStringList &desktopNames,
                                                              const QString &focusedDesktop,
                                                              const QStringList &occupiedDesktops,
                                                              const QStringList &urgentDesktops);
    static QString parseFocusedNodeTitle(const QByteArray &nodeJson);

public slots:
    void start() override;
    void runCommand(const QString &command) override;
    void activateWorkspace(const QString &workspaceName) override;
    void activateRelativeWorkspace(int direction) override;
    void cycleKeyboardLayout() override;
    void requestTreeSnapshot() override;

private slots:
    void readSubscriptionEvents();
    void restartSubscription();

private:
    static QStringList lines(const QByteArray &data);
    static QByteArray runBspc(const QStringList &arguments, int timeoutMs = 500);

    void refreshWorkspaces();
    void refreshActiveWindow();
    void startSubscription();
    void setCurrentWindowTitle(const QString &title);

    WorkspaceModel m_workspaceModel;
    QString m_currentWindowTitle;
    QProcess m_subscribeProcess;
};
