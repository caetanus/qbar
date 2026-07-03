#include "enginebootstrap.h"

#include "qml/jsonasync.h"
#include "qml/jsprocess.h"
#include "qml/jstimers.h"
#include "qml/localstorage.h"
#include "qml/modelcapsules.h"
#include "qml/netfetch.h"

#include <QColor>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickImageProvider>
#include <QSet>
#include <QStringList>

#include <algorithm>

namespace {

class ThemeIconProvider final : public QQuickImageProvider {
public:
    ThemeIconProvider()
        : QQuickImageProvider(QQuickImageProvider::Pixmap)
    {
    }

    QPixmap requestPixmap(const QString &id, QSize *size, const QSize &requestedSize) override
    {
        const int side = std::max(1, requestedSize.width() > 0 ? requestedSize.width() : 18);

        // '|' is not in the URL "unreserved" set, so QUrl percent-encodes it to
        // "%7C" and that escaped form survives into this id. Normalize both
        // spellings before splitting.
        QString normalizedId = id;
        normalizedId.replace(QStringLiteral("%7C"), QStringLiteral("|"), Qt::CaseInsensitive);
        const QStringList parts = normalizedId.split(QLatin1Char('|'));
        const QString iconId = parts.value(0);
        const QString iconThemePath = parts.size() > 2 ? parts.value(2) : QString();

        // Tint colors arrive as bare "RRGGBB" hex (without '#', which would be
        // parsed as a URL fragment and stripped before reaching this provider).
        QColor tintColor;
        if (parts.size() > 1 && !parts.value(1).isEmpty()) {
            QString tintSpec = parts.value(1);
            if (!tintSpec.startsWith(QLatin1Char('#'))) {
                tintSpec.prepend(QLatin1Char('#'));
            }
            tintColor = QColor(tintSpec);
        }

        QIcon icon;
        if (QFileInfo::exists(iconId)) {
            icon = QIcon(iconId);
        } else {
            icon = QIcon::fromTheme(iconId);
            if (icon.isNull()) {
                const QString fileName = findIconFile(iconId, iconThemePath);
                if (!fileName.isEmpty()) {
                    icon = QIcon(fileName);
                }
            }
        }
        if (icon.isNull()) {
            icon = QIcon::fromTheme(QStringLiteral("application-x-executable"));
        }

        QPixmap pixmap = icon.pixmap(side, side);
        const QSize actualSize = pixmap.size();
        const int displaySide = std::min(side, std::max(actualSize.width(), actualSize.height()));
        if (displaySide < side) {
            pixmap = pixmap.scaled(displaySide, displaySide, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }

        if (tintColor.isValid() && tintColor.alpha() > 0) {
            QPixmap tinted(displaySide, displaySide);
            tinted.fill(Qt::transparent);
            QPainter painter(&tinted);
            painter.setRenderHint(QPainter::Antialiasing, true);
            painter.drawPixmap(0, 0, pixmap);
            painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
            painter.fillRect(tinted.rect(), tintColor);
            painter.end();
            pixmap = tinted;
        }

        if (size != nullptr) {
            *size = pixmap.size();
        }
        return pixmap;
    }

private:
    QString findIconFile(const QString &iconName, const QString &iconThemePath) const
    {
        QStringList iconNames;
        if (iconName.endsWith(QStringLiteral("-symbolic"))) {
            iconNames << iconName.left(iconName.size() - 9);
        }
        iconNames << iconName;

        QStringList names;
        for (const QString &name : iconNames) {
            names << name + QStringLiteral(".png")
                  << name + QStringLiteral(".svg")
                  << name + QStringLiteral(".xpm");
        }

        const QStringList themeNames = {
            QIcon::themeName(),
            QIcon::fallbackThemeName(),
            QStringLiteral("hicolor"),
        };

        auto pickLargest = [&](QDirIterator &it) -> QString {
            QString best;
            qint64 bestSize = 0;
            while (it.hasNext()) {
                const QString path = it.next();
                const qint64 sz = QFileInfo(path).size();
                if (sz > bestSize) {
                    best = path;
                    bestSize = sz;
                }
            }
            return best;
        };

        auto findInBases = [&](const QStringList &bases) -> QString {
            for (const QString &base : bases) {
                QDirIterator looseIterator(base, names, QDir::Files, QDirIterator::Subdirectories);
                const QString found = pickLargest(looseIterator);
                if (!found.isEmpty()) {
                    return found;
                }
            }
            for (const QString &theme : themeNames) {
                if (theme.isEmpty()) {
                    continue;
                }
                for (const QString &base : bases) {
                    const QString root = base + QLatin1Char('/') + theme;
                    if (!QFileInfo::exists(root)) {
                        continue;
                    }
                    QDirIterator iterator(root, names, QDir::Files, QDirIterator::Subdirectories);
                    const QString found = pickLargest(iterator);
                    if (!found.isEmpty()) {
                        return found;
                    }
                }
            }
            return {};
        };

        QStringList homeBases;
        QStringList systemBases;
        if (!iconThemePath.isEmpty()) {
            homeBases.prepend(iconThemePath);
            systemBases.prepend(iconThemePath);
        }
        for (const QString &base : QIcon::themeSearchPaths()) {
            if (base.startsWith(QDir::homePath())) {
                homeBases.append(base);
            } else {
                systemBases.append(base);
            }
        }

        const QString homeIcon = findInBases(homeBases);
        if (!homeIcon.isEmpty()) {
            return homeIcon;
        }

        const QString systemIcon = findInBases(systemBases);
        if (!systemIcon.isEmpty()) {
            return systemIcon;
        }

        return {};
    }
};

} // namespace

void installQbarEngineGlobals(QQmlEngine *engine)
{
    if (engine->imageProvider(QStringLiteral("themeicon")) == nullptr) {
        engine->addImageProvider(QStringLiteral("themeicon"), new ThemeIconProvider);
    }

    // Web-style setTimeout/setInterval globals (QTimer-backed) — Qt's QML engine has none.
    JsTimers::install(engine);
    // `Http` global — a QNetworkAccessManager-backed fetch transport for Fetch.js, since
    // QML's XMLHttpRequest silently hangs on network failure (no timeout/onerror).
    NetFetch::install(engine);
    // `JsonAsync` global — worker-threaded JSON.parse for Json.js, so large payloads
    // don't block the event loop and stutter animations (the MPRIS marquee).
    JsonAsync::install(engine);
    // Persistent Web-Storage-like key/value API for runtime QML widgets.
    LocalStorage::install(engine);
    // `Proc` global — a QProcess-backed runner so custom QML widgets can shell out to
    // external commands (the QML side has no process API). Async + self-cleaning like Http.
    JsProcess::install(engine);
}

void exposeConfiguredModels(QQmlContext *context, const BarConfig &config, QWindow *window)
{
    QSet<QString> usedApplets;
    const auto addModule = [&usedApplets](const QString &token) {
        if (token.startsWith(QLatin1String("CustomTool:")) || token.startsWith(QLatin1String("group/")))
            return;
        const int slash = token.indexOf(QLatin1Char('/')); // strip a "/variant" suffix
        usedApplets.insert(slash >= 0 ? token.left(slash) : token);
    };
    for (const QString &t : config.appletsLeft) {
        addModule(t);
    }
    for (const QString &t : config.appletsCenter) {
        addModule(t);
    }
    for (const QString &t : config.appletsRight) {
        addModule(t);
    }
    for (auto it = config.groups.constBegin(); it != config.groups.constEnd(); ++it) {
        const QVariantList mods = it.value().toMap().value(QStringLiteral("modules")).toList();
        for (const QVariant &m : mods) {
            addModule(m.toString());
        }
    }
    const auto expose = [context, window, &usedApplets](const char *applet, const char *ctx, const char *key) {
        if (usedApplets.contains(QLatin1String(applet))) {
            context->setContextProperty(QLatin1String(ctx),
                                        ModelCapsules::instance()->acquire(QLatin1String(key), window));
        }
    };
    expose("CPU", "cpuModel", "cpu");
    expose("Memory", "cpuModel", "cpu");          // Memory reads cpuModel too
    expose("Temperature", "temperatureModel", "temperature");
    expose("Network", "networkModel", "network");
    expose("Network", "networkProcessModel", "networkProcess");
    expose("NetworkManager", "networkManagerModel", "networkManager");
    expose("Brightness", "brightnessModel", "brightness");
    expose("Media", "mprisModel", "mpris");
    expose("Battery", "batteryModel", "battery");
    expose("Tray", "trayModel", "tray");
    expose("Sound", "soundModel", "sound");
    expose("Caffeine", "caffeineModel", "caffeine");
    expose("Disk", "diskModel", "disk");
    expose("Bluetooth", "bluetoothModel", "bluetooth");
    expose("PowerProfiles", "powerProfilesModel", "powerProfiles");
    expose("UPower", "upowerModel", "upower");
    expose("User", "userModel", "user");
    expose("Privacy", "privacyModel", "privacy");
    expose("Clock", "calendarModel", "calendar"); // the calendar popup Clock opens
}
