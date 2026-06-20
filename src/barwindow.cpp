#include "barwindow.h"

#include "qml/qbarpopupservice.h"
#include "json/jsonc.h"
#include "cpu/cpumodel.h"
#include "temperature/temperaturemodel.h"
#include "brightness/brightnessmodel.h"
#include "caffeine/caffeinemodel.h"
#include "battery/batterymodel.h"
#include "network/networkmodel.h"
#include "networkmanager/networkmanagermodel.h"
#include "disk/diskmodel.h"
#include "bluetooth/bluetoothmodel.h"
#include "powerprofiles/powerprofilesmodel.h"
#include "sound/audiobackendfactory.h"
#include "mpris/mprismodel.h"
#include "platform/capslockmonitor.h"
#include "calendar/calendarmodel.h"
#include "css/csstheme.h"
#include "dbus/dbusservice.h"
#include "platform/platformbarintegration.h"
#include "wm/wmbackendfactory.h"
#include "ipc/i3ipcclient.h"

#include <QApplication>
#include <QByteArray>
#include <QCloseEvent>
#include <QColor>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHideEvent>
#include <QIcon>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QPoint>
#include <QDate>
#include <QProcess>
#include <QQuickImageProvider>
#include <QQuickItem>
#include <QQmlContext>
#include <QQmlEngine>
#include <QPixmap>
#include <QScreen>
#include <QStandardPaths>
#include <QShowEvent>
#include <QPointF>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>
#include <algorithm>
#include <unistd.h>

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

int clampCoordinate(int value, int minValue, int maxValue)
{
    return std::clamp(value, minValue, std::max(minValue, maxValue));
}

QPoint clampedPopupPosition(const QRect &screenArea, const QPoint &preferred, const QSize &popupSize)
{
    const int maxX = screenArea.x() + screenArea.width() - popupSize.width();
    const int maxY = screenArea.y() + screenArea.height() - popupSize.height();
    return QPoint(clampCoordinate(preferred.x(), screenArea.x(), maxX),
                  clampCoordinate(preferred.y(), screenArea.y(), maxY));
}

static QColor contrastColorFor(const QColor &background)
{
    const double luminance = (0.2126 * background.redF())
        + (0.7152 * background.greenF())
        + (0.0722 * background.blueF());
    return luminance < 0.5 ? QColor(QStringLiteral("#ffffff")) : QColor(QStringLiteral("#1f2933"));
}

static QColor contrastGreenFor(const QColor &background)
{
    const double luminance = (0.2126 * background.redF())
        + (0.7152 * background.greenF())
        + (0.0722 * background.blueF());
    return luminance < 0.5 ? QColor(QStringLiteral("#86efac")) : QColor(QStringLiteral("#166534"));
}

QVariantMap themeMap(const BarConfig &config)
{
    return {
        {QStringLiteral("background"), config.background.name(QColor::HexArgb)},
        {QStringLiteral("foreground"), config.foreground.name(QColor::HexArgb)},
        {QStringLiteral("accent"), config.accent.name(QColor::HexArgb)},
        {QStringLiteral("accentForeground"), contrastColorFor(config.accent).name(QColor::HexArgb)},
        {QStringLiteral("contrastIcon"), contrastColorFor(config.background).name(QColor::HexArgb)},
        {QStringLiteral("eventDot"), contrastGreenFor(config.background).name(QColor::HexArgb)},
        {QStringLiteral("eventDotOnAccent"), contrastGreenFor(config.accent).name(QColor::HexArgb)},
        {QStringLiteral("fontFamily"), config.fontFamily},
        {QStringLiteral("fontSize"), config.fontSize},
        {QStringLiteral("trayItemPadding"), config.trayItemPadding},
        {QStringLiteral("animationDuration"), config.animationDuration},
        {QStringLiteral("animationEasing"), static_cast<int>(config.animationEasing.type())},
        {QStringLiteral("height"), config.height},
        {QStringLiteral("margin"), config.margin},
        {QStringLiteral("spacing"), config.spacing},
    };
}

} // namespace

