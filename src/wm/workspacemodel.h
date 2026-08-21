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

    // Restrict the model to one monitor's workspaces (waybar's all-outputs:false).
    // Empty = no filter. Workspaces whose backend reports no output always pass,
    // so backends without output info (bspwm/ewmh) are unaffected.
    void setOutputFilter(const QString &output);

signals:
    void emptyChanged();

private:
    void applyReplace(QList<Workspace> workspaces);
    QList<Workspace> filtered(QList<Workspace> workspaces) const;

    QList<Workspace> m_workspaces;     // what the view sees (post-filter)
    QList<Workspace> m_allWorkspaces;  // last full set, to re-filter on demand
    QString m_outputFilter;
};
