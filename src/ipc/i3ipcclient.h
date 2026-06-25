#pragma once

#include <QByteArray>
#include <QLocalSocket>
#include <QStringList>
#include <QTimer>

#include "../wm/windowmanagerbackend.h"
#include "../wm/workspacemodel.h"

// X11-only; on i3 it feeds the keyboard layout the IPC can't provide. Pointer stays
// null on sway / non-X11 builds, so only a forward declaration is needed here.
class X11KeyboardLayout;

class I3IpcClient final : public WindowManagerBackend {
    Q_OBJECT

public:
    explicit I3IpcClient(QObject *parent = nullptr);

    QString name() const override;
    WorkspaceModel *workspaceModel() override;
    QString currentWindowTitle() const override;
    QString currentKeyboardLayout() const override;
    qint64 focusedContainerId() const override;
    QString bindingMode() const override;
    int scratchpadCount() const override;

public slots:
    void start() override;
    void runCommand(const QString &command) override;
    void activateWorkspace(const QString &workspaceName) override;
    void activateRelativeWorkspace(int direction) override;
    void activateWindowByPid(qint64 pid) override;
    void activateWindow(qint64 id) override;
    void closeWindow(qint64 id) override;
    void cycleKeyboardLayout() override;
    void requestTreeSnapshot() override;

    static bool windowEventUpdatesFocusedTitle(const QString &change,
                                               qint64 eventContainerId,
                                               qint64 focusedContainerId);

signals:
    void qbarNodeFound(qint64 nodeId);

private slots:
    void reconnect();
    void readCommandMessages();
    void readEventMessages();
    void handleCommandDisconnected();
    void handleEventDisconnected();
    void requestWorkspaces();
    void requestInputs();
    void subscribeWorkspaceEvents();
    void flushPendingCommands();

private:
    enum MessageType : quint32 {
        RunCommand = 0,
        GetWorkspaces = 1,
        Subscribe = 2,
        GetTree = 4,
        GetInputs = 100,
    };

    void connectSockets();
    void sendMessage(QLocalSocket *socket, MessageType type, const QByteArray &payload = {});
    void consumeMessages(QLocalSocket *socket, QByteArray *buffer, bool eventStream);
    void handleMessage(quint32 type, const QByteArray &payload, bool eventStream);
    void setCurrentWindowTitle(const QString &title);
    void setCurrentKeyboardLayout(const QString &layout);
    void setFocusedContainerId(qint64 containerId);
    void setBindingMode(const QString &mode);
    void setScratchpadCount(int count);
    bool supportsSwayInputs() const;
    QString socketPath() const;

    WorkspaceModel m_workspaceModel;
    QString m_currentWindowTitle;
    QString m_currentKeyboardLayout;
    qint64 m_focusedContainerId = -1;
    QString m_bindingMode = QStringLiteral("default");
    int m_scratchpadCount = 0;
    QLocalSocket m_commandSocket;
    QLocalSocket m_eventSocket;
    QByteArray m_commandBuffer;
    QByteArray m_eventBuffer;
    QStringList m_pendingCommands;
    QTimer m_reconnectTimer;
    X11KeyboardLayout *m_keyboardLayoutMonitor = nullptr;  // i3/X11 only; null on sway
};
