project(
  'qbar',
  'c',
  'cpp',
  version: '0.1.0',
  license: 'MIT',
  default_options: [
    'b_pie=false',
    'cpp_std=c++20',
    'warning_level=2',
  ],
)

qt6 = import('qt6')

add_project_arguments('-fPIC', language: 'cpp')

# Qt's canonical export is CMake, not pkg-config — resolve it through the qt6
# module's dependency with an explicit modules list (method: 'cmake'), the
# idiomatic meson approach, instead of distro-optional per-module .pc files.
#
# Svg and WebSockets are MANDATORY here even though qbar only imports them from
# QML: requiring them at build time closes the "compiles fine, breaks at runtime"
# trap on a stock i3 box (every .svg icon failing, the Bitcoin widget not loading).
# On Arch qt6-svg / qt6-websockets ship the lib + image plugin + QML module as one
# package, so a build that finds them is guaranteed the runtime pieces too.
qt6_dep = dependency(
  'qt6',
  modules: [
    'Core', 'Gui', 'Widgets', 'Network', 'Qml', 'Quick', 'DBus',
    'Svg',         # SVG image support — themeicon + every .svg applet icon
    'WebSockets',  # QtWebSockets QML import — Bitcoin ticker widget
  ],
)

qt_deps = [
  qt6_dep,
  dependency('sqlite3'),
  dependency('xkbregistry'),
  dependency('libedataserver-1.2'),
  dependency('libecal-2.0'),
  dependency('libpulse'),
]

# The CSS engine — born in src/css/, now vendored (vendor/qml-css-engine, a git
# submodule; subprojects/qml-css-engine is a symlink meson's subproject() follows).
qml_css_engine_sub = subproject('qml-css-engine')
qml_css_dep = qml_css_engine_sub.get_variable('qml_css_engine_dep')
qt_deps += [qml_css_dep]

# Discover the Qt host tool dir + include root from whichever backend resolved
# the dependency — qmake (config-tool) reports QT_HOST_BINS/QT_INSTALL_HEADERS,
# pkg-config reports bindir/includedir. Never assumed (Debian's multiarch prefix
# differs from Arch's /usr/include/qt6).
qt_host_bins = qt6_dep.get_variable(
  configtool: 'QT_HOST_BINS',
  pkgconfig: 'bindir',
  default_value: '',
)
qt_includedir = qt6_dep.get_variable(
  configtool: 'QT_INSTALL_HEADERS',
  pkgconfig: 'includedir',
  default_value: '',
)
if qt_includedir == ''
  error('Could not determine the Qt6 include directory from the Qt6Core dependency.')
endif
qsb_program = find_program('qsb', 'qsb-qt6', required: false)
if not qsb_program.found() and qt_host_bins != ''
  qsb_program = find_program(join_paths(qt_host_bins, 'qsb'), required: false)
endif
python3 = find_program('python3', required: true)
popup_emboss_shader_enabled = qsb_program.found()
popup_emboss_shader_qrc = []
if popup_emboss_shader_enabled
  popup_emboss_shader_source = files('qml/shaders/PopupEmboss.frag')
  popup_emboss_shader_qsb = custom_target(
    'popup-emboss-frag-qsb',
    input: popup_emboss_shader_source,
    output: 'PopupEmboss.frag.qsb',
    command: [qsb_program, '--qt6', '-o', '@OUTPUT@', '@INPUT@'],
    build_by_default: true,
  )
  popup_emboss_shader_qsb_path = join_paths(meson.project_build_root(), 'PopupEmboss.frag.qsb')
  popup_emboss_shader_conf = configuration_data()
  popup_emboss_shader_conf.set('POPUP_EMBOSS_QSB', popup_emboss_shader_qsb_path)
  popup_emboss_shader_qrc = configure_file(
    input: 'qml/qbar-shaders.qrc.in',
    output: 'qbar-shaders.qrc',
    configuration: popup_emboss_shader_conf,
  )
endif

platform_deps = []
platform_sources = []
# Q_OBJECT headers whose .cpp is platform-gated must be moc'd ONLY when that
# platform is built — otherwise the moc unit references a constructor/dtor/vtable
# that the (excluded) .cpp never provides, breaking the link on the other platform.
platform_moc_headers = []
platform_generated = []
platform_args = []
platform_includes = []
wayland_shell_plugin = []