BarWindow::BarWindow(const BarConfig &config, QWindow *parent)
    : QQuickView(parent)
    , m_config(config)
    , m_evolutionCalendarExecutable(QStandardPaths::findExecutable(QStringLiteral("evolution")))
{
    configureWindow();
    m_wm = createWindowManagerBackend(m_config.windowManagerBackend, this);
    m_trayModel = new StatusNotifierModel(this);
    m_cpuModel = new CpuModel(this);
    m_temperatureModel = new TemperatureModel(this);
    m_networkModel = new NetworkModel(this);
    m_networkManagerModel = new NetworkManagerModel(this);
    m_brightnessModel = new BrightnessModel(this);
    m_caffeineModel = new CaffeineModel(this, this);
    m_soundModel = createAudioBackend(this);
    m_mprisModel = new MprisModel(this);
    m_capsLockMonitor = new CapsLockMonitor(this);
    m_calendarModel = new CalendarModel(this);
    m_batteryModel = new BatteryModel(this);
    m_diskModel = new DiskModel(this);
    m_bluetoothModel = new BluetoothModel(this);
    m_powerProfilesModel = new PowerProfilesModel(this);
    m_cssTheme = new CssTheme(this);
    loadCssTheme();

    m_configWatcher = new QFileSystemWatcher(this);
    if (!m_config.configFilePath.isEmpty() && QFileInfo::exists(m_config.configFilePath)) {
        QFile f(m_config.configFilePath);
        if (f.open(QIODevice::ReadOnly)) {
            m_configHash = QCryptographicHash::hash(f.readAll(), QCryptographicHash::Md5);
        }
        m_configWatcher->addPath(m_config.configFilePath);
    }
    connect(m_configWatcher, &QFileSystemWatcher::fileChanged, this, &BarWindow::onConfigFileChanged);

    if (auto *i3Backend = qobject_cast<I3IpcClient *>(m_wm)) {
        connect(i3Backend, SIGNAL(qbarNodeFound(qint64)), this, SLOT(handleQbarNodeFound(qint64)));
    }
    buildLayout();
    positionAtTop();
    m_wm->start();
    m_trayModel->start();
}

bool BarWindow::calendarAppAvailable() const
{
    return !m_evolutionCalendarExecutable.isEmpty();
}

void BarWindow::closeEvent(QCloseEvent *event)
{
    qWarning() << "QBar closeEvent";
    QQuickView::closeEvent(event);
    QApplication::quit();
}

void BarWindow::hideEvent(QHideEvent *event)
{
    qWarning() << "QBar hideEvent";
    QQuickView::hideEvent(event);
}

void BarWindow::resizeEvent(QResizeEvent *event)
{
    QQuickView::resizeEvent(event);
}

void BarWindow::showEvent(QShowEvent *event)
{
    qWarning() << "QBar showEvent";
    QQuickView::showEvent(event);

    if (!m_platformIntegrationApplied) {
        applyPlatformBarIntegration(this, m_config);
        scheduleTestWindowRules();
        m_platformIntegrationApplied = true;
    }
}

void BarWindow::loadCssTheme()
{
    // Probe candidate paths in priority order
    const QString configDir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    QStringList candidates;
    if (!m_config.styleSheet.isEmpty())
        candidates.append(m_config.styleSheet);
    candidates.append(QDir(configDir).filePath(QStringLiteral("qbar/style.css")));

    for (const QString &path : candidates) {
        if (QFileInfo::exists(path)) {
            m_cssTheme->load(path);
            return;
        }
    }
}

