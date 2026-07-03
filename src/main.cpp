#include "barwindow.h"
#include "config.h"
#include "crashguard.h"
#include "customtool/customtoolmodel.h"
#include "graphics/sparkline.h"
#include "qml/qbaripc.h"

#include <QApplication>
#include <QCalendarWidget>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QEvent>
#include <QFrame>
#include <QGuiApplication>
#include <QHash>
#include <QIcon>
#include <QKeyEvent>
#include <QLibraryInfo>
#include <QLocale>
#include <QProcess>
#include <QScreen>
#include <QSet>
#include <QQuickWindow>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QTranslator>
#include <QVBoxLayout>
#include <QtQml/qqml.h>
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#ifdef Q_OS_LINUX
#include <sys/prctl.h>
#include <sys/resource.h>
#endif

namespace {

class CalendarPopupFrame final : public QFrame {
public:
    using QFrame::QFrame;

protected:
    bool event(QEvent *event) override
    {
        if (event->type() == QEvent::KeyPress) {
            auto *keyEvent = static_cast<QKeyEvent *>(event);
            if (keyEvent->key() == Qt::Key_Escape) {
                close();
                return true;
            }
        }

        return QFrame::event(event);
    }
};

bool hasArg(int argc, char *argv[], const char *name)
{
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], name) == 0) {
            return true;
        }
    }

    return false;
}

QByteArray argValue(int argc, char *argv[], const char *name, const char *fallback)
{
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], name) == 0) {
            return QByteArray(argv[i + 1]);
        }
    }

    return QByteArray(fallback);
}

// --- Log verbosity -----------------------------------------------------------------
// A normal run shows only Warning and above (clean output for a release); `-v` adds Info,
// `-vv` adds Debug. The threshold is enforced by a message handler keyed on the message
// TYPE — independent of per-category logging rules — so it's predictable.
QtMsgType g_logThreshold = QtWarningMsg;

int logRank(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg: return 0;
    case QtInfoMsg: return 1;
    case QtWarningMsg: return 2;
    case QtCriticalMsg: return 3;
    case QtFatalMsg: return 4;
    }
    return 4;
}

void qbarMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    if (logRank(type) >= logRank(g_logThreshold)) {
        const QByteArray line = qFormatLogMessage(type, context, message).toLocal8Bit();
        fputs(line.constData(), stderr);
        fputc('\n', stderr);
    }
    if (type == QtFatalMsg) {
        abort();
    }
}

// -v / --verbose = +1 (Info), -vv = +2 (Debug); repeatable.
int verbosityLevel(int argc, char *argv[])
{
    int level = 0;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "-v") == 0 || std::strcmp(argv[i], "--verbose") == 0) {
            level += 1;
        } else if (std::strcmp(argv[i], "-vv") == 0) {
            level += 2;
        }
    }
    return level;
}

void configureWaylandLayerShellEnvironment(int argc, char *argv[])
{
    if (hasArg(argc, argv, "--no-wayland-layer-shell") || hasArg(argc, argv, "--calendar-popup")) {
        return;
    }

    const char *platformValue = std::getenv("QT_QPA_PLATFORM");
    const char *sessionValue = std::getenv("XDG_SESSION_TYPE");
    const char *waylandDisplayValue = std::getenv("WAYLAND_DISPLAY");
    const QByteArray platform = platformValue != nullptr ? QByteArray(platformValue) : QByteArray();
    const QByteArray session = sessionValue != nullptr ? QByteArray(sessionValue) : QByteArray();
    const bool hasWaylandDisplay = waylandDisplayValue != nullptr && *waylandDisplayValue != '\0';
    if (!platform.contains("wayland") && session != QByteArray("wayland") && !hasWaylandDisplay) {
        return;
    }

    const QByteArray binaryDir = QByteArray(std::filesystem::absolute(argv[0]).parent_path().string().c_str());
    qputenv("QT_WAYLAND_SHELL_INTEGRATION", "qbar-layer-shell");
    qputenv("QT_PLUGIN_PATH", binaryDir);
    qputenv("QBAR_LAYER_POSITION", argValue(argc, argv, "--position", "top"));
    qputenv("QBAR_LAYER_HEIGHT", argValue(argc, argv, "--height", "28"));
    qputenv("QBAR_LAYER_EXCLUSIVE", hasArg(argc, argv, "--no-exclusive-zone") ? "0" : "1");
    qWarning() << "QBar using Wayland shell integration: qbar-layer-shell";
}

