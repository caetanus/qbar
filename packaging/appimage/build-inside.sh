#!/bin/bash
# Runs inside the manylinux image (see build.sh): /src = qbar checkout (read-only),
# /out = where the AppImage lands. Everything else happens in /tmp.
set -euo pipefail

SRC=/src
OUT=/out
BUILD=/build   # a volume (see build.sh) so rebuilds are incremental
APPDIR=/tmp/AppDir
VERSION="${QBAR_VERSION:-$(git -C "$SRC" describe --tags --always 2>/dev/null || echo dev)}"

meson setup "$BUILD" "$SRC" $( [ -f "$BUILD/build.ninja" ] && echo --reconfigure ) \
    --prefix=/usr --buildtype=release -Dtests=false \
    -Dwayland=enabled -Dx11=enabled -Dlockscreen=enabled \
    -Dwireplumber=disabled -Dprivacy=disabled  # libpipewire loads SPA plugins from a host path; cannot be bundled
meson compile -C "$BUILD" ${JOBS:+-j "$JOBS"}
rm -rf "$APPDIR"
DESTDIR="$APPDIR" meson install -C "$BUILD" --quiet

# PAM config must come from the host (/etc/pam.d/qbar-lock); headers/static lib are dev-only.
# meson's libdir is lib64 on EL; linuxdeploy bundles into usr/lib.
LIBDIR="$APPDIR/usr/lib64"; [ -d "$LIBDIR" ] || LIBDIR="$APPDIR/usr/lib"
rm -rf "$APPDIR/etc" "$APPDIR/usr/include" "$LIBDIR/libqmlcssengine.a" "$LIBDIR/pkgconfig"
# Shell-integration plugins go where the bundled Qt looks for plugins.
mkdir -p "$APPDIR/usr/plugins"
mv "$LIBDIR/qt6/plugins/wayland-shell-integration" "$APPDIR/usr/plugins/"
rm -rf "$LIBDIR/qt6"
rmdir "$LIBDIR" 2>/dev/null || true

# One desktop entry/icon for the image (qbar.desktop is NoDisplay with a generic icon).
rm -f "$APPDIR/usr/share/applications/"*.desktop
cat > "$APPDIR/usr/share/applications/qbar.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=QBar
GenericName=Status Bar
Comment=Wayland/X11 status bar
Exec=qbar
Icon=qbar
Terminal=false
NoDisplay=true
Categories=System;
EOF
install -Dm644 "$SRC/data/qbar.svg" "$APPDIR/usr/share/icons/hicolor/scalable/apps/qbar.svg"

export QML_SOURCES_PATHS="$SRC/qml:$SRC/config/widgets:$SRC/vendor/qml-css-engine/src"
export EXTRA_PLATFORM_PLUGINS="libqwayland.so"
export EXTRA_QT_MODULES="waylandclient;svg;websockets"
export EXTRA_PLUGINS="wayland-shell-integration,wayland-decoration-client,tls,iconengines,imageformats"

# The bundled EDS client (calendar) opens org.gnome.Evolution.DefaultSources & co. and
# GLib *aborts* when a schema is missing — ship them; AppRun adds the dir to the search.
mkdir -p "$APPDIR/usr/share/glib-2.0/schemas"
cp /usr/share/glib-2.0/schemas/org.gnome.Evolution*.gschema.xml \
   /usr/share/glib-2.0/schemas/org.gnome.evolution*.gschema.xml "$APPDIR/usr/share/glib-2.0/schemas/"
glib-compile-schemas "$APPDIR/usr/share/glib-2.0/schemas"

# linuxdeploy-plugin-qt does not know the client-side EGL/dmabuf integrations;
# without them QtQuick cannot render through the GPU on Wayland. Copy them by hand.
mkdir -p "$APPDIR/usr/plugins/wayland-graphics-integration-client"
cp "$QTDIR"/plugins/wayland-graphics-integration-client/*.so "$APPDIR/usr/plugins/wayland-graphics-integration-client/"

# Excluded, taken from the host:
#   libpam  — the host's PAM stack (modules + config) must match the libpam that loads it.
#   glib    — the host's GIO modules (dconf, gvfs) are built against the host glib; a bundled
#             (older) glib loading them fails on missing symbols. glib's ABI only grows, and
#             every target distro has >= EL9's 2.68.
mkdir -p "$APPDIR/usr/lib/camel-providers"   # EDS_CAMEL_PROVIDER_DIR, see AppRun
touch "$APPDIR/usr/lib/camel-providers/.keep"

linuxdeploy --appdir "$APPDIR" \
    -e "$APPDIR/usr/bin/qbar" -e "$APPDIR/usr/bin/qbar-ipc" -e "$APPDIR/usr/bin/qbar-lock" \
    -d "$APPDIR/usr/share/applications/qbar.desktop" \
    -i "$APPDIR/usr/share/icons/hicolor/scalable/apps/qbar.svg" \
    --exclude-library 'libpam.so*' \
    --exclude-library 'libglib-2.0.so*' --exclude-library 'libgobject-2.0.so*' \
    --exclude-library 'libgio-2.0.so*' --exclude-library 'libgmodule-2.0.so*' \
    --exclude-library 'libgthread-2.0.so*' \
    --plugin qt

# linuxdeploy-plugin-qt re-deploys excluded libraries for the Qt plugins; drop them again.
rm -f "$APPDIR"/usr/lib/lib{glib,gio,gobject,gmodule,gthread}-2.0.so* "$APPDIR"/usr/lib/libpam.so*

# Plugins we placed by hand: resolve their Qt deps from the bundle, like Qt's own plugins
# (not via `linuxdeploy -l`, which would also copy them into usr/lib).
for p in "$APPDIR"/usr/plugins/wayland-shell-integration/libqbar-*.so \
         "$APPDIR"/usr/plugins/wayland-graphics-integration-client/*.so; do
    patchelf --set-rpath '$ORIGIN/../../lib' "$p"
done
missing=$(for p in "$APPDIR"/usr/plugins/*/*.so; do
    LD_LIBRARY_PATH="$APPDIR/usr/lib" ldd "$p" | { grep "not found" || true; } | sed "s|^|$(basename "$p"): |"; done)
[ -z "$missing" ] || { echo "unresolved plugin deps:"; echo "$missing"; exit 1; }

install -m755 "$SRC/packaging/appimage/AppRun" "$APPDIR/AppRun"

mkdir -p "$OUT"
ARCH=x86_64 VERSION="$VERSION" appimagetool --no-appstream "$APPDIR" "$OUT/qbar-$VERSION-x86_64.AppImage"
echo "built $OUT/qbar-$VERSION-x86_64.AppImage"