if get_option('qml_debug')
  # Pulls in the QML debug services so `-qmljsdebugger=port:N` activates the
  # profiler/JS-debugger connector. Profiling builds only — never ship enabled.
  platform_args += ['-DQT_QML_DEBUG', '-DQT_QML_DEBUG_NO_WARNING']
endif

if popup_emboss_shader_enabled
  platform_args += ['-DQBAR_HAVE_POPUP_EMBOSS_SHADER=1']
  platform_generated += [popup_emboss_shader_qsb]
endif

qtgui_version = dependency('Qt6Gui').version()
qtgui_private_include = include_directories(
  qt_includedir / 'QtGui' / qtgui_version,
  qt_includedir / 'QtGui' / qtgui_version / 'QtGui',
)

x11_dep = dependency('xcb', required: get_option('x11'))
xcb_ewmh_dep = dependency('xcb-ewmh', required: get_option('x11'))
xcb_xkb_dep = dependency('xcb-xkb', required: get_option('x11'))
xcb_screensaver_dep = dependency('xcb-screensaver', required: get_option('x11'))
if x11_dep.found() and xcb_ewmh_dep.found() and xcb_xkb_dep.found() and xcb_screensaver_dep.found()
  platform_deps += [x11_dep, xcb_ewmh_dep, xcb_xkb_dep, xcb_screensaver_dep]
  platform_sources += files(
    'src/platform/x11barintegration.cpp',
    'src/platform/x11keyboardlayout.cpp',
  )
  platform_moc_headers += files('src/platform/x11keyboardlayout.h')
  platform_includes += qtgui_private_include
  platform_args += ['-DQBAR_HAVE_X11=1']
endif