int calendarPopupCoordinate(int argc, char *argv[], const char *name, int fallback)
{
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], name) == 0) {
            return std::atoi(argv[i + 1]);
        }
    }

    return fallback;
}

QString calendarPopupString(int argc, char *argv[], const char *name, const QString &fallback)
{
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], name) == 0) {
            return QString::fromLocal8Bit(argv[i + 1]);
        }
    }

    return fallback;
}

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

QString gtkIconThemeName()
{
    const QString configHome = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    const QString gtk3Settings = configHome + QStringLiteral("/gtk-3.0/settings.ini");
    QSettings gtk3(gtk3Settings, QSettings::IniFormat);
    const QString gtk3Theme = gtk3.value(QStringLiteral("Settings/gtk-icon-theme-name")).toString();
    if (!gtk3Theme.isEmpty()) {
        return gtk3Theme;
    }

    const QString gtk4Settings = configHome + QStringLiteral("/gtk-4.0/settings.ini");
    QSettings gtk4(gtk4Settings, QSettings::IniFormat);
    const QString gtk4Theme = gtk4.value(QStringLiteral("Settings/gtk-icon-theme-name")).toString();
    if (!gtk4Theme.isEmpty()) {
        return gtk4Theme;
    }

    QProcess gsettings;
    gsettings.start(QStringLiteral("gsettings"),
                    {QStringLiteral("get"),
                     QStringLiteral("org.gnome.desktop.interface"),
                     QStringLiteral("icon-theme")});
    if (gsettings.waitForFinished(250) && gsettings.exitStatus() == QProcess::NormalExit && gsettings.exitCode() == 0) {
        QString theme = QString::fromLocal8Bit(gsettings.readAllStandardOutput()).trimmed();
        if (theme.startsWith(QLatin1Char('\'')) && theme.endsWith(QLatin1Char('\''))) {
            theme = theme.mid(1, theme.size() - 2);
        }
        if (!theme.isEmpty()) {
            return theme;
        }
    }

    return {};
}

void configureIconTheme()
{
    QStringList searchPaths;
    const QString home = QDir::homePath();
    searchPaths << home + QStringLiteral("/.icons")
                << home + QStringLiteral("/.local/share/icons")
                << QStringLiteral("/usr/local/share/icons")
                << QStringLiteral("/usr/share/icons");
    QIcon::setThemeSearchPaths(searchPaths);

    const QString gtkTheme = gtkIconThemeName();
    if (!gtkTheme.isEmpty()) {
        QIcon::setThemeName(gtkTheme);
    } else if (QIcon::themeName().isEmpty()) {
        QIcon::setThemeName(QStringLiteral("hicolor"));
    }
    if (QIcon::fallbackThemeName().isEmpty()) {
        QIcon::setFallbackThemeName(QStringLiteral("hicolor"));
    }
}

