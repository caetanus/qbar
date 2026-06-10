project(
  'qbar',
  'cpp',
  version: '0.1.0',
  default_options: [
    'b_pie=true',
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
  dependency('Qt6Qml'),
  dependency('Qt6Quick'),
  dependency('Qt6QuickWidgets'),
  dependency('Qt6DBus'),
]

sources = files(
  'src/main.cpp',
  'src/barwindow.cpp',
  'src/applethost.cpp',
  'src/config.cpp',
)

headers = files(
  'src/barwindow.h',
  'src/applethost.h',
)

qt_sources = qt6.preprocess(
  moc_headers: headers,
  qresources: files('qml/qbar.qrc'),
)

executable(
  'qbar',
  sources,
  qt_sources,
  dependencies: qt_deps,
  install: true,
)