wayland_dep = dependency('wayland-client', required: get_option('wayland'))
wayland_scanner = find_program('wayland-scanner', required: get_option('wayland'))
wayland_protocols_dep = dependency('wayland-protocols', required: get_option('wayland'))
# Set once the Wayland shell plugins (layer-shell + session-lock) are built; the
# qbar-lock target keys its Wayland lock backend off this.
have_wayland_shell = false
session_lock_header = []
session_lock_code = []
if wayland_dep.found() and wayland_scanner.found() and wayland_protocols_dep.found()
  # Ask wayland-protocols where it installed its XML, rather than assuming /usr/share.
  wl_protocols_dir = wayland_protocols_dep.get_variable(pkgconfig: 'pkgdatadir')
  layer_shell_protocol = files('protocols/wlr-layer-shell-unstable-v1.xml')
  layer_shell_header = custom_target(
    'wlr-layer-shell-client-header',
    input: layer_shell_protocol,
    output: 'wlr-layer-shell-unstable-v1-client-protocol.h',
    command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
  )
  layer_shell_code = custom_target(
    'wlr-layer-shell-client-code',
    input: layer_shell_protocol,
    output: 'wlr-layer-shell-unstable-v1-protocol.c',
    command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
  )
  xdg_shell_protocol = files(wl_protocols_dir / 'stable/xdg-shell/xdg-shell.xml')
  xdg_shell_header = custom_target(
    'xdg-shell-client-header',
    input: xdg_shell_protocol,
    output: 'xdg-shell-client-protocol.h',
    command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
  )
  xdg_shell_code = custom_target(
    'xdg-shell-client-code',
    input: xdg_shell_protocol,
    output: 'xdg-shell-protocol.c',
    command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
  )
  idle_inhibit_protocol = files(wl_protocols_dir / 'unstable/idle-inhibit/idle-inhibit-unstable-v1.xml')
  idle_inhibit_header = custom_target(
    'idle-inhibit-client-header',
    input: idle_inhibit_protocol,
    output: 'idle-inhibit-unstable-v1-client-protocol.h',
    command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
  )
  idle_inhibit_code = custom_target(
    'idle-inhibit-client-code',
    input: idle_inhibit_protocol,
    output: 'idle-inhibit-unstable-v1-protocol.c',
    command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
  )

  session_lock_protocol = files(wl_protocols_dir / 'staging/ext-session-lock/ext-session-lock-v1.xml')
  session_lock_header = custom_target(
    'ext-session-lock-client-header',
    input: session_lock_protocol,
    output: 'ext-session-lock-v1-client-protocol.h',
    command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
  )
  session_lock_code = custom_target(
    'ext-session-lock-client-code',
    input: session_lock_protocol,
    output: 'ext-session-lock-v1-protocol.c',
    command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
  )

  platform_deps += [wayland_dep]
  platform_sources += files('src/platform/waylandlayershellintegration.cpp')
  platform_generated += [layer_shell_header, layer_shell_code, xdg_shell_header, xdg_shell_code, idle_inhibit_header, idle_inhibit_code]
  platform_includes += qtgui_private_include
  platform_args += ['-DQBAR_HAVE_WAYLAND=1']

  qtwayland_version = dependency('Qt6WaylandClient').version()
  qtwayland_private_include = include_directories(
    qt_includedir / 'QtCore' / qtwayland_version,
    qt_includedir / 'QtCore' / qtwayland_version / 'QtCore',
    qt_includedir / 'QtWaylandGlobal' / qtwayland_version,
    qt_includedir / 'QtWaylandGlobal' / qtwayland_version / 'QtWaylandGlobal',
    qt_includedir / 'QtWaylandClient' / qtwayland_version,
    qt_includedir / 'QtWaylandClient' / qtwayland_version / 'QtWaylandClient',
  )

  plugin_headers = files('src/wayland-shell-integration/qbar-layer-shell/qbarlayershellintegration.h')
  plugin_sources = files('src/wayland-shell-integration/qbar-layer-shell/qbarlayershellintegration.cpp')
  plugin_moc = qt6.preprocess(moc_headers: plugin_headers)
  wayland_shell_plugin = shared_module(
    'qbar-layer-shell',
    plugin_sources,
    plugin_moc,
    layer_shell_header,
    layer_shell_code,
    xdg_shell_header,
    xdg_shell_code,
    cpp_args: ['-fPIC'],
    include_directories: platform_includes + [qtwayland_private_include],
    dependencies: qt_deps + [dependency('Qt6WaylandClient'), wayland_dep],
    install: true,
    install_dir: get_option('libdir') / 'qt6/plugins/wayland-shell-integration',
    name_prefix: 'lib',
  )

  custom_target(
    'qbar-layer-shell-plugin-copy',
    input: wayland_shell_plugin,
    output: 'qbar-layer-shell-plugin-copy.stamp',
    # Replace the plugin ATOMICALLY (write to a temp, then rename). A plain `cp` truncates and
    # rewrites the SAME inode, and a running qbar mmaps this file MAP_PRIVATE — so overwriting it
    # in place pulls the read-only .text/.data.rel.ro (vtable) pages out from under the live
    # process, corrupting it on the next page fault → crash during recompilation (confirmed: the
    # inode is preserved by cp, and cores taken right after a rebuild showed zeroed plugin
    # vtables → rip=0 in sendThroughObjectEventFilters). rename() gives the destination a NEW
    # inode; the old one stays alive for the running process's mapping, so a rebuild no longer
    # crashes a running bar.
    command: ['sh', '-c', 'mkdir -p wayland-shell-integration && cp "$1" wayland-shell-integration/libqbar-layer-shell.so.tmp && mv -f wayland-shell-integration/libqbar-layer-shell.so.tmp wayland-shell-integration/libqbar-layer-shell.so && touch "$2"', 'sh', '@INPUT@', '@OUTPUT@'],
    build_by_default: true,
  )

  # Session-lock shell integration plugin (qbar-lock, Wayland ext-session-lock-v1).
  session_lock_plugin_headers = files('src/wayland-shell-integration/qbar-session-lock/qbarsessionlockintegration.h')
  session_lock_plugin_sources = files('src/wayland-shell-integration/qbar-session-lock/qbarsessionlockintegration.cpp')
  session_lock_plugin_moc = qt6.preprocess(moc_headers: session_lock_plugin_headers)
  session_lock_plugin = shared_module(
    'qbar-session-lock',
    session_lock_plugin_sources,
    session_lock_plugin_moc,
    session_lock_header,
    session_lock_code,
    cpp_args: ['-fPIC'],
    include_directories: platform_includes + [qtwayland_private_include],
    dependencies: qt_deps + [dependency('Qt6WaylandClient'), wayland_dep],
    install: true,
    install_dir: get_option('libdir') / 'qt6/plugins/wayland-shell-integration',
    name_prefix: 'lib',
  )
  custom_target(
    'qbar-session-lock-plugin-copy',
    input: session_lock_plugin,
    output: 'qbar-session-lock-plugin-copy.stamp',
    # Atomic replace (see the layer-shell copy above for why cp-in-place corrupts a live mmap).
    command: ['sh', '-c', 'mkdir -p wayland-shell-integration && cp "$1" wayland-shell-integration/libqbar-session-lock.so.tmp && mv -f wayland-shell-integration/libqbar-session-lock.so.tmp wayland-shell-integration/libqbar-session-lock.so && touch "$2"', 'sh', '@INPUT@', '@OUTPUT@'],
    build_by_default: true,
  )

  have_wayland_shell = true
