# LANG support: compile *.ts and embed the .qm catalogs under :/i18n.
# The qrc lists the .qm files; meson finds the sibling .ts and runs lrelease+rcc.
# Lives in its own meson.build because compile_translations(qresource:) rejects
# paths with a subdirectory component.
qbar_i18n = qt6.compile_translations(qresource: 'qbar-i18n.qrc')
lockscreen_i18n = qt6.compile_translations(qresource: 'qbar-lock-i18n.qrc')
