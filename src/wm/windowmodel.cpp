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

    // In-place update when the set of window ids is unchanged (only titles/focus/
    // urgency shifted), to avoid a full reset that would drop selection/animation.
    bool sameShape = m_windows.size() == windows.size();
    if (sameShape) {
        for (int i = 0; i < windows.size(); ++i) {
            if (m_windows.at(i).id != windows.at(i).id) {
                sameShape = false;
                break;
            }
        }
    }

    if (sameShape) {
        for (int i = 0; i < windows.size(); ++i) {
            const Window &current = m_windows.at(i);
            const Window &incoming = windows.at(i);
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
            if (!roles.isEmpty()) {
                m_windows[i] = incoming;
                const QModelIndex idx = index(i, 0);
                emit dataChanged(idx, idx, roles);
            }
        }
        return;
    }

    beginResetModel();
    m_windows = std::move(windows);
    endResetModel();

    if (wasEmpty != m_windows.isEmpty()) {
        emit emptyChanged();
    }
}