endif

sources = files(
  'src/main.cpp',
  'src/barwindow.cpp',
  'src/configreloader.cpp',
  'src/thememanager.cpp',
  'src/qml/baractions.cpp',
  'src/qml/enginebootstrap.cpp',
  'src/qml/widgetreloader.cpp',
  'src/wm/testwindowrules.cpp',
  'src/crashguard.cpp',
  'src/config.cpp',
  'src/brightness/brightnessmodel.cpp',
  'src/caffeine/caffeinemodel.cpp',
  'src/customtool/customtoolmodel.cpp',
  'src/ipc/i3ipcclient.cpp',
  'src/platform/keyboardlayoutcode.cpp',
  'src/platform/platformbarintegration.cpp',
  'src/json/jsonc.cpp',
  'src/dbus/dbusservice.cpp',
  'src/notifications/notificationmodel.cpp',
  'src/notifications/notificationserver.cpp',
  'src/notifications/notificationwindow.cpp',
  'src/qml/qbarpopupservice.cpp',
  'src/qml/dockwindow.cpp',
  'src/qml/qbaripc.cpp',
  'src/qml/jsonasync.cpp',
  'src/qml/jstimers.cpp',
  'src/qml/netfetch.cpp',
  'src/qml/jsprocess.cpp',
  'src/qml/localstorage.cpp',
  'src/qml/modelcapsules.cpp',
  'src/tray/statusnotifiermodel.cpp',
  'src/cpu/cpumodel.cpp',
  'src/temperature/temperaturemodel.cpp',
  'src/sound/audiobackend.cpp',
  'src/sound/audiobackendfactory.cpp',
  'src/sound/soundmodel.cpp',
  'src/mpris/mprismodel.cpp',
  'src/graphics/sparkline.cpp',
  'src/platform/capslockmonitor.cpp',
  'src/network/networkmodel.cpp',
  'src/network/networkprocessmodel.cpp',
  'src/networkmanager/networkmanagermodel.cpp',
  'src/battery/batterymodel.cpp',
  'src/disk/diskmodel.cpp',
  'src/system/loadmodel.cpp',
  'src/system/keyboardstatemodel.cpp',
  'src/system/failedunitsmodel.cpp',
  'src/mpd/mpdmodel.cpp',
  'src/bluetooth/bluetoothmodel.cpp',
  'src/powerprofiles/powerprofilesmodel.cpp',
  'src/upower/upowermodel.cpp',
  'src/user/usermodel.cpp',
  'src/privacy/privacymodel.cpp',
  'src/calendar/calendarmodel.cpp',
  'src/wm/windowmanagerbackend.cpp',
  'src/wm/workspacemodel.cpp',
  'src/wm/windowmodel.cpp',
  'src/wm/nullbackend.cpp',
  'src/wm/hyprlandbackend.cpp',
  'src/wm/bspwmbackend.cpp',
  'src/wm/ewmhbackend.cpp',
  'src/wm/wmbackendfactory.cpp',
)

