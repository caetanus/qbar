#include "windowmodel.h"

#include <QHash>
#include <QStringList>

namespace {

// Reorder so windows of the same workspace are contiguous, preserving the order
// in which workspaces first appear (which is already the natural order for the
// i3/bspwm tree walks). Backends like Hyprland/ewmh that emit windows ungrouped
// then still render grouped in the taskbar.
QList<WindowModel::Window> groupByWorkspace(const QList<WindowModel::Window> &windows)
{
    QStringList order;
    QHash<QString, QList<WindowModel::Window>> buckets;
    for (const auto &window : windows) {
        if (!buckets.contains(window.workspaceName)) {
            order.append(window.workspaceName);
        }
        buckets[window.workspaceName].append(window);
    }

    QList<WindowModel::Window> grouped;
    grouped.reserve(windows.size());
    for (const QString &workspace : order) {
        grouped.append(buckets.value(workspace));
    }
    return grouped;
}

} // namespace

WindowModel::WindowModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int WindowModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_windows.size();
}

QVariant WindowModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_windows.size()) {
        return {};
    }

    const auto &window = m_windows.at(index.row());
    switch (role) {
    case IdRole:
        return window.id;
    case TitleRole:
        return window.title;
    case AppIdRole:
        return window.appId;
    case WorkspaceNameRole:
        return window.workspaceName;
    case MonitorRole:
        return window.monitor;
    case FocusedRole:
        return window.focused;
    case UrgentRole:
        return window.urgent;
    default:
        return {};
    }
}

QHash<int, QByteArray> WindowModel::roleNames() const
{
    return {
        {IdRole, "windowId"},
        {TitleRole, "title"},
        {AppIdRole, "appId"},
        {WorkspaceNameRole, "workspaceName"},
        {MonitorRole, "monitor"},
        {FocusedRole, "focused"},
        {UrgentRole, "urgent"},
    };
}

QList<int> WindowModel::changedRoles(const Window &current, const Window &incoming)
{
    QList<int> roles;
    if (current.title != incoming.title) {
        roles.append(TitleRole);
    }
    if (current.appId != incoming.appId) {
        roles.append(AppIdRole);
    }
    if (current.workspaceName != incoming.workspaceName) {
        roles.append(WorkspaceNameRole);
    }
    if (current.monitor != incoming.monitor) {
        roles.append(MonitorRole);
    }
    if (current.focused != incoming.focused) {
        roles.append(FocusedRole);
    }
    if (current.urgent != incoming.urgent) {
        roles.append(UrgentRole);
    }
    return roles;
}

int WindowModel::indexOfWindow(qint64 id, int from) const
{
    for (int i = qMax(0, from); i < m_windows.size(); ++i) {
        if (m_windows.at(i).id == id) {
            return i;
        }
    }
    return -1;
}

void WindowModel::updateWindowAt(int row, const Window &incoming)
{
    const QList<int> roles = changedRoles(m_windows.at(row), incoming);
    if (roles.isEmpty()) {
        return;
    }

    m_windows[row] = incoming;
    const QModelIndex idx = index(row, 0);
    emit dataChanged(idx, idx, roles);
}

bool WindowModel::isEmpty() const
{
    return m_windows.isEmpty();
}

QList<WindowModel::Window> WindowModel::windows() const
{
    return m_windows;
}

void WindowModel::replace(QList<Window> windows)
{
    windows = groupByWorkspace(windows);
    const bool wasEmpty = m_windows.isEmpty();

    for (int row = m_windows.size() - 1; row >= 0; --row) {
        bool stillPresent = false;
        for (const Window &incoming : std::as_const(windows)) {
            if (incoming.id == m_windows.at(row).id) {
                stillPresent = true;
                break;
            }
        }
        if (!stillPresent) {
            beginRemoveRows(QModelIndex(), row, row);
            m_windows.removeAt(row);
            endRemoveRows();
        }
    }

    for (int targetRow = 0; targetRow < windows.size(); ++targetRow) {
        const Window &incoming = windows.at(targetRow);
        const int currentRow = indexOfWindow(incoming.id, targetRow);

        if (currentRow < 0) {
            beginInsertRows(QModelIndex(), targetRow, targetRow);
            m_windows.insert(targetRow, incoming);
            endInsertRows();
            continue;
        }

        if (currentRow != targetRow) {
            beginMoveRows(QModelIndex(), currentRow, currentRow, QModelIndex(), targetRow);
            m_windows.move(currentRow, targetRow);
            endMoveRows();
        }

        updateWindowAt(targetRow, incoming);
    }

    if (wasEmpty != m_windows.isEmpty()) {
        emit emptyChanged();
    }
}
