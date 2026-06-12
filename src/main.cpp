#include "barwindow.h"
#include "config.h"

#include <QApplication>
#include <QCalendarWidget>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QEvent>
#include <QFrame>
#include <QGuiApplication>
#include <QIcon>
#include <QKeyEvent>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <filesystem>

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

} // namespace

int main(int argc, char *argv[])
{
    configureWaylandLayerShellEnvironment(argc, argv);

    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("qbar"));
    QCoreApplication::setOrganizationName(QStringLiteral("qbar"));
    QGuiApplication::setDesktopFileName(QStringLiteral("qbar"));
    configureIconTheme();

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

    BarWindow window(loadConfig());
    window.show();

    return app.exec();
}