headers = files(
  'src/barwindow.h',
  'src/configreloader.h',
  'src/thememanager.h',
  'src/qml/baractions.h',
  'src/qml/widgetreloader.h',
  'src/wm/testwindowrules.h',
  'src/ipc/i3ipcclient.h',
  'src/platform/platformbarintegration.h',
  'src/dbus/dbusservice.h',
  'src/notifications/notificationmodel.h',
  'src/notifications/notificationserver.h',
  'src/notifications/notificationwindow.h',
  'src/qml/qbarpopupservice.h',
  'src/qml/dockwindow.h',
  'src/qml/qbaripc.h',
  'src/qml/jsonasync.h',
  'src/qml/jstimers.h',
  'src/qml/netfetch.h',
  'src/qml/jsprocess.h',
  'src/qml/localstorage.h',
  'src/qml/modelcapsules.h',
  'src/brightness/brightnessmodel.h',
  'src/caffeine/caffeinemodel.h',
  'src/customtool/customtoolmodel.h',
  'src/tray/statusnotifiermodel.h',
  'src/cpu/cpumodel.h',
  'src/temperature/temperaturemodel.h',
  'src/sound/audiobackend.h',
  'src/sound/soundmodel.h',
  'src/mpris/mprismodel.h',
  'src/graphics/sparkline.h',
  'src/platform/capslockmonitor.h',
  'src/network/networkmodel.h',
  'src/network/networkprocessmodel.h',
  'src/networkmanager/networkmanagermodel.h',
  'src/battery/batterymodel.h',
  'src/disk/diskmodel.h',
  'src/system/loadmodel.h',
  'src/system/keyboardstatemodel.h',
  'src/system/failedunitsmodel.h',
  'src/mpd/mpdmodel.h',
  'src/bluetooth/bluetoothmodel.h',
  'src/powerprofiles/powerprofilesmodel.h',
  'src/upower/upowermodel.h',
  'src/user/usermodel.h',
  'src/privacy/privacymodel.h',
  'src/calendar/calendarmodel.h',
  'src/wm/windowmanagerbackend.h',
  'src/wm/workspacemodel.h',
  'src/wm/windowmodel.h',
  'src/wm/nullbackend.h',
  'src/wm/hyprlandbackend.h',
  'src/wm/bspwmbackend.h',
  'src/wm/ewmhbackend.h',
)

# Native WirePlumber audio backend (optional). When wireplumber-0.5 is present
# it is compiled and selected at build time instead of the libpulse SoundModel;
# the libpulse path stays the default fallback. Mirrors the x11/wayland feature
# pattern (-DQBAR_HAVE_* + conditional sources/deps).
wireplumber_dep = dependency('wireplumber-0.5', required: get_option('wireplumber'))
if wireplumber_dep.found()
  sources += files('src/sound/wireplumberbackend.cpp')
  headers += files('src/sound/wireplumberbackend.h')
  platform_deps += [wireplumber_dep]
  platform_args += ['-DQBAR_HAVE_WIREPLUMBER=1']
endif

# Privacy module (optional): mic/camera in-use indicator via libpipewire. privacymodel.cpp
# is always compiled (an inert stub without the define); the dep + define are added only when
# libpipewire is present.
pipewire_dep = dependency('libpipewire-0.3', required: get_option('privacy'))
if pipewire_dep.found()
  platform_deps += [pipewire_dep]
  platform_args += ['-DQBAR_HAVE_PIPEWIRE=1']
endif

qresources = [files('qml/qbar.qrc'), files('data/qbar-defaults.qrc')]
if popup_emboss_shader_enabled
  qresources += [popup_emboss_shader_qrc]
endif

# LANG support: translation catalogs for qbar (qbar_i18n) and qbar-lock
# (lockscreen_i18n), embedded under :/i18n.
subdir('i18n')

qt_sources = qt6.preprocess(
  moc_headers: headers + platform_moc_headers,
  qresources: qresources,
)

executable(
  'qbar',
  sources,
  platform_sources,
  platform_generated,
  qt_sources,
  qbar_i18n,
  cpp_args: platform_args,
  include_directories: platform_includes + [include_directories('src')],
  dependencies: qt_deps + platform_deps,
  pie: false,
  install: true,
)

