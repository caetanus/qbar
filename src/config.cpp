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

BarPosition readPosition(const QString &value, BarPosition fallback)
{
    const QString normalized = value.toLower();
    if (normalized == QStringLiteral("top")) {
        return BarPosition::Top;
    }
    if (normalized == QStringLiteral("bottom")) {
        return BarPosition::Bottom;
    }

    return fallback;
}

bool readBoolText(const QString &value, bool fallback)
{
    const QString normalized = value.toLower();
    if (normalized == QStringLiteral("true") || normalized == QStringLiteral("1")
        || normalized == QStringLiteral("yes") || normalized == QStringLiteral("on")) {
        return true;
    }
    if (normalized == QStringLiteral("false") || normalized == QStringLiteral("0")
        || normalized == QStringLiteral("no") || normalized == QStringLiteral("off")) {
        return false;
    }

    return fallback;
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

void applyCommandLineOverrides(BarConfig *config)
{
    const auto args = QCoreApplication::arguments();
    for (int i = 1; i < args.size(); ++i) {
        const QString arg = args.at(i);
        if (arg == QStringLiteral("--height") && i + 1 < args.size()) {
            config->height = args.at(++i).toInt();
        } else if (arg == QStringLiteral("--position") && i + 1 < args.size()) {
            config->position = readPosition(args.at(++i), config->position);
        } else if (arg == QStringLiteral("--x") && i + 1 < args.size()) {
            config->x = args.at(++i).toInt();
        } else if (arg == QStringLiteral("--y") && i + 1 < args.size()) {
            config->y = args.at(++i).toInt();
        } else if (arg == QStringLiteral("--exclusive-zone") && i + 1 < args.size()) {
            config->exclusiveZone = readBoolText(args.at(++i), config->exclusiveZone);
        } else if (arg == QStringLiteral("--no-exclusive-zone")) {
            config->exclusiveZone = false;
        } else if (arg == QStringLiteral("--wayland-layer-shell") && i + 1 < args.size()) {
            config->waylandLayerShell = readBoolText(args.at(++i), config->waylandLayerShell);
        } else if (arg == QStringLiteral("--no-wayland-layer-shell")) {
            config->waylandLayerShell = false;
        }
    }
}

} // namespace

BarConfig loadConfig()
{
    BarConfig config;

    QFile file(configPath());
    if (!file.open(QIODevice::ReadOnly)) {
        applyCommandLineOverrides(&config);
        return config;
    }

    const auto document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject()) {
        applyCommandLineOverrides(&config);
        return config;
    }

    const auto root = document.object();
    config.height = root.value(QStringLiteral("height")).toInt(config.height);
    config.margin = root.value(QStringLiteral("margin")).toInt(config.margin);
    config.spacing = root.value(QStringLiteral("spacing")).toInt(config.spacing);
    config.x = root.value(QStringLiteral("x")).toInt(config.x);
    config.y = root.value(QStringLiteral("y")).toInt(config.y);
    config.position = readPosition(root.value(QStringLiteral("position")).toString(), config.position);
    if (root.contains(QStringLiteral("exclusiveZone"))) {
        config.exclusiveZone = root.value(QStringLiteral("exclusiveZone")).toBool(config.exclusiveZone);
    }
    if (root.contains(QStringLiteral("waylandLayerShell"))) {
        config.waylandLayerShell = root.value(QStringLiteral("waylandLayerShell")).toBool(config.waylandLayerShell);
    }
    config.fontFamily = root.value(QStringLiteral("fontFamily")).toString(config.fontFamily);
    config.fontSize = root.value(QStringLiteral("fontSize")).toInt(config.fontSize);
    config.trayItemPadding = root.value(QStringLiteral("trayItemPadding")).toInt(config.trayItemPadding);
    config.background = readColor(root, QStringLiteral("background"), config.background);
    config.foreground = readColor(root, QStringLiteral("foreground"), config.foreground);
    config.accent = readColor(root, QStringLiteral("accent"), config.accent);
    config.animationDuration = root.value(QStringLiteral("animationDuration")).toInt(config.animationDuration);
    if (root.contains(QStringLiteral("animationEasing"))) {
        config.animationEasing = easingCurveFromName(root.value(QStringLiteral("animationEasing")).toString());
    }

    const auto applets = root.value(QStringLiteral("applets")).toArray();
    if (!applets.isEmpty()) {
        config.applets.clear();
        for (const auto &applet : applets) {
            if (applet.isString()) {
                config.applets.append(applet.toString());
            }
        }
    }

    auto readModuleList = [&](const QString &key) {
        const auto arr = root.value(key).toArray();
        for (const auto &m : arr) {
            if (m.isString()) {
                config.applets.append(m.toString());
            }
        }
    };

    if (config.applets.isEmpty() || root.contains(QStringLiteral("modules-left"))) {
        config.applets.clear();
        readModuleList(QStringLiteral("modules-left"));
        readModuleList(QStringLiteral("modules-center"));
        readModuleList(QStringLiteral("modules-right"));
    }

    applyCommandLineOverrides(&config);
    return config;
}