void configureCoreDumps()
{
#ifdef Q_OS_LINUX
    if (prctl(PR_SET_DUMPABLE, 1) != 0) {
        qWarning() << "QBar failed to mark process dumpable:" << std::strerror(errno);
    }

    rlimit limit {};
    if (getrlimit(RLIMIT_CORE, &limit) != 0) {
        qWarning() << "QBar failed to read core dump limit:" << std::strerror(errno);
        return;
    }

    if (limit.rlim_max == 0) {
        qWarning() << "QBar core dumps are disabled by the parent process hard limit";
        return;
    }

    const rlim_t target = limit.rlim_max == RLIM_INFINITY ? RLIM_INFINITY : limit.rlim_max;
    if (limit.rlim_cur != target) {
        limit.rlim_cur = target;
        if (setrlimit(RLIMIT_CORE, &limit) != 0) {
            qWarning() << "QBar failed to enable core dumps:" << std::strerror(errno);
            return;
        }
    }

    qWarning() << "QBar core dump limit:"
               << (limit.rlim_cur == RLIM_INFINITY ? QStringLiteral("unlimited") : QString::number(limit.rlim_cur));
#endif
}

// Owns the live set of BarWindows and keeps it in sync with the connected monitors.
// Each logical bar config is replicated onto every monitor it targets (see
// barConfigTargetsScreen); as monitors are plugged or unplugged, bars are created
// and torn down to match. Bars are keyed by "<configIndex>@<screenName>", so a
// config keeps its array index — the per-bar live config reload relies on it.
class BarManager final : public QObject {
public:
    explicit BarManager(QList<BarConfig> configs, QObject *parent = nullptr)
        : QObject(parent)
        , m_configs(std::move(configs))
    {
        connect(qApp, &QGuiApplication::screenAdded, this, [this](QScreen *) { reconcile(); });
        connect(qApp, &QGuiApplication::screenRemoved, this, [this](QScreen *) { reconcile(); });
        reconcile();
    }

private:
    void reconcile()
    {
        const auto screens = QGuiApplication::screens();
        QSet<QString> liveScreens;
        for (QScreen *screen : screens) {
            liveScreens.insert(screen->name());
            for (int i = 0; i < m_configs.size(); ++i) {
                if (!barConfigTargetsScreen(m_configs.at(i), screen->name())) {
                    continue;
                }
                const QString key = barKey(i, screen->name());
                if (m_bars.contains(key)) {
                    continue;
                }
                auto *bar = new BarWindow(m_configs.at(i));
                bar->setScreen(screen);
                bar->show();
                m_bars.insert(key, bar);
            }
        }

        // Tear down bars whose target monitor was unplugged.
        for (auto it = m_bars.begin(); it != m_bars.end();) {
            if (liveScreens.contains(screenNameOfKey(it.key()))) {
                ++it;
            } else {
                it.value()->deleteLater();
                it = m_bars.erase(it);
            }
        }
    }

    static QString barKey(int index, const QString &screenName)
    {
        return QString::number(index) + QLatin1Char('@') + screenName;
    }

    static QString screenNameOfKey(const QString &key)
    {
        return key.mid(key.indexOf(QLatin1Char('@')) + 1);
    }

    QList<BarConfig> m_configs;
    QHash<QString, BarWindow *> m_bars;
};

} // namespace

