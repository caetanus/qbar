#include "config.h"

#include "json/jsonc.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScreen>
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


// The bundled defaults — a documented config template, the JSON schema, every theme, and
// the example QML widgets — live in the qrc under :/defaults (see data/qbar-defaults.qrc).
// We copy them into the config dir so a new user lands on something real to edit.

// Copy a qrc resource to disk if the destination is missing. qrc files come out read-only,
// so relax permissions to user read/write (the whole point is that the user can edit them).
bool copyResourceIfMissing(const QString &resourcePath, const QString &destPath)
{
    if (QFileInfo::exists(destPath)) {
        return false;
    }
    QDir().mkpath(QFileInfo(destPath).path());
    if (!QFile::copy(resourcePath, destPath)) {
        return false;
    }
    QFile::setPermissions(destPath,
                          QFileDevice::ReadOwner | QFileDevice::WriteOwner
                              | QFileDevice::ReadGroup | QFileDevice::ReadOther);
    return true;
}

// Write the JSON schema next to the config (so editors validate) if it isn't there yet.
void ensureConfigSchema(const QString &configFilePath)
{
    const QString schemaPath = QFileInfo(configFilePath).dir().filePath(QStringLiteral("config.schema.json"));
    copyResourceIfMissing(QStringLiteral(":/defaults/config.schema.json"), schemaPath);
}

// First-run scaffold: with no config yet, spit the whole starter kit (documented config.json,
// schema, all themes, example widgets) into the config dir, so the user isn't staring at an
// empty directory. Only ever runs on a fresh dir (guarded on the config file's absence).
// On first run, bake the monitors connected right now into the freshly-copied
// config.json so the user sees their actual outputs (and how per-monitor config
// works) instead of a generic template. The bundled config carries no "output"
// key, so listing every detected monitor keeps the default "show everywhere"
// behaviour while making the wiring explicit and editable. The config is a
// heavily-commented JSONC document, so we splice text rather than reserialising.
void injectDetectedOutputs(const QString &configFilePath)
{
    if (QGuiApplication::instance() == nullptr) {
        return; // no GUI context (e.g. tests); leave the template untouched
    }
    QStringList names;
    const auto screens = QGuiApplication::screens();
    for (const QScreen *screen : screens) {
        if (!screen->name().isEmpty()) {
            names.append(screen->name());
        }
    }
    if (names.isEmpty()) {
        return;
    }

    QFile file(configFilePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    QString text = QString::fromUtf8(file.readAll());
    file.close();

    const qsizetype brace = text.indexOf(QLatin1Char('{'));
    if (brace < 0 || text.contains(QStringLiteral("\"output\""))) {
        return; // not an object, or already specifies outputs
    }

    QStringList quoted;
    quoted.reserve(names.size());
    for (const QString &name : std::as_const(names)) {
        quoted.append(QStringLiteral("\"%1\"").arg(name));
    }
    const QString block = QStringLiteral(
        "\n  // Monitors detected on first run. Empty/omitted = show on every monitor"
        "\n  // (and any hotplugged later); prefix a name with \"!\" to exclude it. To give"
        "\n  // each monitor its own layout, make this file a JSON array of bar objects,"
        "\n  // each with its own \"output\"."
        "\n  \"output\": [%1],")
        .arg(quoted.join(QStringLiteral(", ")));
    text.insert(brace + 1, block);

    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(text.toUtf8());
        file.close();
    }
}

void scaffoldConfigDir(const QString &configFilePath)
{
    if (QFileInfo::exists(configFilePath)) {
        return;
    }
    const QString dir = QFileInfo(configFilePath).dir().path();
    const QString prefix = QStringLiteral(":/defaults/");
    QDirIterator it(QStringLiteral(":/defaults"), QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString src = it.next();
        copyResourceIfMissing(src, QDir(dir).filePath(src.mid(prefix.size())));
    }
    injectDetectedOutputs(configFilePath);
}