QString barPositionName(BarPosition position)
{
    return position == BarPosition::Bottom ? QStringLiteral("bottom") : QStringLiteral("top");
}

QEasingCurve easingCurveFromName(const QString &name)
{
    const QString n = name.toLower();
    if (n == QStringLiteral("linear")) return QEasingCurve::Linear;
    if (n == QStringLiteral("inquad")) return QEasingCurve::InQuad;
    if (n == QStringLiteral("outquad")) return QEasingCurve::OutQuad;
    if (n == QStringLiteral("inoutquad")) return QEasingCurve::InOutQuad;
    if (n == QStringLiteral("outinquad")) return QEasingCurve::OutInQuad;
    if (n == QStringLiteral("incubic")) return QEasingCurve::InCubic;
    if (n == QStringLiteral("outcubic")) return QEasingCurve::OutCubic;
    if (n == QStringLiteral("inoutcubic")) return QEasingCurve::InOutCubic;
    if (n == QStringLiteral("outincubic")) return QEasingCurve::OutInCubic;
    if (n == QStringLiteral("inquart")) return QEasingCurve::InQuart;
    if (n == QStringLiteral("outquart")) return QEasingCurve::OutQuart;
    if (n == QStringLiteral("inoutquart")) return QEasingCurve::InOutQuart;
    if (n == QStringLiteral("inquint")) return QEasingCurve::InQuint;
    if (n == QStringLiteral("outquint")) return QEasingCurve::OutQuint;
    if (n == QStringLiteral("inoutquint")) return QEasingCurve::InOutQuint;
    if (n == QStringLiteral("insine")) return QEasingCurve::InSine;
    if (n == QStringLiteral("outsine")) return QEasingCurve::OutSine;
    if (n == QStringLiteral("inoutsine")) return QEasingCurve::InOutSine;
    if (n == QStringLiteral("outexpo")) return QEasingCurve::OutExpo;
    if (n == QStringLiteral("inexpo")) return QEasingCurve::InExpo;
    if (n == QStringLiteral("inoutexpo")) return QEasingCurve::InOutExpo;
    if (n == QStringLiteral("outcirc")) return QEasingCurve::OutCirc;
    if (n == QStringLiteral("incirc")) return QEasingCurve::InCirc;
    if (n == QStringLiteral("inoutcirc")) return QEasingCurve::InOutCirc;
    if (n == QStringLiteral("outelastic")) return QEasingCurve::OutElastic;
    if (n == QStringLiteral("inelastic")) return QEasingCurve::InElastic;
    if (n == QStringLiteral("inoutelastic")) return QEasingCurve::InOutElastic;
    if (n == QStringLiteral("outback")) return QEasingCurve::OutBack;
    if (n == QStringLiteral("inback")) return QEasingCurve::InBack;
    if (n == QStringLiteral("inoutback")) return QEasingCurve::InOutBack;
    if (n == QStringLiteral("outbounce")) return QEasingCurve::OutBounce;
    if (n == QStringLiteral("inbounce")) return QEasingCurve::InBounce;
    if (n == QStringLiteral("inoutbounce")) return QEasingCurve::InOutBounce;
    return QEasingCurve::InOutQuad;
}
