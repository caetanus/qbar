#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QString>

// List of open windows across the active window manager backend, mirroring the
// WorkspaceModel pattern. Each backend (i3/sway, Hyprland, ewmh, bspwm) fills
// this; the Taskbar applet consumes it and filters by scope (workspace/all/
// monitor) in QML. The `id` is an opaque backend-specific handle — focus/close
// always route back through the backend's activateWindow/closeWindow, never
// interpreting the id in QML.
class WindowModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(bool empty READ isEmpty NOTIFY emptyChanged)

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        TitleRole,
        AppIdRole,
        WorkspaceNameRole,
        MonitorRole,
        FocusedRole,
        UrgentRole,
    };

    struct Window {
        qint64 id = 0;
        QString title;
        QString appId;
        QString workspaceName;
        QString monitor;
        bool focused = false;
        bool urgent = false;
    };

    explicit WindowModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE bool isEmpty() const;

    QList<Window> windows() const;
    void replace(QList<Window> windows);

signals:
    void emptyChanged();

private:
    static QList<int> changedRoles(const Window &current, const Window &incoming);
    int indexOfWindow(qint64 id, int from = 0) const;
    void updateWindowAt(int row, const Window &incoming);

    QList<Window> m_windows;
};
