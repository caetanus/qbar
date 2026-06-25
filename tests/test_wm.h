#pragma once

#include <QObject>

class WindowManagerTests final : public QObject {
    Q_OBJECT

private slots:
    void workspaceModelUpdatesRoles();
    void windowModelMutatesIncrementally();
    void i3WindowTitleEventsOnlyUpdateFocusedContainer();
    void i3WorkspaceJsonIsParsed();
    void hyprlandWorkspaceJsonIsParsed();
    void hyprlandActiveWindowTitleIsParsed();
    void hyprlandKeyboardLayoutIsNormalized();
    void hyprlandSubmapIsNormalizedAsBindingMode();
    void bspwmDesktopStateIsParsed();
    void bspwmFocusedNodeTitleIsParsed();
    void factoryCreatesNullBackend();
};
