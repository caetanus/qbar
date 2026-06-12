#pragma once

#include <QAbstractListModel>
#include <QByteArray>
#include <QJsonArray>
#include <QLocalSocket>
#include <QObject>
#include <QStringList>
#include <QTimer>

class WorkspaceModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(bool empty READ isEmpty NOTIFY emptyChanged)

public:
    enum Role {
        NameRole = Qt::UserRole + 1,
        NumberRole,
        FocusedRole,
        UrgentRole,
        VisibleRole,
        OutputRole,
    };

    explicit WorkspaceModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE bool isEmpty() const;

    void replaceFromJson(const QJsonArray &workspaces);

signals:
    void emptyChanged();

private:
    struct Workspace {
        QString name;
        QString output;
        int number = -1;
        bool focused = false;
        bool urgent = false;
        bool visible = false;
    };

    QList<Workspace> m_workspaces;
};

class I3IpcClient final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString currentWindowTitle READ currentWindowTitle NOTIFY currentWindowTitleChanged)
    Q_PROPERTY(QString currentKeyboardLayout READ currentKeyboardLayout NOTIFY currentKeyboardLayoutChanged)

public:
    explicit I3IpcClient(QObject *parent = nullptr);

    WorkspaceModel *workspaceModel();
    QString currentWindowTitle() const;
    QString currentKeyboardLayout() const;

public slots:
    void start();
    void runCommand(const QString &command);
    void activateWorkspace(const QString &workspaceName);
    void activateRelativeWorkspace(int direction);
    void cycleKeyboardLayout();
    void requestTreeSnapshot();

signals:
    void qbarNodeFound(qint64 nodeId);
    void currentWindowTitleChanged();
    void currentKeyboardLayoutChanged();

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
    bool supportsSwayInputs() const;
    QString socketPath() const;

    WorkspaceModel m_workspaceModel;
    QString m_currentWindowTitle;
    QString m_currentKeyboardLayout;
    QLocalSocket m_commandSocket;
    QLocalSocket m_eventSocket;
    QByteArray m_commandBuffer;
    QByteArray m_eventBuffer;
    QStringList m_pendingCommands;
    QTimer m_reconnectTimer;
};
