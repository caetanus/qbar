#include "config.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

namespace {

QColor readColor(const QJsonObject &object, const QString &key, const QColor &fallback)
{
    const auto value = object.value(key);
    if (!value.isString()) {
        return fallback;
    }

    const QColor color(value.toString());
    return color.isValid() ? color : fallback;
}

QString configPath()
{
    const auto args = QCoreApplication::arguments();
    for (int i = 1; i + 1 < args.size(); ++i) {
        if (args.at(i) == QStringLiteral("--config")) {
            return args.at(i + 1);
        }
    }

    const auto base = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    return QDir(base).filePath(QStringLiteral("qbar/config.json"));
}

} // namespace

BarConfig loadConfig()
{
    BarConfig config;

    QFile file(configPath());
    if (!file.open(QIODevice::ReadOnly)) {
        return config;
    }

    const auto document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject()) {
        return config;
    }

    const auto root = document.object();
    config.height = root.value(QStringLiteral("height")).toInt(config.height);
    config.margin = root.value(QStringLiteral("margin")).toInt(config.margin);
    config.spacing = root.value(QStringLiteral("spacing")).toInt(config.spacing);
    config.fontFamily = root.value(QStringLiteral("fontFamily")).toString(config.fontFamily);
    config.fontSize = root.value(QStringLiteral("fontSize")).toInt(config.fontSize);
    config.background = readColor(root, QStringLiteral("background"), config.background);
    config.foreground = readColor(root, QStringLiteral("foreground"), config.foreground);
    config.accent = readColor(root, QStringLiteral("accent"), config.accent);

    const auto applets = root.value(QStringLiteral("applets")).toArray();
    if (!applets.isEmpty()) {
        config.applets.clear();
        for (const auto &applet : applets) {
            if (applet.isString()) {
                config.applets.append(applet.toString());
            }
        }
    }

    return config;
}
