#include "platformbarintegration.h"

#include <QGuiApplication>
#include <QString>
#include <cstdlib>

bool applyX11BarIntegration(QWidget *window, const BarConfig &config);
bool applyWaylandLayerShellIntegration(QWidget *window, const BarConfig &config);

void applyPlatformBarIntegration(QWidget *window, const BarConfig &config)
{
    const QString platform = QGuiApplication::platformName().toLower();

#if QBAR_HAVE_WAYLAND
    const bool layerShellPluginActive = std::getenv("QT_WAYLAND_SHELL_INTEGRATION") != nullptr
        && QString::fromLocal8Bit(std::getenv("QT_WAYLAND_SHELL_INTEGRATION")) == QStringLiteral("qbar-layer-shell");
    if (platform.contains(QStringLiteral("wayland")) && layerShellPluginActive) {
        Q_UNUSED(window);
        Q_UNUSED(config);
        return;
    }
    if (platform.contains(QStringLiteral("wayland")) && applyWaylandLayerShellIntegration(window, config)) {
        return;
    }
#endif

#if QBAR_HAVE_X11
    if ((platform.contains(QStringLiteral("xcb")) || platform.contains(QStringLiteral("x11")))
        && applyX11BarIntegration(window, config)) {
        return;
    }
#endif

    Q_UNUSED(window);
    Q_UNUSED(config);
}