# Tiny client for the JSON IPC — bind it to a key in your compositor
# (e.g. `bindsym $mod+c exec qbar-ipc toggle cpu`).
executable(
  'qbar-ipc',
  files('src/tools/qbaripccli.cpp'),
  dependencies: [dependency('Qt6Core'), dependency('Qt6Network')],
  install: true,
)

lockscreen_dep = dependency('pam', required: get_option('lockscreen'))
if lockscreen_dep.found()
  lockscreen_args = []
  lockscreen_deps = [
    dependency('Qt6Core'),
    dependency('Qt6Gui'),
    dependency('Qt6Qml'),
    dependency('Qt6Quick'),
    dependency('Qt6DBus'),
    lockscreen_dep,
    qml_css_dep,
  ]
  if x11_dep.found()
    lockscreen_args += ['-DQBAR_LOCK_HAVE_X11=1']
    lockscreen_deps += [x11_dep]
  endif

  # The Wayland lock backend drives ext-session-lock-v1 via the qbar-session-lock plugin;
  # it needs the generated protocol (for unlock_and_destroy + the availability probe).
  lockscreen_generated = []
  if have_wayland_shell
    lockscreen_args += ['-DQBAR_LOCK_HAVE_WAYLAND=1']
    lockscreen_deps += [wayland_dep]
    lockscreen_generated += [session_lock_header, session_lock_code]
  endif

  lockscreen_headers = files(
    'src/lock/lockbackend.h',
    'src/lock/lockcontroller.h',
    'src/lock/pamauthenticator.h',
    'src/lock/fprintauthenticator.h',
    'src/lock/waylandlockbackend.h',
    'src/lock/x11lockbackend.h',
    'src/user/usermodel.h',
    'src/platform/capslockmonitor.h',
  )
  lockscreen_qt_sources = qt6.preprocess(
    moc_headers: lockscreen_headers,
    qresources: files('qml/qbar-lock.qrc'),
  )
  executable(
    'qbar-lock',
    files(
      'src/lock/lockmain.cpp',
      'src/lock/lockbackend.cpp',
      'src/lock/lockcontroller.cpp',
      'src/lock/pamauthenticator.cpp',
      'src/lock/fprintauthenticator.cpp',
      'src/lock/waylandlockbackend.cpp',
      'src/lock/x11lockbackend.cpp',
      'src/user/usermodel.cpp',
      'src/platform/capslockmonitor.cpp',
    ),
    lockscreen_qt_sources,
    lockscreen_generated,
    lockscreen_i18n,
    cpp_args: lockscreen_args,
    include_directories: [include_directories('src'), qtgui_private_include],
    dependencies: lockscreen_deps,
    install: true,
  )
  install_data(
    'data/themes/bliss-98-lock.css',
    'data/themes/i3lock.css',
    'data/themes/macchiato-lock.css',
    install_dir: get_option('datadir') / 'qbar' / 'themes',
  )
  install_data(
    'data/pam/qbar-lock',
    'data/pam/qbar-lock-face',
    install_dir: get_option('sysconfdir') / 'pam.d',
  )
  install_data(
    'data/qbar-lock.desktop',
    install_dir: get_option('datadir') / 'applications',
  )
endif

install_data(
  'data/qbar.desktop',
  install_dir: get_option('datadir') / 'applications',
)

if get_option('tests')

qt_test_dep = dependency('Qt6Test')
qt_quick_test_dep = dependency('Qt6QuickTest')

wm_test_headers = files('tests/test_wm.h')
wm_test_moc = qt6.preprocess(
  moc_headers: wm_test_headers,
)

wm_test_exe = executable(
  'test-wm',
  files(
    'tests/test_wm.cpp',
    'src/wm/windowmanagerbackend.cpp',
    'src/wm/workspacemodel.cpp',
    'src/wm/windowmodel.cpp',
    'src/wm/nullbackend.cpp',
    'src/wm/hyprlandbackend.cpp',
    'src/wm/bspwmbackend.cpp',
    'src/wm/ewmhbackend.cpp',
    'src/wm/wmbackendfactory.cpp',
    'src/ipc/i3ipcclient.cpp',
    'src/platform/keyboardlayoutcode.cpp',
  ),
  wm_test_moc,
  qt6.preprocess(moc_headers: files(
    'src/wm/windowmanagerbackend.h',
    'src/wm/workspacemodel.h',
    'src/wm/windowmodel.h',
    'src/wm/nullbackend.h',
    'src/wm/hyprlandbackend.h',
    'src/wm/bspwmbackend.h',
    'src/wm/ewmhbackend.h',
    'src/ipc/i3ipcclient.h',
  )),
  dependencies: qt_deps + [qt_test_dep],
)

