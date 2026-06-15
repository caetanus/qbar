#pragma once

#include <QObject>

class WindowManagerTests final : public QObject {
    Q_OBJECT

private slots:
    void workspaceModelUpdatesRoles();
    void i3WorkspaceJsonIsParsed();
    void hyprlandWorkspaceJsonIsParsed();
    void hyprlandActiveWindowTitleIsParsed();
    void hyprlandKeyboardLayoutIsNormalized();
    void bspwmDesktopStateIsParsed();
    void bspwmFocusedNodeTitleIsParsed();
    void factoryCreatesNullBackend();
};