// Flatten the top-level config into a list of bar objects, expanding "include" string entries
// — a path (relative to the including file's dir) to another config file, à la polybar — so a
// big multi-monitor setup can be split across files. Recursive, with a depth guard vs cycles.
void collectBarObjects(const QJsonValue &value, const QString &baseDir,
                       QList<QJsonObject> *out, int depth)
{
    if (depth > 8) {
        qWarning("qbar: config include nesting too deep (cycle?), stopping");
        return;
    }
    if (value.isObject()) {
        out->append(value.toObject());
    } else if (value.isArray()) {
        for (const QJsonValue &entry : value.toArray()) {
            collectBarObjects(entry, baseDir, out, depth);
        }
    } else if (value.isString()) {
        QString path = value.toString();
        if (!QDir::isAbsolutePath(path)) {
            path = QDir(baseDir).filePath(path);
        }
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            qWarning() << "qbar: config include not found:" << path;
            return;
        }
        QString err;
        const auto doc = Jsonc::parse(QString::fromUtf8(file.readAll()), &err);
        if (!err.isEmpty()) {
            qWarning() << "qbar: config include" << path << err;
        }
        // An included file's own relative includes resolve against ITS directory.
        const QString includedBase = QFileInfo(path).absolutePath();
        if (doc.isArray()) {
            collectBarObjects(doc.array(), includedBase, out, depth + 1);
        } else if (doc.isObject()) {
            out->append(doc.object());
        }
    }
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

// A module list that may contain inline groups. A string is a module name as-is. An object
// `{ "<name>": [ {id/orientation/drawer}, "Child", ... ] }` (or `{ "<name>": { "modules":
// [...], ... } }`) is an inline group: it's normalised into `groups["group/<name>"]` and the
// list gets the "group/<name>" reference token — so both this and waybar's top-level
// `group/<name>` form collapse to the same internal representation Bar.qml renders.
// Parse an inline group's body into its def map (options + a normalised "modules" list).
// The body is either an array — `[ {opts}, "Child", ... ]` (opts objects are merged into the
// def, strings are children) — or an object — `{ "modules": [...], ...opts }`.
QVariantMap parseInlineGroupBody(const QJsonValue &body)
{
    QVariantMap def;
    QStringList children;
    if (body.isArray()) {
        for (const auto &child : body.toArray()) {
            if (child.isString()) {
                children.append(child.toString());
            } else if (child.isObject()) {
                const QVariantMap opts = child.toObject().toVariantMap();
                for (auto it = opts.constBegin(); it != opts.constEnd(); ++it) {
                    def.insert(it.key(), it.value());
                }
            }
        }
    } else if (body.isObject()) {
        def = body.toObject().toVariantMap();
        for (const auto &child : body.toObject().value(QStringLiteral("modules")).toArray()) {
            if (child.isString()) {
                children.append(child.toString());
            }
        }
    }
    def.insert(QStringLiteral("modules"), children);
    return def;
}

void readModuleList(const QJsonObject &root, const QString &key, QStringList *target, QVariantMap *groups)
{
    const auto arr = root.value(key).toArray();
    target->clear();
    if (arr.isEmpty()) {
        return;
    }
    for (const auto &value : arr) {
        if (value.isString()) {
            target->append(value.toString());
            continue;
        }
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject obj = value.toObject();
        if (obj.size() != 1) {
            continue;
        }
        const QString name = obj.constBegin().key();
        const QString ref = QStringLiteral("group/") + name;
        groups->insert(ref, parseInlineGroupBody(obj.constBegin().value()));
        target->append(ref);
    }
}