void BarWindow::onConfigFileChanged(const QString &path)
{
    // Re-add to watcher — editors often atomically replace files
    if (!m_configWatcher->files().contains(path))
        m_configWatcher->addPath(path);

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "qbar: cannot read config file" << path;
        return;
    }
    const QByteArray data = file.readAll();
    const QByteArray hash = QCryptographicHash::hash(data, QCryptographicHash::Md5);
    if (hash == m_configHash)
        return; // touch with no content change
    m_configHash = hash;

    QString jsonError;
    const auto doc = Jsonc::parse(QString::fromUtf8(data), &jsonError);
    QJsonObject root;
    bool foundRoot = false;
    if (doc.isArray()) {
        const QJsonArray bars = doc.array();
        if (m_config.configIndex >= 0 && m_config.configIndex < bars.size() && bars.at(m_config.configIndex).isObject()) {
            root = bars.at(m_config.configIndex).toObject();
            foundRoot = true;
        }
    } else if (doc.isObject()) {
        root = doc.object();
        foundRoot = true;
    }

    if (!foundRoot) {
        qWarning() << "qbar: config.json is broken (invalid JSONC), ignoring reload" << jsonError;
        return;
    }

    const QString newStyleSheet = root.value(QStringLiteral("styleSheet")).toString();
    if (newStyleSheet != m_config.styleSheet) {
        m_config.styleSheet = newStyleSheet;
        loadCssTheme();
    } else {
        // styleSheet unchanged — CSS watcher handles its own reloads
    }

    const QVariantMap newCustomTools = parseCustomTools(root);
    if (newCustomTools != m_config.customTools) {
        m_config.customTools = newCustomTools;
        rootContext()->setContextProperty(QStringLiteral("customTools"), m_config.customTools);
    }
}

void BarWindow::configureWindow()
{
    setTitle(QStringLiteral("QBar"));
    setProperty("appId", QStringLiteral("qbar"));
    setProperty("desktopFileName", QStringLiteral("qbar"));
    // Per-window layer geometry so multiple bars (multi-monitor, top+bottom) can
    // differ; the layer-shell integration reads these, falling back to env.
    setProperty("qbarBarHeight", m_config.height);
    setProperty("qbarBarPosition", barPositionName(m_config.position));
    setProperty("qbarBarExclusive", m_config.exclusiveZone);
    const int windowHeight = !m_config.waylandLayerShell && m_config.height < 50 ? 50 : m_config.height;
    setMinimumHeight(windowHeight);
    setMaximumHeight(windowHeight);
    Qt::WindowFlags flags = Qt::Window | Qt::FramelessWindowHint;
    if (!m_config.waylandLayerShell) {
        flags |= Qt::WindowStaysOnTopHint;
    }
    setFlags(flags);
    setColor(Qt::transparent);
}