int main(int argc, char *argv[])
{
    const int verbosity = verbosityLevel(argc, argv);
    g_logThreshold = verbosity >= 2 ? QtDebugMsg
                   : verbosity == 1 ? QtInfoMsg
                                    : QtWarningMsg;
    qInstallMessageHandler(qbarMessageHandler);

    configureCoreDumps();
    configureWaylandLayerShellEnvironment(argc, argv);
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGLRhi);

    QApplication app(argc, argv);
    // After QApplication so the Wayland platform plugin (and libQt6WaylandClient, where the
    // recoverable doHandleFrameCallback lives) is loaded and dlsym can find it.
    installCrashGuard();
    QCoreApplication::setApplicationName(QStringLiteral("qbar"));
    QCoreApplication::setOrganizationName(QStringLiteral("qbar"));
    QGuiApplication::setDesktopFileName(QStringLiteral("qbar"));
    qmlRegisterType<CustomToolModel>("QBar", 1, 0, "CustomToolModel");
    qmlRegisterType<Sparkline>("QBar", 1, 0, "Sparkline");
    configureIconTheme();

    // LANG/LC_MESSAGES support: Qt's own catalogs (built-in components such as
    // QCalendarWidget) first, then ours (":/i18n", embedded via meson
    // compile_translations). Both live on main's stack, outliving app.exec().
    QTranslator qtTranslator;
    if (qtTranslator.load(QLocale(), QStringLiteral("qt"), QStringLiteral("_"),
                          QLibraryInfo::path(QLibraryInfo::TranslationsPath))) {
        QCoreApplication::installTranslator(&qtTranslator);
    }
    QTranslator translator;
    if (translator.load(QLocale(), QStringLiteral("qbar"), QStringLiteral("_"),
                        QStringLiteral(":/i18n"))) {
        QCoreApplication::installTranslator(&translator);
    }

    if (hasArg(argc, argv, "--calendar-popup")) {
        const QString popupAppId = calendarPopupString(argc, argv, "--popup-app-id", QStringLiteral("qbar-calendar-popup"));
        const QString popupTitle = calendarPopupString(argc, argv, "--popup-title", QStringLiteral("QBar Calendar Popup"));
        QGuiApplication::setDesktopFileName(popupAppId);

        auto popup = new CalendarPopupFrame(nullptr, Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
        popup->setAttribute(Qt::WA_DeleteOnClose, true);
        popup->setVisible(false);
        popup->setObjectName(QStringLiteral("CalendarPopup"));
        popup->setProperty("appId", popupAppId);
        popup->setProperty("desktopFileName", popupAppId);
        popup->setWindowTitle(popupTitle);
        popup->setFocusPolicy(Qt::StrongFocus);
        popup->setStyleSheet(QStringLiteral(
            "QFrame#CalendarPopup {"
            "background: rgba(38, 48, 57, 220);"
            "border: 1px solid rgba(255, 255, 255, 48);"
            "border-radius: 2px;"
            "}"
            "QCalendarWidget { color: #eef2f7; }"));

        auto *layout = new QVBoxLayout(popup);
        layout->setContentsMargins(10, 10, 10, 10);
        layout->addWidget(new QCalendarWidget(popup));
        popup->resize(340, 280);

        int x = calendarPopupCoordinate(argc, argv, "--x", 0);
        int y = calendarPopupCoordinate(argc, argv, "--y", 0);
        const int revealDelay = calendarPopupCoordinate(argc, argv, "--reveal-delay", 260);

        const QScreen *screen = QGuiApplication::screenAt(QPoint(x, y));
        if (screen == nullptr) {
            screen = QGuiApplication::primaryScreen();
        }
        if (screen != nullptr) {
            const QPoint clamped = clampedPopupPosition(screen->availableGeometry(), QPoint(x, y), popup->size());
            x = clamped.x();
            y = clamped.y();
        }

        popup->move(x, y);
        QObject::connect(popup, SIGNAL(destroyed(QObject*)), &app, SLOT(quit()));
        if (revealDelay > 0) {
            popup->setWindowOpacity(0.0);
        }
        popup->setVisible(true);
        popup->raise();
        popup->setFocus(Qt::PopupFocusReason);
        if (revealDelay > 0) {
            QTimer::singleShot(revealDelay, popup, [popup]() {
                popup->setWindowOpacity(1.0);
                popup->raise();
            });
        }
        return app.exec();
    }

    // Each config entry (a single object → one bar; an array → several, e.g.
    // top+bottom) is a logical bar. The BarManager replicates each onto every
    // monitor its `output` targets (absent = all), and adds/removes bars as
    // monitors are hotplugged. The layer-shell integration reads each bar's
    // geometry from per-window properties (see BarWindow), so bars can differ.
    auto *barManager = new BarManager(loadConfigs(), &app);
    Q_UNUSED(barManager);

    // JSON IPC over a QLocalSocket (open/toggle popups, e.g. from keyboard shortcuts).
    QbarIpc::instance()->start();

    return app.exec();
}