QVariantMap defaultCustomTools()
{
    QVariantMap tools;
    const QString waybarScripts = QDir::homePath() + QStringLiteral("/.config/waybar/scripts/");

    QVariantMap dollar;
    dollar.insert(QStringLiteral("exec"), QStringLiteral("python -u ") + waybarScripts + QStringLiteral("dollar.py --waybar"));
    dollar.insert(QStringLiteral("interval"), 10);
    dollar.insert(QStringLiteral("return-type"), QStringLiteral("json"));
    dollar.insert(QStringLiteral("show-empty"), false);
    dollar.insert(QStringLiteral("tooltip"), false);
    tools.insert(QStringLiteral("custom/dollar"), dollar);

    QVariantList cryptoTicks;
    QVariantMap btc;
    btc.insert(QStringLiteral("label"), QStringLiteral("BTC"));
    btc.insert(QStringLiteral("symbol"), QStringLiteral("BTCUSDT"));
    btc.insert(QStringLiteral("base"), QStringLiteral("BTC"));
    btc.insert(QStringLiteral("quote"), QStringLiteral("USDT"));
    btc.insert(QStringLiteral("icon"), QStringLiteral("₿"));
    btc.insert(QStringLiteral("color"), QStringLiteral("#f7931a"));
    cryptoTicks.append(btc);
    QVariantMap eth;
    eth.insert(QStringLiteral("label"), QStringLiteral("ETH"));
    eth.insert(QStringLiteral("symbol"), QStringLiteral("ETHUSDT"));
    eth.insert(QStringLiteral("base"), QStringLiteral("ETH"));
    eth.insert(QStringLiteral("quote"), QStringLiteral("USDT"));
    eth.insert(QStringLiteral("icon"), QStringLiteral("Ξ"));
    eth.insert(QStringLiteral("color"), QStringLiteral("#627eea"));
    cryptoTicks.append(eth);
    QVariantMap xmr;
    xmr.insert(QStringLiteral("label"), QStringLiteral("XMR"));
    xmr.insert(QStringLiteral("symbol"), QStringLiteral("XMRUSDT"));
    xmr.insert(QStringLiteral("base"), QStringLiteral("XMR"));
    xmr.insert(QStringLiteral("quote"), QStringLiteral("USDT"));
    xmr.insert(QStringLiteral("icon"), QStringLiteral("ɱ"));
    xmr.insert(QStringLiteral("color"), QStringLiteral("#ff6600"));
    cryptoTicks.append(xmr);

    QVariantMap crypto;
    crypto.insert(QStringLiteral("source"), QStringLiteral("widgets/Crypto.qml"));
    crypto.insert(QStringLiteral("ticks"), cryptoTicks);
    tools.insert(QStringLiteral("custom/crypto"), crypto);

    return tools;
}

} // namespace

QVariantMap parseCustomTools(const QJsonObject &root)
{
    QVariantMap customTools = defaultCustomTools();
    if (root.contains(QStringLiteral("customTools")) && root.value(QStringLiteral("customTools")).isObject()) {
        const QVariantMap overrides = root.value(QStringLiteral("customTools")).toObject().toVariantMap();
        for (auto it = overrides.cbegin(); it != overrides.cend(); ++it) {
            customTools.insert(it.key(), it.value());
        }
    }
    return customTools;
}

// Per-applet display config: { "format": [parts...], "text": "<label>" }.
// `defaultFormat` reproduces the applet's current visuals when unconfigured;
// `defaultText` is the literal label for the "text"/"cycle" parts.
QVariantMap parseDisplay(const QJsonObject &root, const QString &key,
                         const QStringList &defaultFormat, const QString &defaultText)
{
    QVariantMap result{
        {QStringLiteral("format"), QVariant(defaultFormat)},
        {QStringLiteral("text"), defaultText},
    };
    if (root.contains(key) && root.value(key).isObject()) {
        const QJsonObject object = root.value(key).toObject();
        if (object.value(QStringLiteral("format")).isArray()) {
            QStringList parts;
            for (const auto &value : object.value(QStringLiteral("format")).toArray()) {
                if (value.isString()) {
                    parts.append(value.toString());
                }
            }
            result.insert(QStringLiteral("format"), QVariant(parts));
        }
        if (object.contains(QStringLiteral("text"))) {
            result.insert(QStringLiteral("text"), object.value(QStringLiteral("text")).toString());
        }
    }
    return result;
}

QVariantMap parseTaskbar(const QJsonObject &root)
{
    QVariantMap taskbar{
        {QStringLiteral("scope"), QStringLiteral("workspace")},
        {QStringLiteral("middleClickClose"), true},
        {QStringLiteral("rightClickMenu"), true},
    };
    if (root.contains(QStringLiteral("taskbar")) && root.value(QStringLiteral("taskbar")).isObject()) {
        const QVariantMap overrides = root.value(QStringLiteral("taskbar")).toObject().toVariantMap();
        for (auto it = overrides.cbegin(); it != overrides.cend(); ++it) {
            taskbar.insert(it.key(), it.value());
        }
    }
    return taskbar;
}