void BarWindow::buildLayout()
{
    const QVariantMap theme = themeMap(m_config);
    if (engine()->imageProvider(QStringLiteral("themeicon")) == nullptr) {
        engine()->addImageProvider(QStringLiteral("themeicon"), new ThemeIconProvider);
    }

    m_popupService = new QBarPopupService(engine(), theme, m_wm->workspaceModel(), m_wm, m_trayModel, m_cssTheme, this);
    m_popupService->setOverlayKeyboardFocus(m_config.popupKeyboardFocus);
    m_popupService->setBarWindow(this);
    connect(m_popupService, &QBarPopupService::popupClosed, this, [this](const QString &id) {
        if (id == m_calendarPopupId) {
            m_calendarPopupId.clear();
        }
    });
    connect(m_wm, &WindowManagerBackend::workspaceFocusEvent, m_popupService, &QBarPopupService::closeAll);

    rootContext()->setContextProperty(QStringLiteral("theme"), theme);
    // Directory of the active config file — relative custom-widget `source` paths
    // (runtime QML loaded by Bar.appletUrl) resolve against it.
    rootContext()->setContextProperty(QStringLiteral("configDir"),
                                      m_config.configFilePath.isEmpty()
                                          ? QString()
                                          : QFileInfo(m_config.configFilePath).absolutePath());
    rootContext()->setContextProperty(QStringLiteral("popupEmbossShaderAvailable"),
#ifdef QBAR_HAVE_POPUP_EMBOSS_SHADER
                                      true
#else
                                      false
#endif
    );
    rootContext()->setContextProperty(QStringLiteral("qbarPopups"), m_popupService);
    rootContext()->setContextProperty(QStringLiteral("workspaceModel"), m_wm->workspaceModel());
    rootContext()->setContextProperty(QStringLiteral("windowModel"), m_wm->windowModel());
    rootContext()->setContextProperty(QStringLiteral("taskbarConfig"), m_config.taskbar);
    rootContext()->setContextProperty(QStringLiteral("cpuConfig"), m_config.cpu);
    rootContext()->setContextProperty(QStringLiteral("memoryConfig"), m_config.memory);
    rootContext()->setContextProperty(QStringLiteral("networkConfig"), m_config.network);
    rootContext()->setContextProperty(QStringLiteral("cpuModel"), m_cpuModel);
    rootContext()->setContextProperty(QStringLiteral("temperatureModel"), m_temperatureModel);
    rootContext()->setContextProperty(QStringLiteral("networkModel"), m_networkModel);
    rootContext()->setContextProperty(QStringLiteral("networkManagerModel"), m_networkManagerModel);
    rootContext()->setContextProperty(QStringLiteral("brightnessModel"), m_brightnessModel);
    rootContext()->setContextProperty(QStringLiteral("caffeineModel"), m_caffeineModel);
    rootContext()->setContextProperty(QStringLiteral("soundModel"), m_soundModel);
    rootContext()->setContextProperty(QStringLiteral("mprisModel"), m_mprisModel);
    rootContext()->setContextProperty(QStringLiteral("capsLock"), m_capsLockMonitor);
    rootContext()->setContextProperty(QStringLiteral("calendarModel"), m_calendarModel);
    rootContext()->setContextProperty(QStringLiteral("wm"), m_wm);
    rootContext()->setContextProperty(QStringLiteral("i3Ipc"), m_wm);
    rootContext()->setContextProperty(QStringLiteral("trayModel"), m_trayModel);
    rootContext()->setContextProperty(QStringLiteral("batteryModel"), m_batteryModel);
    rootContext()->setContextProperty(QStringLiteral("diskModel"), m_diskModel);
    rootContext()->setContextProperty(QStringLiteral("bluetoothModel"), m_bluetoothModel);
    rootContext()->setContextProperty(QStringLiteral("powerProfilesModel"), m_powerProfilesModel);
    rootContext()->setContextProperty(QStringLiteral("cssTheme"), m_cssTheme);
    rootContext()->setContextProperty(QStringLiteral("dbus"), new DBusService(engine(), this));
    rootContext()->setContextProperty(QStringLiteral("customTools"), m_config.customTools);
    rootContext()->setContextProperty(QStringLiteral("appletNames"), m_config.applets);
    rootContext()->setContextProperty(QStringLiteral("leftApplets"), m_config.appletsLeft);
    rootContext()->setContextProperty(QStringLiteral("centerApplets"), m_config.appletsCenter);
    rootContext()->setContextProperty(QStringLiteral("rightApplets"), m_config.appletsRight);
    rootContext()->setContextProperty(QStringLiteral("barWindow"), this);

    setResizeMode(QQuickView::SizeRootObjectToView);
    setSource(QUrl(QStringLiteral("qrc:/Bar.qml")));
}

void BarWindow::positionAtTop()
{
    const QRect target = targetBarGeometry();
    setMinimumSize(target.size());
    setMaximumSize(target.size());
    setGeometry(target);
}

QRect BarWindow::targetBarGeometry() const
{
    const auto *screen = QGuiApplication::primaryScreen();
    if (screen == nullptr) {
        return QRect(0, 0, 900, m_config.height);
    }

    const auto area = m_config.waylandLayerShell ? screen->availableGeometry() : screen->geometry();
    const int targetHeight = !m_config.waylandLayerShell && m_config.height < 50 ? 50 : m_config.height;
    const int y = m_config.position == BarPosition::Bottom
        ? area.y() + area.height() - targetHeight
        : area.y();
    const int targetX = m_config.x >= 0 ? m_config.x : area.x();
    const int targetY = m_config.y >= 0 ? m_config.y : y;
    return QRect(targetX, targetY, area.width(), targetHeight);
}

