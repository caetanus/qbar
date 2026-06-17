#pragma once

#include <QEasingCurve>
#include <QColor>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

enum class BarPosition {
    Top,
    Bottom,
};

struct BarConfig {
    int height = 28;
    int margin = 0;
    int spacing = 0;
    int x = -1;
    int y = -1;
    BarPosition position = BarPosition::Top;
    bool exclusiveZone = true;
    bool waylandLayerShell = true;
    // When true, the popup backdrop overlay grabs the keyboard while open so it
    // can handle Escape-to-close. When false the overlay stays focusless.
    bool popupKeyboardFocus = false;
    QColor background = QColor(38, 48, 57, 175);
    QColor foreground = QColor(238, 242, 247);
    QColor accent = QColor(99, 179, 237);
    QString fontFamily = QStringLiteral("Sans Serif");
    int fontSize = 9;
    int trayItemPadding = 2;
    int animationDuration = 200;
    QEasingCurve animationEasing = QEasingCurve::InOutQuad;
    QString windowManagerBackend = QStringLiteral("auto");
    QVariantMap customTools;
    // Taskbar applet options: scope ("workspace"|"all"|"monitor"),
    // middleClickClose (bool), rightClickMenu (bool). Defaults set in config.cpp.
    QVariantMap taskbar;
    // CPU/Memory/Network display: { "format": [parts...], "text": "<label>" }.
    // Parts (composable, ordered): "text" (the literal label), "percentage",
    // "clock" (cpu), "absolute" (mem used/total, net rate), "graph", and "cycle"
    // (a value slot the mouse wheel cycles through the applet's modes + none).
    // Defaults in config.cpp.
    QVariantMap cpu;
    QVariantMap memory;
    QVariantMap network;
    QString styleSheet;
    QString output;          // target monitor name (waybar: "output")
    int marginTop    = -1;   // -1 = inherit from margin
    int marginBottom = -1;
    int marginLeft   = -1;
    int marginRight  = -1;
    QStringList appletsLeft = {
        QStringLiteral("Workspaces"),
        QStringLiteral("I3Mode"),
        QStringLiteral("CPU"),
        QStringLiteral("Memory"),
        QStringLiteral("Network"),
        QStringLiteral("Media"),
    };
    QStringList appletsCenter = {
        QStringLiteral("Title"),
    };
    QStringList appletsRight = {
        QStringLiteral("Caffeine"),
        QStringLiteral("Brightness"),
        QStringLiteral("XInput"),
        QStringLiteral("NetworkManager"),
        QStringLiteral("Temperature"),
        QStringLiteral("Sound"),
        QStringLiteral("Battery"),
        QStringLiteral("CustomTool:custom/dollar"),
        QStringLiteral("CustomTool:custom/btc"),
        QStringLiteral("Clock"),
        QStringLiteral("Tray"),
    };
    QString configFilePath;
    int configIndex = 0;
    QStringList applets = {
        QStringLiteral("Workspaces"),
        QStringLiteral("I3Mode"),
        QStringLiteral("CPU"),
        QStringLiteral("Memory"),
        QStringLiteral("Network"),
        QStringLiteral("Media"),
        QStringLiteral("Title"),
        QStringLiteral("Caffeine"),
        QStringLiteral("Brightness"),
        QStringLiteral("XInput"),
        QStringLiteral("NetworkManager"),
        QStringLiteral("Temperature"),
        QStringLiteral("Sound"),
        QStringLiteral("Battery"),
        QStringLiteral("CustomTool:custom/dollar"),
        QStringLiteral("CustomTool:custom/btc"),
        QStringLiteral("Clock"),
        QStringLiteral("Tray"),
    };
};

BarConfig loadConfig();
QList<BarConfig> loadConfigs();
QString barPositionName(BarPosition position);
QEasingCurve easingCurveFromName(const QString &name);
QVariantMap parseCustomTools(const QJsonObject &root);