QVariantMap parseDock(const QJsonObject &root)
{
    QVariantMap dock{
        {QStringLiteral("magnify"), QStringLiteral("fisheye")},
        {QStringLiteral("indicator"), QStringLiteral("underline")},
    };
    if (root.contains(QStringLiteral("dock")) && root.value(QStringLiteral("dock")).isObject()) {
        const QVariantMap overrides = root.value(QStringLiteral("dock")).toObject().toVariantMap();
        for (auto it = overrides.cbegin(); it != overrides.cend(); ++it) {
            dock.insert(it.key(), it.value());
        }
    }
    return dock;
}

QVariantMap parseNotifications(const QJsonObject &root)
{
    // Opt-in on purpose: owning org.freedesktop.Notifications displaces whatever
    // daemon (dunst/mako) the user already runs — that must be an explicit choice
    // (`"notifications": { "enabled": true }`), not a surprise side effect of qbar.
    QVariantMap notifications{
        {QStringLiteral("enabled"), false},
        {QStringLiteral("corner"), QStringLiteral("top-right")},
        {QStringLiteral("maxVisible"), 5},
        {QStringLiteral("timeout"), 6000},
        {QStringLiteral("timeoutLow"), 4000},
        {QStringLiteral("timeoutCritical"), 0},
        {QStringLiteral("width"), 380},
        {QStringLiteral("margin"), 12},
    };
    if (root.contains(QStringLiteral("notifications")) && root.value(QStringLiteral("notifications")).isObject()) {
        const QVariantMap overrides = root.value(QStringLiteral("notifications")).toObject().toVariantMap();
        for (auto it = overrides.cbegin(); it != overrides.cend(); ++it) {
            notifications.insert(it.key(), it.value());
        }
    }
    return notifications;
}

BarConfig parseBarObject(const QJsonObject &root)
{
    BarConfig config;
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
    if (root.contains(QStringLiteral("popupKeyboardFocus"))) {
        config.popupKeyboardFocus = root.value(QStringLiteral("popupKeyboardFocus")).toBool(config.popupKeyboardFocus);
    }
    if (root.contains(QStringLiteral("popupReuse"))) {
        config.popupReuse = root.value(QStringLiteral("popupReuse")).toBool(config.popupReuse);
    }
    config.fontFamily = root.value(QStringLiteral("fontFamily")).toString(config.fontFamily);
    config.fontSize = root.value(QStringLiteral("fontSize")).toInt(config.fontSize);
    config.styleSheet = root.value(QStringLiteral("styleSheet")).toString(config.styleSheet);
    config.baseStyleSheet = root.value(QStringLiteral("baseStyleSheet")).toString(config.baseStyleSheet);
    // "output": a single monitor name, or an array of names (entries may be "!name"
    // to exclude). Absent/empty means every monitor.
    config.outputs.clear();
    const QJsonValue outputValue = root.value(QStringLiteral("output"));
    if (outputValue.isString()) {
        const QString name = outputValue.toString().trimmed();
        if (!name.isEmpty()) {
            config.outputs.append(name);
        }
    } else if (outputValue.isArray()) {
        const QJsonArray entries = outputValue.toArray();
        for (const auto &entry : entries) {
            const QString name = entry.toString().trimmed();
            if (!name.isEmpty()) {
                config.outputs.append(name);
            }
        }
    }
    config.trayItemPadding = root.value(QStringLiteral("trayItemPadding")).toInt(config.trayItemPadding);
    config.background = readColor(root, QStringLiteral("background"), config.background);
    config.foreground = readColor(root, QStringLiteral("foreground"), config.foreground);
    config.accent = readColor(root, QStringLiteral("accent"), config.accent);
    config.animationDuration = root.value(QStringLiteral("animationDuration")).toInt(config.animationDuration);
    if (root.contains(QStringLiteral("animationEasing"))) {
        config.animationEasing = easingCurveFromName(root.value(QStringLiteral("animationEasing")).toString());
    }
    if (root.contains(QStringLiteral("windowManager")) && root.value(QStringLiteral("windowManager")).isObject()) {
        const QJsonObject wm = root.value(QStringLiteral("windowManager")).toObject();
        config.windowManagerBackend = wm.value(QStringLiteral("backend")).toString(config.windowManagerBackend);
    }
    config.customTools = parseCustomTools(root);
    config.taskbar = parseTaskbar(root);
    config.dock = parseDock(root);
    config.notifications = parseNotifications(root);
    // The graph is always rendered; these defaults list only the value part shown
    // beside it, reproducing the historical look.
    config.cpu = parseDisplay(root, QStringLiteral("cpu"), {QStringLiteral("cycle")}, QStringLiteral("cpu"));
    config.memory = parseDisplay(root, QStringLiteral("memory"), {QStringLiteral("cycle")}, QStringLiteral("mem"));
    config.network = parseDisplay(root, QStringLiteral("network"), {QStringLiteral("cycle")}, QStringLiteral("net"));

    // Collect every top-level "group/<name>" object (waybar group/drawer modules).
    config.groups.clear();
    for (auto it = root.constBegin(); it != root.constEnd(); ++it) {
        if (it.key().startsWith(QStringLiteral("group/")) && it.value().isObject())
            config.groups.insert(it.key(), it.value().toObject().toVariantMap());
    }

    const bool hasModuleSections = root.contains(QStringLiteral("modules-left"))
        || root.contains(QStringLiteral("modules-center"))
        || root.contains(QStringLiteral("modules-right"));

    if (hasModuleSections) {
        readModuleList(root, QStringLiteral("modules-left"), &config.appletsLeft, &config.groups);
        readModuleList(root, QStringLiteral("modules-center"), &config.appletsCenter, &config.groups);
        readModuleList(root, QStringLiteral("modules-right"), &config.appletsRight, &config.groups);
        config.applets = config.appletsLeft;
        config.applets.append(config.appletsCenter);
        config.applets.append(config.appletsRight);
    } else {
        const auto applets = root.value(QStringLiteral("applets")).toArray();
        if (!applets.isEmpty()) {
            config.applets.clear();
            for (const auto &applet : applets) {
                if (applet.isString()) {
                    config.applets.append(applet.toString());
                }
            }
        }

        config.appletsLeft = config.applets;
        config.appletsCenter.clear();
        config.appletsRight.clear();
    }

    return config;
}

