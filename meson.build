project(
  'qbar',
  'c',
  'cpp',
  version: '0.1.0',
  default_options: [
    'b_pie=false',
    'cpp_std=c++20',
    'warning_level=2',
  ],
)

qt6 = import('qt6')

add_project_arguments('-fPIC', language: 'cpp')

qt_deps = [
  dependency('Qt6Core'),
  dependency('Qt6Gui'),
  dependency('Qt6Widgets'),
  dependency('Qt6Network'),
  dependency('Qt6Qml'),
  dependency('Qt6Quick'),
  dependency('Qt6DBus'),
  dependency('sqlite3'),
  dependency('xkbregistry'),
  dependency('libedataserver-1.2'),
  dependency('libecal-2.0'),
]

qsb_program = find_program('/usr/lib/qt6/bin/qsb', required: false)
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
platform_generated = []
platform_args = []
platform_includes = []
wayland_shell_plugin = []

if popup_emboss_shader_enabled
  platform_args += ['-DQBAR_HAVE_POPUP_EMBOSS_SHADER=1']
  platform_generated += [popup_emboss_shader_qsb]
endif

qtgui_version = dependency('Qt6Gui').version()
qtgui_private_include = include_directories(
  '/usr/include/qt6/QtGui/' + qtgui_version,
  '/usr/include/qt6/QtGui/' + qtgui_version + '/QtGui',
)

x11_dep = dependency('xcb', required: get_option('x11'))
xcb_ewmh_dep = dependency('xcb-ewmh', required: get_option('x11'))
if x11_dep.found() and xcb_ewmh_dep.found()
  platform_deps += [x11_dep, xcb_ewmh_dep]
  platform_sources += files('src/platform/x11barintegration.cpp')
  platform_includes += qtgui_private_include
  platform_args += ['-DQBAR_HAVE_X11=1']
endif

wayland_dep = dependency('wayland-client', required: get_option('wayland'))
wlroots_dep = dependency('wlroots-0.19', required: get_option('wayland'))
wayland_scanner = find_program('wayland-scanner', required: get_option('wayland'))
if wayland_dep.found() and wlroots_dep.found() and wayland_scanner.found()
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
  xdg_shell_protocol = files('/usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml')
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
  idle_inhibit_protocol = files('/usr/share/wayland-protocols/unstable/idle-inhibit/idle-inhibit-unstable-v1.xml')
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

  platform_deps += [wayland_dep]
  platform_sources += files('src/platform/waylandlayershellintegration.cpp')
  platform_generated += [layer_shell_header, layer_shell_code, xdg_shell_header, xdg_shell_code, idle_inhibit_header, idle_inhibit_code]
  platform_includes += qtgui_private_include
  platform_args += ['-DQBAR_HAVE_WAYLAND=1']

  qtwayland_version = dependency('Qt6WaylandClient').version()
  qtwayland_private_include = include_directories(
    '/usr/include/qt6/QtCore/' + qtwayland_version,
    '/usr/include/qt6/QtCore/' + qtwayland_version + '/QtCore',
    '/usr/include/qt6/QtWaylandGlobal/' + qtwayland_version,
    '/usr/include/qt6/QtWaylandGlobal/' + qtwayland_version + '/QtWaylandGlobal',
    '/usr/include/qt6/QtWaylandClient/' + qtwayland_version,
    '/usr/include/qt6/QtWaylandClient/' + qtwayland_version + '/QtWaylandClient',
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
    command: ['sh', '-c', 'mkdir -p wayland-shell-integration && cp "$1" wayland-shell-integration/libqbar-layer-shell.so && touch "$2"', 'sh', '@INPUT@', '@OUTPUT@'],
    build_by_default: true,
  )
endif

sources = files(
  'src/main.cpp',
  'src/barwindow.cpp',
  'src/config.cpp',
  'src/brightness/brightnessmodel.cpp',
  'src/caffeine/caffeinemodel.cpp',
  'src/customtool/customtoolmodel.cpp',
  'src/ipc/i3ipcclient.cpp',
  'src/platform/platformbarintegration.cpp',
  'src/css/csstheme.cpp',
  'src/qml/qbarpopupservice.cpp',
  'src/tray/statusnotifiermodel.cpp',
  'src/cpu/cpumodel.cpp',
  'src/temperature/temperaturemodel.cpp',
  'src/sound/soundmodel.cpp',
  'src/network/networkmodel.cpp',
  'src/networkmanager/networkmanagermodel.cpp',
  'src/battery/batterymodel.cpp',
  'src/calendar/calendarmodel.cpp',
  'src/wm/windowmanagerbackend.cpp',
  'src/wm/workspacemodel.cpp',
  'src/wm/nullbackend.cpp',
  'src/wm/hyprlandbackend.cpp',
  'src/wm/bspwmbackend.cpp',
  'src/wm/ewmhbackend.cpp',
  'src/wm/wmbackendfactory.cpp',
)

headers = files(
  'src/barwindow.h',
  'src/ipc/i3ipcclient.h',
  'src/platform/platformbarintegration.h',
  'src/css/csstheme.h',
  'src/qml/qbarpopupservice.h',
  'src/brightness/brightnessmodel.h',
  'src/caffeine/caffeinemodel.h',
  'src/customtool/customtoolmodel.h',
  'src/tray/statusnotifiermodel.h',
  'src/cpu/cpumodel.h',
  'src/temperature/temperaturemodel.h',
  'src/sound/soundmodel.h',
  'src/network/networkmodel.h',
  'src/networkmanager/networkmanagermodel.h',
  'src/battery/batterymodel.h',
  'src/calendar/calendarmodel.h',
  'src/wm/windowmanagerbackend.h',
  'src/wm/workspacemodel.h',
  'src/wm/nullbackend.h',
  'src/wm/hyprlandbackend.h',
  'src/wm/bspwmbackend.h',
  'src/wm/ewmhbackend.h',
)

qresources = [files('qml/qbar.qrc')]
if popup_emboss_shader_enabled
  qresources += [popup_emboss_shader_qrc]
endif

qt_sources = qt6.preprocess(
  moc_headers: headers,
  qresources: qresources,
)

executable(
  'qbar',
  sources,
  platform_sources,
  platform_generated,
  qt_sources,
  cpp_args: platform_args,
  include_directories: platform_includes,
  dependencies: qt_deps + platform_deps,
  pie: false,
  install: true,
)

install_data(
  'data/qbar.desktop',
  install_dir: get_option('datadir') / 'applications',
)

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
    'src/wm/nullbackend.cpp',
    'src/wm/hyprlandbackend.cpp',
    'src/wm/bspwmbackend.cpp',
    'src/wm/ewmhbackend.cpp',
    'src/wm/wmbackendfactory.cpp',
    'src/ipc/i3ipcclient.cpp',
  ),
  wm_test_moc,
  qt6.preprocess(moc_headers: files(
    'src/wm/windowmanagerbackend.h',
    'src/wm/workspacemodel.h',
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
    'src/css/csstheme.cpp',
  ),
  css_test_moc,
  qt6.preprocess(moc_headers: files('src/css/csstheme.h')),
  dependencies: [
    dependency('Qt6Core'),
    dependency('Qt6Gui'),
    qt_test_dep,
  ],
)

test('css-theme', css_test_exe)

qml_test_moc = qt6.preprocess(
  moc_headers: files(
    'tests/qmltests.h',
    'src/customtool/customtoolmodel.h',
  ),
  qresources: files('tests/qml/qbar-test.qrc'),
)

qml_test_exe = executable(
  'test-qml',
  files(
    'tests/qmltests.cpp',
    'src/customtool/customtoolmodel.cpp',
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
