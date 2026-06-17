#pragma once

#include "windowmanagerbackend.h"
#include "workspacemodel.h"

#include <QByteArray>
#include <QLocalSocket>
#include <QTimer>

class HyprlandBackend final : public WindowManagerBackend {
    Q_OBJECT

public:
    explicit HyprlandBackend(QObject *parent = nullptr);

    QString name() const override;
    WorkspaceModel *workspaceModel() override;
    QString currentWindowTitle() const override;
    QString currentKeyboardLayout() const override;
    qint64 focusedContainerId() const override;

    static bool isAvailable();
    static QString commandSocketPath();
    static QString eventSocketPath();
    static QList<WorkspaceModel::Workspace> parseWorkspaces(const QByteArray &workspacesJson,
                                                            const QByteArray &activeWorkspaceJson,
                                                            const QByteArray &monitorsJson);
    static QString parseActiveWindowTitle(const QByteArray &activeWindowJson);
    static QString parseActiveKeyboardLayout(const QByteArray &devicesJson);
    static QList<WindowModel::Window> parseClients(const QByteArray &clientsJson,
                                                   const QByteArray &monitorsJson,
                                                   qint64 focusedAddress);

public slots:
    void start() override;
    void runCommand(const QString &command) override;
    void activateWorkspace(const QString &workspaceName) override;
    void activateRelativeWorkspace(int direction) override;
    void activateWindow(qint64 id) override;
    void closeWindow(qint64 id) override;
    void cycleKeyboardLayout() override;
    void requestTreeSnapshot() override;

private slots:
    void reconnect();
    void readEventMessages();
    void handleEventDisconnected();

private:
    QByteArray request(const QByteArray &command);
    void connectEventSocket();
    void refreshWorkspaces();
    void refreshActiveWindow();
    void refreshWindows();
    void refreshKeyboardLayout();
    void handleEventLine(const QByteArray &line);
    void setCurrentWindowTitle(const QString &title);
    void setCurrentKeyboardLayout(const QString &layout);
    void setFocusedContainerId(qint64 containerId);

    WorkspaceModel m_workspaceModel;
    QString m_currentWindowTitle;
    QString m_currentKeyboardLayout;
    qint64 m_focusedContainerId = -1;
    QLocalSocket m_eventSocket;
    QByteArray m_eventBuffer;
    QTimer m_reconnectTimer;
};