QList<BarConfig> loadConfigs()
{
    const QString path = configPath();
    scaffoldConfigDir(path); // first run: write the bundled starter kit into a fresh config dir
    ensureConfigSchema(path);

    const auto withDefaults = [&](BarConfig config, int index = 0) {
        config.configFilePath = path;
        config.configIndex = index;
        applyCommandLineOverrides(&config);
        return config;
    };

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {withDefaults(BarConfig{})};
    }

    QString jsonError;
    const auto document = Jsonc::parse(QString::fromUtf8(file.readAll()), &jsonError);

    // A single object is one bar; an array is several (multi-monitor, top+bottom). Array
    // entries may also be "path" strings — includes, expanded relative to this file's dir.
    QList<QJsonObject> barObjects;
    const QString baseDir = QFileInfo(path).absolutePath();
    if (document.isArray()) {
        collectBarObjects(document.array(), baseDir, &barObjects, 0);
    } else if (document.isObject()) {
        barObjects.append(document.object());
    }

    if (barObjects.isEmpty()) {
        if (!jsonError.isEmpty()) {
            qWarning() << "qbar: config" << path << jsonError;
        }
        return {withDefaults(BarConfig{})};
    }

    QList<BarConfig> configs;
    configs.reserve(barObjects.size());
    for (qsizetype i = 0; i < barObjects.size(); ++i) {
        configs.append(withDefaults(parseBarObject(barObjects.at(i)), static_cast<int>(i)));
    }
    return configs;
}

BarConfig loadConfig()
{
    const QList<BarConfig> configs = loadConfigs();
    return configs.isEmpty() ? BarConfig{} : configs.first();
}

bool barConfigTargetsScreen(const BarConfig &config, const QString &screenName)
{
    bool hasAllowList = false;
    bool allowed = false;
    for (const QString &spec : config.outputs) {
        if (spec.startsWith(QLatin1Char('!'))) {
            if (QStringView(spec).mid(1) == screenName) {
                return false; // explicit exclusion wins
            }
            continue;
        }
        hasAllowList = true;
        if (spec == screenName) {
            allowed = true;
        }
    }
    // No positive entries → every monitor (minus exclusions handled above).
    return hasAllowList ? allowed : true;
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
