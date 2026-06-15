#include "workspacemodel.h"

#include <QJsonObject>

WorkspaceModel::WorkspaceModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int WorkspaceModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_workspaces.size();
}

QVariant WorkspaceModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_workspaces.size()) {
        return {};
    }

    const auto &workspace = m_workspaces.at(index.row());
    switch (role) {
    case NameRole:
        return workspace.name;
    case NumberRole:
        return workspace.number;
    case FocusedRole:
        return workspace.focused;
    case UrgentRole:
        return workspace.urgent;
    case VisibleRole:
        return workspace.visible;
    case OutputRole:
        return workspace.output;
    default:
        return {};
    }
}

QHash<int, QByteArray> WorkspaceModel::roleNames() const
{
    return {
        {NameRole, "name"},
        {NumberRole, "number"},
        {FocusedRole, "focused"},
        {UrgentRole, "urgent"},
        {VisibleRole, "visible"},
        {OutputRole, "output"},
    };
}

bool WorkspaceModel::isEmpty() const
{
    return m_workspaces.isEmpty();
}

QList<WorkspaceModel::Workspace> WorkspaceModel::workspaces() const
{
    return m_workspaces;
}

void WorkspaceModel::replace(QList<Workspace> workspaces)
{
    const bool wasEmpty = m_workspaces.isEmpty();

    const bool sameShape = m_workspaces.size() == workspaces.size();
    if (sameShape) {
        bool identitiesMatch = true;
        for (int i = 0; i < workspaces.size(); ++i) {
            const Workspace &current = m_workspaces.at(i);
            const Workspace &incoming = workspaces.at(i);
            if (current.name != incoming.name || current.number != incoming.number) {
                identitiesMatch = false;
                break;
            }
        }

        if (identitiesMatch) {
            for (int i = 0; i < workspaces.size(); ++i) {
                const Workspace &current = m_workspaces.at(i);
                const Workspace &incoming = workspaces.at(i);
                QList<int> roles;
                if (current.name != incoming.name) {
                    roles.append(NameRole);
                }
                if (current.output != incoming.output) {
                    roles.append(OutputRole);
                }
                if (current.number != incoming.number) {
                    roles.append(NumberRole);
                }
                if (current.focused != incoming.focused) {
                    roles.append(FocusedRole);
                }
                if (current.urgent != incoming.urgent) {
                    roles.append(UrgentRole);
                }
                if (current.visible != incoming.visible) {
                    roles.append(VisibleRole);
                }
                if (!roles.isEmpty()) {
                    m_workspaces[i] = incoming;
                    const QModelIndex idx = index(i, 0);
                    emit dataChanged(idx, idx, roles);
                }
            }

            if (wasEmpty != m_workspaces.isEmpty()) {
                emit emptyChanged();
            }
            return;
        }
    }

    beginResetModel();
    m_workspaces = std::move(workspaces);
    endResetModel();

    if (wasEmpty != m_workspaces.isEmpty()) {
        emit emptyChanged();
    }
}

void WorkspaceModel::replaceFromI3Json(const QJsonArray &workspaces)
{
    QList<Workspace> next;
    next.reserve(static_cast<int>(workspaces.size()));

    for (const auto &value : workspaces) {
        const auto object = value.toObject();
        Workspace workspace;
        workspace.name = object.value(QStringLiteral("name")).toString();
        workspace.output = object.value(QStringLiteral("output")).toString();
        workspace.number = object.value(QStringLiteral("num")).toInt(-1);
        workspace.focused = object.value(QStringLiteral("focused")).toBool(false);
        workspace.urgent = object.value(QStringLiteral("urgent")).toBool(false);
        workspace.visible = object.value(QStringLiteral("visible")).toBool(false);
        next.append(workspace);
    }

    replace(std::move(next));
}