test('wm-backends', wm_test_exe)

css_test_moc = qt6.preprocess(
  moc_headers: files('tests/test_css.h'),
)

css_test_exe = executable(
  'test-css',
  files(
    'tests/test_css.cpp',
  ),
  css_test_moc,
  dependencies: [
    dependency('Qt6Core'),
    dependency('Qt6Gui'),
    dependency('Qt6Qml'),
    qt_test_dep,
    qml_css_dep,
  ],
)

test('css-theme', css_test_exe)

jsonc_test_moc = qt6.preprocess(
  moc_headers: files('tests/test_jsonc.h'),
)

jsonc_test_exe = executable(
  'test-jsonc',
  files(
    'tests/test_jsonc.cpp',
    'src/json/jsonc.cpp',
  ),
  jsonc_test_moc,
  dependencies: [
    dependency('Qt6Core'),
    qt_test_dep,
  ],
  include_directories: include_directories('src'),
)

test('jsonc', jsonc_test_exe)

localstorage_test_moc = qt6.preprocess(
  moc_headers: files(
    'tests/test_localstorage.h',
    'src/qml/localstorage.h',
  ),
)

localstorage_test_exe = executable(
  'test-localstorage',
  files(
    'tests/test_localstorage.cpp',
    'src/qml/localstorage.cpp',
  ),
  localstorage_test_moc,
  dependencies: [
    dependency('Qt6Core'),
    dependency('Qt6Qml'),
    dependency('sqlite3'),
    qt_test_dep,
  ],
  include_directories: include_directories('src'),
)

test('local-storage', localstorage_test_exe)

jsonasync_test_moc = qt6.preprocess(
  moc_headers: files(
    'tests/test_jsonasync.h',
    'src/qml/jsonasync.h',
  ),
)

jsonasync_test_exe = executable(
  'test-jsonasync',
  files(
    'tests/test_jsonasync.cpp',
    'src/qml/jsonasync.cpp',
  ),
  jsonasync_test_moc,
  dependencies: [
    dependency('Qt6Core'),
    dependency('Qt6Qml'),
    qt_test_dep,
  ],
  include_directories: include_directories('src'),
)

test('json-async', jsonasync_test_exe)

customtool_test_moc = qt6.preprocess(
  moc_headers: files(
    'tests/test_customtool.h',
    'src/customtool/customtoolmodel.h',
  ),
)

customtool_test_exe = executable(
  'test-customtool',
  files(
    'tests/test_customtool.cpp',
    'src/customtool/customtoolmodel.cpp',
  ),
  customtool_test_moc,
  dependencies: [
    dependency('Qt6Core'),
    qt_test_dep,
  ],
)

test('custom-tool', customtool_test_exe)

qml_test_moc = qt6.preprocess(
  moc_headers: files(
    'tests/qmltests.h',
    'src/customtool/customtoolmodel.h',
    'src/graphics/sparkline.h',
  ),
  qresources: files('tests/qml/qbar-test.qrc'),
)

qml_test_exe = executable(
  'test-qml',
  files(
    'tests/qmltests.cpp',
    'src/customtool/customtoolmodel.cpp',
    'src/graphics/sparkline.cpp',
  ),
  qml_test_moc,
  dependencies: [
    dependency('Qt6Core'),
    dependency('Qt6Gui'),
    dependency('Qt6Qml'),
    dependency('Qt6Quick'),
    qt_quick_test_dep,
  ],
)

test(
  'qml-applets',
  qml_test_exe,
  args: ['-input', meson.project_source_root() / 'tests/qml'],
  env: ['QT_QPA_PLATFORM=offscreen'],
)

endif # tests
