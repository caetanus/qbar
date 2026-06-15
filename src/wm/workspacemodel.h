#pragma once

#include <QAbstractListModel>
#include <QJsonArray>
#include <QList>
#include <QString>

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

    struct Workspace {
        QString name;
        QString output;
        int number = -1;
        bool focused = false;
        bool urgent = false;
        bool visible = false;
    };

    explicit WorkspaceModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE bool isEmpty() const;

    QList<Workspace> workspaces() const;
    void replace(QList<Workspace> workspaces);
    void replaceFromI3Json(const QJsonArray &workspaces);

signals:
    void emptyChanged();

private:
    QList<Workspace> m_workspaces;
};