QString BarWindow::testWindowCriteria() const
{
    if (m_swayNodeId >= 0) {
        return QStringLiteral("[con_id=%1]").arg(m_swayNodeId);
    }

    const QString pidCriteria = QStringLiteral("[pid=%1]").arg(static_cast<qlonglong>(getpid()));
    return QStringLiteral("%1; [app_id=\"qbar\"]; [title=\"QBar\"]").arg(pidCriteria);
}

void BarWindow::applyTestWindowRules()
{
    if (m_config.waylandLayerShell || m_wm == nullptr) {
        return;
    }

    if (m_swayNodeId >= 0) {
        const QString criteria = QStringLiteral("[con_id=%1]").arg(m_swayNodeId);
        m_wm->runCommand(QStringLiteral("%1 floating enable; %1 sticky enable; %1 border none").arg(criteria));
        QTimer::singleShot(80, this, SLOT(moveTestWindow()));
        return;
    }

    const QString pidCriteria = QStringLiteral("[pid=%1]").arg(static_cast<qlonglong>(getpid()));
    const QString appCriteria = QStringLiteral("[app_id=\"qbar\"]");
    const QString titleCriteria = QStringLiteral("[title=\"QBar\"]");
    m_wm->runCommand(QStringLiteral(
                         "%1 floating enable; %1 sticky enable; %1 border none; "
                         "%2 floating enable; %2 sticky enable; %2 border none; "
                         "%3 floating enable; %3 sticky enable; %3 border none")
                         .arg(pidCriteria, appCriteria, titleCriteria));
    QTimer::singleShot(80, this, SLOT(moveTestWindow()));
}

void BarWindow::moveTestWindow()
{
    if (m_config.waylandLayerShell || m_wm == nullptr) {
        return;
    }

    const QRect target = targetBarGeometry();
    qWarning() << "QBar test window target geometry:" << target;
    const int moveY = target.y();
    if (m_swayNodeId >= 0) {
        const QString criteria = QStringLiteral("[con_id=%1]").arg(m_swayNodeId);
        m_wm->runCommand(QStringLiteral("%1 resize set width %2 px height %3 px; %1 move absolute position %4 %5")
                             .arg(criteria)
                             .arg(target.width())
                             .arg(target.height())
                             .arg(target.x())
                             .arg(moveY));
        return;
    }

    const QString pidCriteria = QStringLiteral("[pid=%1]").arg(static_cast<qlonglong>(getpid()));
    const QString appCriteria = QStringLiteral("[app_id=\"qbar\"]");
    const QString titleCriteria = QStringLiteral("[title=\"QBar\"]");
    m_wm->runCommand(QStringLiteral(
                         "%1 resize set width %2 px height %3 px; %1 move absolute position %4 %5; "
                         "%6 resize set width %2 px height %3 px; %6 move absolute position %4 %5; "
                         "%7 resize set width %2 px height %3 px; %7 move absolute position %4 %5")
                         .arg(pidCriteria)
                         .arg(target.width())
                         .arg(target.height())
                         .arg(target.x())
                         .arg(moveY)
                         .arg(appCriteria)
                         .arg(titleCriteria));
}

void BarWindow::handleQbarNodeFound(qint64 nodeId)
{
    m_swayNodeId = nodeId;
    applyTestWindowRules();
    QTimer::singleShot(80, this, SLOT(moveTestWindow()));
}

void BarWindow::installTestWindowRule()
{
    if (m_config.waylandLayerShell || m_wm == nullptr) {
        return;
    }

    const QRect target = targetBarGeometry();
    const QString pidCriteria = QStringLiteral("[pid=%1]").arg(static_cast<qlonglong>(getpid()));
    const QString appCriteria = QStringLiteral("[app_id=\"qbar\"]");
    const QString titleCriteria = QStringLiteral("[title=\"QBar\"]");
    Q_UNUSED(target);
    const QString action = QStringLiteral("floating enable, sticky enable, border none");

    m_wm->runCommand(QStringLiteral("for_window %1 %2; for_window %3 %2; for_window %4 %2")
                         .arg(pidCriteria, action, appCriteria, titleCriteria));
}

