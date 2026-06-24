# Reproduce a clean i3/Xorg user environment (the friend's setup) on Debian, where
# the Qt6 QML runtime is split into many `qml6-module-*` packages — so a missing
# module makes only the applets that import it fail ("half the applets don't work").
#
# Build is x11-only (matches i3/Xorg). meson.build discovers the Qt include/tool
# dirs from the dependency itself, so Debian's multiarch layout works unmodified.
FROM debian:trixie

ENV DEBIAN_FRONTEND=noninteractive

# --- Build toolchain ---------------------------------------------------------
RUN apt-get update && apt-get install -y --no-install-recommends \
      build-essential meson ninja-build pkg-config python3 ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# --- Qt6 dev + private headers (x11barintegration.cpp uses QtGui private) -----
# qt6-svg-dev / qt6-websockets-dev are mandatory: meson.build requires the Svg and
# WebSockets modules (qbar imports them from QML), so the build fails without them.
RUN apt-get update && apt-get install -y --no-install-recommends \
      qt6-base-dev qt6-base-private-dev \
      qt6-declarative-dev qt6-declarative-private-dev \
      qt6-svg-dev qt6-websockets-dev \
    && rm -rf /var/lib/apt/lists/*

# --- Native library deps the meson build links against -----------------------
RUN apt-get update && apt-get install -y --no-install-recommends \
      libsqlite3-dev libpulse-dev \
      libxcb1-dev libxcb-ewmh-dev \
      libxkbcommon-dev libxkbregistry-dev \
      libedataserver1.2-dev libecal2.0-dev \
    && rm -rf /var/lib/apt/lists/*

# --- Qt6 QML RUNTIME modules: THIS is what reproduces the missing-applet bug --
# Each import in the qbar QML is a separate Debian package. Deliberately listed
# explicitly so we can comment one out and watch the matching applets fail.
RUN apt-get update && apt-get install -y --no-install-recommends \
      qml6-module-qtqml \
      qml6-module-qtqml-workerscript \
      qml6-module-qtquick \
      qml6-module-qtquick-window \
      qml6-module-qtquick-templates \
      qml6-module-qtquick-controls \
      qml6-module-qtquick-effects \
      qml6-module-qtquick-shapes \
      qml6-module-qtquick-layouts \
      qml6-module-qtcore \
      qml6-module-qtwebsockets \
    && rm -rf /var/lib/apt/lists/*

# --- Runtime session: i3 in a nested X server, GL via llvmpipe, dbus, tray ----
# NOTE: the three packages below the WM line are the ones a minimal i3 install
# typically LACKS, and each silently breaks a band of applets at runtime (this
# is the friend's "half the applets don't work", not missing qmltypes):
#   libqt6svg6 + qt6-svg-plugins -> Qt SVG support. On Debian the *library* and
#                             the *image-format plugin* are split: the plugin
#                             (qt6-svg-plugins) is what actually lets QImage / QML
#                             Image decode .svg. Without it every .svg icon fails
#                             with "Unsupported image format". (Arch's qt6-svg
#                             bundles both, so one package fixes it there.)
#   adwaita-icon-theme      -> the symbolic icons the themeicon:// provider serves
#                             (network/bluetooth/battery status glyphs).
#   evolution-data-server   -> the EDS *daemon* (Sources5 D-Bus service) the
#                             Calendar applet needs; the -dev libs alone are not
#                             enough — the service must be installable/launchable.
RUN apt-get update && apt-get install -y --no-install-recommends \
      i3-wm dbus-x11 x11-xserver-utils x11-utils \
      libgl1-mesa-dri libglx-mesa0 \
      fonts-dejavu-core fonts-font-awesome \
      procps gdb \
      libqt6svg6 qt6-svg-plugins \
      adwaita-icon-theme hicolor-icon-theme \
      evolution-data-server \
    && rm -rf /var/lib/apt/lists/*

ENV QT_QPA_PLATFORM=xcb \
    LIBGL_ALWAYS_SOFTWARE=1 \
    QT_LOGGING_RULES="qt.qml.binding.removal.info=false"

WORKDIR /src
COPY inside.sh /usr/local/bin/inside.sh
RUN chmod +x /usr/local/bin/inside.sh
ENTRYPOINT ["/usr/local/bin/inside.sh"]
