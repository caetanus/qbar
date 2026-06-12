#pragma once

#include <QEasingCurve>
#include <QColor>
#include <QString>
#include <QStringList>

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
    QColor background = QColor(38, 48, 57, 175);
    QColor foreground = QColor(238, 242, 247);
    QColor accent = QColor(99, 179, 237);
    QString fontFamily = QStringLiteral("Sans Serif");
    int fontSize = 9;
    int trayItemPadding = 2;
    int animationDuration = 200;
    QEasingCurve animationEasing = QEasingCurve::InOutQuad;
    QStringList appletsLeft = {
        QStringLiteral("Workspaces"),
        QStringLiteral("CPU"),
        QStringLiteral("Memory"),
        QStringLiteral("Network"),
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
        QStringLiteral("Clock"),
        QStringLiteral("Tray"),
    };
    QStringList applets = {
        QStringLiteral("Workspaces"),
        QStringLiteral("CPU"),
        QStringLiteral("Memory"),
        QStringLiteral("Network"),
        QStringLiteral("Title"),
        QStringLiteral("Caffeine"),
        QStringLiteral("Brightness"),
        QStringLiteral("XInput"),
        QStringLiteral("NetworkManager"),
        QStringLiteral("Temperature"),
        QStringLiteral("Clock"),
        QStringLiteral("Tray"),
        QStringLiteral("Sound"),
        QStringLiteral("Battery"),
    };
};

BarConfig loadConfig();
QString barPositionName(BarPosition position);
QEasingCurve easingCurveFromName(const QString &name);
