#pragma once

#include <QObject>
#include <QString>

#include "windowmodel.h"

class WorkspaceModel;

class WindowManagerBackend : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString name READ name CONSTANT)
    Q_PROPERTY(QString currentWindowTitle READ currentWindowTitle NOTIFY currentWindowTitleChanged)
    Q_PROPERTY(QString currentKeyboardLayout READ currentKeyboardLayout NOTIFY currentKeyboardLayoutChanged)
    Q_PROPERTY(qint64 focusedContainerId READ focusedContainerId NOTIFY focusedContainerChanged)
    Q_PROPERTY(QString bindingMode READ bindingMode NOTIFY bindingModeChanged)

public:
    explicit WindowManagerBackend(QObject *parent = nullptr);

    virtual QString name() const = 0;
    virtual WorkspaceModel *workspaceModel() = 0;
    // Shared window list (the Taskbar applet's model). Backends that enumerate
    // windows fill it via m_windows.replace(); the default stays empty.
    WindowModel *windowModel() { return &m_windows; }
    virtual QString currentWindowTitle() const = 0;
    virtual QString currentKeyboardLayout() const = 0;
    virtual qint64 focusedContainerId() const = 0;
    // i3/sway "binding mode" (e.g. "resize"); other backends have no equivalent
    // and stay on the default "default" mode, which the I3Mode applet hides.
    virtual QString bindingMode() const { return QStringLiteral("default"); }

public slots:
    virtual void start() = 0;
    virtual void runCommand(const QString &command) = 0;
    virtual void activateWorkspace(const QString &workspaceName) = 0;
    virtual void activateRelativeWorkspace(int direction) = 0;
    virtual void cycleKeyboardLayout() = 0;
    virtual void requestTreeSnapshot() {}
    // Focus the window owned by `pid` (switching workspace if needed). Backends
    // without window-by-pid support, and pids with no window (terminal/headless
    // players like mpd), simply do nothing.
    virtual void activateWindowByPid(qint64 pid) { Q_UNUSED(pid); }
    // Focus/close a window by its opaque WindowModel id (Taskbar interactions).
    // Backends without window enumeration leave these as no-ops.
    virtual void activateWindow(qint64 id) { Q_UNUSED(id); }
    virtual void closeWindow(qint64 id) { Q_UNUSED(id); }

signals:
    void currentWindowTitleChanged();
    void currentKeyboardLayoutChanged();
    void focusedContainerChanged(qint64 containerId);
    void containerFocusEvent(qint64 containerId);
    void workspaceFocusEvent();
    void bindingModeChanged();

protected:
    WindowModel m_windows;
};
