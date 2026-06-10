#pragma once

#include <QColor>
#include <QString>
#include <QStringList>

struct BarConfig {
    int height = 28;
    int margin = 0;
    int spacing = 0;
    QColor background = QColor(38, 48, 57, 175);
    QColor foreground = QColor(238, 242, 247);
    QColor accent = QColor(99, 179, 237);
    QString fontFamily = QStringLiteral("Sans Serif");
    int fontSize = 9;
    QStringList applets = {
        QStringLiteral("Workspaces"),
        QStringLiteral("Title"),
        QStringLiteral("Caffeine"),
        QStringLiteral("XInput"),
        QStringLiteral("Tray"),
        QStringLiteral("Sound"),
        QStringLiteral("Battery"),
        QStringLiteral("Calendar"),
        QStringLiteral("Clock"),
    };
};

BarConfig loadConfig();