void BarWindow::scheduleTestWindowRules()
{
    if (m_config.waylandLayerShell) {
        return;
    }

    applyTestWindowRules();
    QTimer::singleShot(120, m_wm, SLOT(requestTreeSnapshot()));
    QTimer::singleShot(150, this, SLOT(applyTestWindowRules()));
    QTimer::singleShot(350, m_wm, SLOT(requestTreeSnapshot()));
    QTimer::singleShot(500, this, SLOT(applyTestWindowRules()));
    QTimer::singleShot(900, m_wm, SLOT(requestTreeSnapshot()));
    QTimer::singleShot(1200, this, SLOT(applyTestWindowRules()));
    QTimer::singleShot(1500, m_wm, SLOT(requestTreeSnapshot()));
    QTimer::singleShot(1800, this, SLOT(moveTestWindow()));
}

void BarWindow::openCalendar(QObject *anchorObject)
{
    if (m_popupService == nullptr) {
        return;
    }

    if (!m_calendarPopupId.isEmpty()) {
        m_popupService->closePopup(m_calendarPopupId);
        m_calendarPopupId.clear();
        return;
    }

    const int popupWidth = 560;
    const int popupHeight = 380;
    const QSize popupSize(popupWidth, popupHeight);
    const int gap = 0;
    auto *anchorItem = qobject_cast<QQuickItem *>(anchorObject);
    QPoint anchorTopLeft = mapToGlobal(QPoint(0, 0));
    QPoint anchorBottomRight = mapToGlobal(QPoint(width(), height()));

    if (anchorItem != nullptr) {
        const QPointF itemTopLeft = anchorItem->mapToScene(QPointF(0.0, 0.0));
        const QPointF itemBottomRight = anchorItem->mapToScene(QPointF(anchorItem->width(), anchorItem->height()));
        anchorTopLeft = mapToGlobal(QPoint(qRound(itemTopLeft.x()), qRound(itemTopLeft.y())));
        anchorBottomRight = mapToGlobal(QPoint(qRound(itemBottomRight.x()), qRound(itemBottomRight.y())));
    }

    int x = anchorBottomRight.x() - popupWidth;
    int y = m_config.position == BarPosition::Bottom
        ? anchorTopLeft.y() - popupHeight - gap
        : anchorBottomRight.y() + gap;

    const QScreen *screen = QGuiApplication::screenAt(anchorTopLeft);
    if (screen != nullptr) {
        const QPoint clamped = clampedPopupPosition(screen->availableGeometry(), QPoint(x, y), popupSize);
        x = clamped.x();
        y = clamped.y();
    }

    QVariantMap properties;
    properties.insert(QStringLiteral("selectedDate"), QDate::currentDate());
    m_calendarPopupId = m_popupService->openPopup(QUrl(QStringLiteral("qrc:/popups/CalendarPopup.qml")),
                                                  properties,
                                                  x,
                                                  y,
                                                  popupWidth,
                                                  popupHeight,
                                                  QStringLiteral("calendar"));
    if (m_calendarPopupId.isEmpty()) {
        qWarning() << "QBar calendar popup failed to open";
    }
}

void BarWindow::openEvolutionCalendar()
{
    if (!calendarAppAvailable()) {
        return;
    }

    const QStringList arguments = {QStringLiteral("--component=calendar")};
    if (!QProcess::startDetached(m_evolutionCalendarExecutable, arguments)) {
        qWarning() << "QBar failed to launch Evolution Calendar" << m_evolutionCalendarExecutable << arguments;
    }
}

void BarWindow::cycleKeyboardLayout()
{
    if (m_wm != nullptr) {
        m_wm->cycleKeyboardLayout();
    }
}

void BarWindow::toggleCaffeine()
{
    if (m_caffeineModel != nullptr) {
        m_caffeineModel->toggle();
    }
}
