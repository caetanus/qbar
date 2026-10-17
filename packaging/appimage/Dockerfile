# qbar AppImage build image — manylinux_2_34 (AlmaLinux 9, glibc 2.34).
#
# 2_34 rather than 2_28: the official Qt 6.11 binaries (installed below with
# aqtinstall, private headers included) are built on RHEL 9 and need glibc 2.34.
# EL9 also ships everything else qbar links (libecal-2.0, xkbregistry, …), so the
# AppImage runs on Ubuntu 22.04+, Debian 12+, RHEL/Alma/Rocky 9+, Fedora and Arch.
FROM quay.io/pypa/manylinux_2_34_x86_64

ARG QT_VERSION=6.11.2

RUN dnf -y install dnf-plugins-core epel-release \
 && dnf config-manager --set-enabled crb \
 && dnf -y install \
      cmake ninja-build file desktop-file-utils patchelf \
      evolution-data-server-devel libxkbcommon-devel libxkbcommon-x11-devel \
      pulseaudio-libs-devel pipewire-devel sqlite-devel pam-devel dbus-devel \
      wayland-devel wayland-protocols-devel \
      libxcb-devel xcb-util-wm-devel xcb-util-cursor-devel xcb-util-image-devel \
      xcb-util-keysyms-devel xcb-util-renderutil-devel \
      mesa-libGL-devel mesa-libEGL-devel fontconfig-devel freetype-devel \
 && dnf clean all \
 # Linux-PAM < 1.5.2 ships no pam.pc; qbar-lock finds pam through pkg-config.
 && { test -f /usr/lib64/pkgconfig/pam.pc || printf '%s\n' \
      'prefix=/usr' 'libdir=${prefix}/lib64' 'includedir=${prefix}/include' '' \
      'Name: PAM' 'Description: Pluggable Authentication Modules' 'Version: 1.5.1' \
      'Libs: -L${libdir} -lpam' 'Cflags: -I${includedir}' > /usr/lib64/pkgconfig/pam.pc; }

# EL9's meson (0.63) is too old for qbar; aqtinstall fetches the official Qt.
RUN python3.12 -m pip install --root-user-action=ignore --no-cache-dir meson aqtinstall \
 && ln -sf /opt/python/cp312-cp312/bin/meson /usr/local/bin/meson \
 && python3.12 -m aqt install-qt linux desktop ${QT_VERSION} linux_gcc_64 \
      -m qtwebsockets qtimageformats qtshadertools -O /opt/qt \
 && rm -rf /root/.cache aqtinstall.log

# Qt 6.11's private QtWaylandClient headers name ::wl_fixes (wayland-client 1.24);
# EL9 has 1.21. Only the declarations matter — Qt carries its own interface code.
ARG WAYLAND_VERSION=1.24.0
RUN dnf -y install expat-devel libffi-devel libxml2-devel && dnf clean all \
 && curl -fsSL https://gitlab.freedesktop.org/wayland/wayland/-/releases/${WAYLAND_VERSION}/downloads/wayland-${WAYLAND_VERSION}.tar.xz \
      | tar -xJ -C /tmp \
 && meson setup /tmp/wl /tmp/wayland-${WAYLAND_VERSION} --prefix=/usr/local --libdir=lib64 \
      -Ddocumentation=false -Dtests=false -Ddtd_validation=false \
 && meson install -C /tmp/wl --quiet && rm -rf /tmp/wl /tmp/wayland-${WAYLAND_VERSION}
ENV PKG_CONFIG_PATH=/usr/local/lib64/pkgconfig:/usr/local/share/pkgconfig

# AppImage tooling (static AppImages, run with --appimage-extract-and-run: no FUSE in docker).
RUN cd /usr/local/bin \
 && curl -fsSLo linuxdeploy https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage \
 && curl -fsSLo linuxdeploy-plugin-qt https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage \
 && curl -fsSLo appimagetool https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage \
 && chmod +x linuxdeploy linuxdeploy-plugin-qt appimagetool

ENV QTDIR=/opt/qt/${QT_VERSION}/gcc_64
ENV PATH=${QTDIR}/bin:${QTDIR}/libexec:${PATH} \
    CMAKE_PREFIX_PATH=${QTDIR} \
    QMAKE=${QTDIR}/bin/qmake \
    LD_LIBRARY_PATH=${QTDIR}/lib \
    APPIMAGE_EXTRACT_AND_RUN=1
